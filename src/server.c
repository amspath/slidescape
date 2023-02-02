/*
  Slidescape, a whole-slide image viewer for digital pathology.
  Copyright (C) 2019-2023  Pieter Valkema

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#ifndef IS_SERVER
#define IS_SERVER 1 // should be defined by the command-line because we also need to compile e.g. tiff.c which is shared
#endif

#ifdef _WIN32
#define _WIN32_WINNT 0x0600
#define WINVER 0x0600
#endif

#if !defined(MBEDTLS_CONFIG_FILE)
#include "mbedtls/config.h"
#else
#include MBEDTLS_CONFIG_FILE
#endif

#if defined(MBEDTLS_PLATFORM_C)
#include "mbedtls/platform.h"
#else
#include <stdio.h>
#include <stdlib.h>
#define mbedtls_fprintf    fprintf
#define mbedtls_printf     printf
#define mbedtls_snprintf   snprintf
#define mbedtls_exit            exit
#define MBEDTLS_EXIT_SUCCESS    EXIT_SUCCESS
#define MBEDTLS_EXIT_FAILURE    EXIT_FAILURE
#endif



#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/certs.h"
#include "mbedtls/x509.h"
#include "mbedtls/ssl.h"
#include "mbedtls/net_sockets.h"
#include "mbedtls/error.h"

#if defined(MBEDTLS_SSL_CACHE_C)
#include "mbedtls/ssl_cache.h"
#endif

#if defined(MBEDTLS_MEMORY_BUFFER_ALLOC_C)
#include "mbedtls/memory_buffer_alloc.h"
#endif

#define STB_SPRINTF_IMPLEMENTATION // normally implemented by ImGui, but the server doesn't have that
#include "common.h"
#include "platform.h"

#include <stdio.h>
#include <string.h>    //strlen
#include <sys/stat.h>
#include <pthread.h>

#include <time.h>
#include <errno.h>

#include "tiff.h"
#include "stringutils.h"

#define MAX_NUM_THREADS 16
#define SERVER_VERBOSE 1

typedef struct {
	mbedtls_net_context client_fd;
	int thread_complete;
	const mbedtls_ssl_config *config;
} thread_info_t;

typedef struct {
	int active;
	thread_info_t   data;
	pthread_t       thread;
} pthread_info_t;

static thread_info_t    base_info;
static pthread_info_t   threads[MAX_NUM_THREADS];

typedef struct {
	thread_info_t *thread_info;
	mbedtls_net_context *client_fd;
	long int thread_id;
	mbedtls_ssl_context ssl;
} server_connection_t;

bool ssl_send(server_connection_t* connection, u8* buf, i32 send_size) {
	/*
	 * 7. Write the 200 Response
	 */
	mbedtls_printf( "  [ #%ld ]  > Write to client:\n", connection->thread_id );
	i32 ret = 1;

	u8* send_buffer_pos = buf;
	u32 send_size_remaining = (u32)send_size;

	bool32 sent = false;
	i32 bytes_written = 0;
	i32 total_bytes_written = 0;

//	len = sprintf( (char *) buf, HTTP_RESPONSE,
//	               mbedtls_ssl_get_ciphersuite( &connection.ssl ) );

	while (!sent) {
		while( ( ret = mbedtls_ssl_write( &connection->ssl, send_buffer_pos, send_size_remaining ) ) <= 0 )
		{
			if( ret == MBEDTLS_ERR_NET_CONN_RESET )
			{
				mbedtls_printf( "  [ #%ld ]  failed: peer closed the connection\n",
				                connection->thread_id );
				return false;
			}

			if( ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE )
			{
				mbedtls_printf( "  [ #%ld ]  failed: mbedtls_ssl_write returned -0x%04x\n",
				                connection->thread_id, ret );
				return false;
			}
		}

		bytes_written = ret;
		total_bytes_written += bytes_written;
		send_buffer_pos += bytes_written;
		send_size_remaining -= bytes_written;
		if (total_bytes_written >= send_size) {
			sent = true;
		}

	}


	mbedtls_printf( "  [ #%ld ]  %d bytes written\n=====\n\n=====\n",
	                connection->thread_id, total_bytes_written);
	return true;
}


static char identity_str[0xFF] = {0};

pthread_cond_t semaphore_work_available;
pthread_mutex_t work_mutex;
volatile int work_available = -1;

//https://stackoverflow.com/questions/1157209/is-there-an-alternative-sleep-function-in-c-to-milliseconds
int msleep(long msec) {
	struct timespec ts;
	int res;

	if (msec < 0)
	{
		errno = EINVAL;
		return -1;
	}

	ts.tv_sec = msec / 1000;
	ts.tv_nsec = (msec % 1000) * 1000000;

	do {
		res = nanosleep(&ts, &ts);
	} while (res && errno == EINTR);

	return res;
}

int read_from_file(const char *fname, void *buf, int max_len) {
	FILE *f = fopen(fname, "rb");
	if (f) {
		int size = fread(buf, 1, max_len - 1, f);
		if (size > 0)
			((unsigned char *)buf)[size] = 0;
		else
			((unsigned char *)buf)[0] = 0;
		fclose(f);
		return size;
	}
	return 0;
}



enum http_request_method {
	HTTP_GET = 1,
	HTTP_HEAD = 2,
	HTTP_POST = 3,
	HTTP_PUT = 4,
	HTTP_DELETE = 5,
};

typedef struct http_request_t {
	u32 method;
	char* method_name;
	char* uri;
	char* protocol;
} http_request_t;

http_request_t* parse_http_headers(const char* http_headers, u64 size) {
	http_request_t* result = calloc(1, sizeof(http_request_t) + size + 1);
	char* headers_copy = (void*)result + sizeof(http_request_t);
	memcpy(headers_copy, http_headers, size);
	headers_copy[size] = '\0';
	size_t num_lines = 0;
	char** lines = split_into_lines(headers_copy, &num_lines);
	if (!lines) goto fail;
//	for (i32 i = 0; i < num_lines; ++i) {
//		printf("header %d: %s\n", i, lines[i]);
//	}
	char* request_line = lines[0];
	char* method = request_line;
	char* uri = find_next_token(method, ' ');
	char* protocol = find_next_token(uri, ' ');
	if (!method || !uri || !protocol) goto fail;
	strip_character(request_line, ' ');
//	printf("request line: method=%s uri=%s protocol=%s\n", method, uri, protocol);

	if (strcmp(method, "GET") == 0) {
		result->method = HTTP_GET;
	} else if (strcmp(method, "POST") == 0) {
		result->method = HTTP_POST;
	} else if (strcmp(method, "PUT") == 0) {
		result->method = HTTP_PUT;
	} else if (strcmp(method, "DELETE") == 0) {
		result->method = HTTP_DELETE;
	}
	result->method_name = method;
	result->uri = uri;
	result->protocol = protocol;

	goto cleanup;
	fail:
	printf("Error: malformed HTTP headers\n");
	free(result);
	return NULL;


	cleanup:
	if (lines) free(lines);
	return result;
}

#define SLIDE_API_MAX_PAR 32

typedef struct slide_api_call_t {
	i32 par_count;
	union {
		char* pars[SLIDE_API_MAX_PAR];
		struct {
			char *command;
			char *filename;
			char *parameter1;
			char *parameter2;
		};
	};

} slide_api_call_t;

slide_api_call_t* interpret_api_request(http_request_t* request) {
	if (!request) return NULL;
	switch(request->method) {
		default: {
			printf("unknown API call: %s %s %s\n", request->method_name, request->uri, request->protocol);
		} break;
		case HTTP_GET:
		case HTTP_POST: {
//			printf("interpreting API call: %s %s %s\n", request->method_name, request->uri, request->protocol);
			size_t uri_len = strlen(request->uri);
			slide_api_call_t* result = calloc(1, sizeof(slide_api_call_t) + uri_len + 1);
			char* uri = (void*)result + sizeof(slide_api_call_t);
			memcpy(uri, request->uri, uri_len);
			char* root = uri;
			i32 par_count = 0;
			char* par = root;
			do {
				par = find_next_token(par, '/');
				result->pars[par_count++] = par;
			} while (par && (par_count < SLIDE_API_MAX_PAR));
			strip_character(uri, '/');
			result->par_count = par_count;
/*
			char* command = find_next_token(root, '/');
			char* filename = find_next_token(command, '/');
			char* parameter1 = find_next_token(filename, '/');
			char* parameter2 = find_next_token(parameter1, '/');
			result->command = command;
			result->filename = filename;
			result->parameter1 = parameter1;
			result->parameter2 = parameter2;*/
			return result;
		} break;
	}
	return NULL;
}

void locate_file_prepend_env(const char* base_filename, const char* env, char* path_buffer, size_t buffer_size) {
	if (base_filename) {
		char* prefix = getenv(env);
		if (prefix) {
			snprintf(path_buffer, buffer_size, "%s/%s", prefix, base_filename);
			return;
		}
	}
	// failed
	strcpy(path_buffer, base_filename);
}

bool server_send_test(server_connection_t* connection) {
	bool32 success = false;

	mem_t* file_mem = platform_read_entire_file("test_google.html");
	if (file_mem) {
		success = ssl_send(connection, file_mem->data, file_mem->len);
		free(file_mem);
	}

	return success;
}


bool32 execute_slide_set_api_call(server_connection_t* connection, slide_api_call_t *call) {
	bool32 success = false;

	char path_buffer[2048];
	path_buffer[0] = '\0';
	locate_file_prepend_env(call->filename, "SLIDES_DIR", path_buffer, sizeof(path_buffer));
	mem_t* file_mem = platform_read_entire_file(path_buffer);
	if (file_mem) {
		success = ssl_send(connection, file_mem->data, file_mem->len);
		free(file_mem);
	}

	return success;
}

bool32 execute_slide_api_call(server_connection_t* connection, slide_api_call_t *call) {
	if (!call || !call->command) return false;
	bool32 success = false;

	if (strcmp(call->command, "slide_set") == 0) {
		success = execute_slide_set_api_call(connection, call);
	}
	else if (strcmp(call->command, "test") == 0) {
		success = server_send_test(connection);
	}

	else if (strcmp(call->command, "slide") == 0) {
		// If the SLIDES_DIR environment variable is set, load slides from there
		char path_buffer[2048];
		path_buffer[0] = '\0';
		locate_file_prepend_env(call->filename, "SLIDES_DIR", path_buffer, sizeof(path_buffer));
		const char* ext = get_file_extension(path_buffer);
		if (strcasecmp(ext, "tiff") != 0) {
			// TODO: search for files with this pattern
			size_t path_len = strlen(path_buffer);
			snprintf(path_buffer + path_len, sizeof(path_buffer) - path_len, ".tiff");
		}

		char* parameter1 = call->parameter1;
		char* parameter2 = call->parameter2;
		// is the client requesting TIFF header and metadata?
		if (parameter1 && strcmp(parameter1, "header") == 0) {
			if (file_exists(path_buffer)) {

				tiff_t tiff = {0};
				if (open_tiff_file(&tiff, path_buffer)) {
					memrw_t payload_buffer = {};
					tiff_serialize(&tiff, &payload_buffer);

					// rewrite the HTTP headers at the start, the Content-Length now isn't correct
					char http_headers[4096];
					snprintf(http_headers, sizeof(http_headers),
					         "HTTP/1.1 200 OK\r\nConnection: close\r\nContent-type: application/octet-stream\r\nContent-length: %-16llu\r\n\r\n",
					         payload_buffer.used_size);
					u64 http_headers_size = strlen(http_headers);

					u64 send_size = http_headers_size + payload_buffer.used_size;
					u8* send_buffer = malloc(send_size);
					memcpy(send_buffer, http_headers, http_headers_size);
					memcpy(send_buffer + http_headers_size, payload_buffer.data, payload_buffer.used_size);
					success = ssl_send(connection, send_buffer, send_size);

//				    tls_close_notify(context);
//				    send_pending(client_sock, context);
					free(send_buffer);
					memrw_destroy(&payload_buffer);
					tiff_destroy(&tiff);
				} else {
					fprintf(stderr, "Couldn't open TIFF file %s\n", path_buffer);
					success = false;
				}


			} else {
				fprintf(stderr, "Couldn't open file %s\n", path_buffer);
				success = false;
			}
		}
		else if (parameter1 && parameter2){

			// try to interpret as batch
			i32 batch_size = (call->par_count - 2) / 2; // minus two, because 'command' and 'filename' are also counted
			ASSERT(batch_size >= 1);
			i64* chunk_offsets = alloca(batch_size * sizeof(i64));
			i64* chunk_sizes = alloca(batch_size * sizeof(i64));
			i64 total_size = 0;
			for (i32 i = 0; i < batch_size; ++i) {
				// try to interpret the parameters as numbers
				chunk_offsets[i] = atoll(call->pars[2+2*i]);
				chunk_sizes[i] = atoll(call->pars[3+2*i]);
				total_size += chunk_sizes[i];
			}


			if (total_size > 0) {

				FILE* fp = fopen64(path_buffer, "rb");
				if (fp) {
					struct stat st;
					if (fstat(fileno(fp), &st) == 0) {
						i64 filesize = st.st_size;

						char http_headers[4096];
						snprintf(http_headers, sizeof(http_headers),
						         "HTTP/1.1 200 OK\r\nConnection: close\r\nContent-type: application/octet-stream\r\nContent-length: %llu\r\n\r\n",
						         total_size);
						u64 http_headers_size = strlen(http_headers);

						u64 send_size = http_headers_size + total_size;
						u8* send_buffer = malloc(send_size);
						memcpy(send_buffer, http_headers, http_headers_size);
						u8* data_buffer = send_buffer + http_headers_size;
						u8* data_buffer_pos = data_buffer;

						bool32 ok = true;

						for (i32 i = 0; i < batch_size; ++i) {
							// try to interpret the parameters as numbers
							i64 requested_offset = chunk_offsets[i];
							i64 requested_size = chunk_sizes[i];
							fseeko64(fp, requested_offset, SEEK_SET);
							ok = ok && (fread(data_buffer_pos, requested_size, 1, fp) == 1);
							if (!ok) {
								printf("Error reading from %s\n", call->filename);
							}
							data_buffer_pos += requested_size;
						}

						if (ok) {

							ssl_send(connection, send_buffer, send_size);

						}
						free(send_buffer);
					}
					fclose(fp);
				}

			}
		}
	} else {
		printf("Slide API: unknown command %s\n", call->command);
	};
	return success;
}




#if 0
void* worker_thread(void* arg_ptr) {

	for (;;) {
		/* thread code blocks here until work is available */
		pthread_mutex_lock(&work_mutex);
		while (work_available <= 0) {
			pthread_cond_wait(&semaphore_work_available, &work_mutex);
		}
		pthread_mutex_unlock(&work_mutex);
		/* proceed with thread execution */

		int client_socket = work_available;
		work_available = 0;

		connection_handler(&client_socket);


//	pthread_cond_signal(&semaphore_work_grabbed);
	}

	return 0;
}

// https://stackoverflow.com/questions/21405204/multithread-server-client-implementation-in-c

typedef struct {
	i32 client_socket;
	pthread_cond_t cond;
} thread_data_t;

int main2(int argc , char *argv[]) {
	int socket_desc , client_sock , read_size;
	socklen_t c;
	struct sockaddr_in server , client;
	char client_message[0xFFFF];

#ifdef _WIN32
	WSADATA wsaData;
	WSAStartup(MAKEWORD(2, 2), &wsaData);
#else
	signal(SIGPIPE, SIG_IGN);
#endif

	tls_init();

	pthread_t threads[MAX_NUM_THREADS] = {0};

	pthread_cond_init(&semaphore_work_available, NULL);
	pthread_mutex_init(&work_mutex, NULL);

	pthread_mutex_lock(&work_mutex);

	for (i64 i = 1; i < COUNT(threads); ++i) {
		if (pthread_create(threads + i, NULL, &worker_thread, (void*)i) != 0) {
			fprintf(stderr, "Error creating thread\n");
			return 1;
		}
	}

	socket_desc = socket(AF_INET , SOCK_STREAM , 0);
	if (socket_desc == -1) {
		printf("Could not create socket");
		return 1;
	}

	int port = 2000;
	if (argc > 1) {
		port = atoi(argv[1]);
		if (port <= 0)
			port = 2000;
	}
	server.sin_family = AF_INET;
	server.sin_addr.s_addr = INADDR_ANY;
	server.sin_port = htons(port);

	int enable = 1;
	int timeout = 5000;
	setsockopt(socket_desc, SOL_SOCKET, SO_REUSEADDR, (char*)&enable, sizeof(int));
	setsockopt(socket_desc, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(int));
	setsockopt(socket_desc, SOL_SOCKET, SO_SNDTIMEO, (char*)&timeout, sizeof(int));

	if( bind(socket_desc,(struct sockaddr *)&server , sizeof(server)) < 0) {
		perror("bind failed. Error");
		return 1;
	}

	listen(socket_desc , 3);

	c = sizeof(struct sockaddr_in);

	/*struct TLSContext **/server_context = tls_create_context(1, TLS_V13);

	// Note: to generate certificates for local testing:
	// https://letsencrypt.org/docs/certificates-for-localhost/
	if (!load_keys(server_context, "testcert/fullchain.pem", "testcert/privkey.pem")) {
		exit(1);
	}

	fprintf(stderr, "Listening on port %d\n", port);

	while (1) {
		identity_str[0] = 0;

		client_sock = accept(socket_desc, (struct sockaddr *)&client, &c);
		if (client_sock < 0) {
			perror("accept failed");
			return 1;
		}

		work_available = client_sock;
		while (work_available) {
			pthread_mutex_unlock(&work_mutex);
			pthread_cond_signal(&semaphore_work_available);
			msleep(1);
//			pthread_mutex_lock(&work_mutex);
//			printf("waiting for someone to grab the work\n");
		}

	}
	tls_destroy_context(server_context);
	return 0;
}





#endif





mbedtls_threading_mutex_t debug_mutex;

static void my_mutexed_debug( void *ctx, int level,
                              const char *file, int line,
                              const char *str )
{
	long int thread_id = (long int) pthread_self();

	mbedtls_mutex_lock( &debug_mutex );

	((void) level);
	mbedtls_fprintf( (FILE *) ctx, "%s:%04d: [ #%ld ] %s",
	                 file, line, thread_id, str );
	fflush(  (FILE *) ctx  );

	mbedtls_mutex_unlock( &debug_mutex );
}



static void *handle_ssl_connection( void *data )
{
	int ret, len;
//	thread_info_t *thread_info = (thread_info_t *) data;
//	mbedtls_net_context *client_fd = &thread_info->client_fd;
//	long int thread_id = (long int) pthread_self();
	unsigned char buf[1024];
//	mbedtls_ssl_context ssl;

	server_connection_t connection = {};
	connection.thread_info = (thread_info_t *) data;
	connection.client_fd = &connection.thread_info->client_fd;
	connection.thread_id = (long int) pthread_self();

	/* Make sure memory references are valid */
	mbedtls_ssl_init( &connection.ssl );

	mbedtls_printf( "  [ #%ld ]  Setting up SSL/TLS data\n", connection.thread_id );

	/*
	 * 4. Get the SSL context ready
	 */
	if( ( ret = mbedtls_ssl_setup( &connection.ssl, connection.thread_info->config ) ) != 0 )
	{
		mbedtls_printf( "  [ #%ld ]  failed: mbedtls_ssl_setup returned -0x%04x\n",
		                connection.thread_id, -ret );
		goto thread_exit;
	}

	mbedtls_ssl_set_bio( &connection.ssl, connection.client_fd, mbedtls_net_send, mbedtls_net_recv, NULL );

	/*
	 * 5. Handshake
	 */
	mbedtls_printf( "  [ #%ld ]  Performing the SSL/TLS handshake\n", connection.thread_id );

	while( ( ret = mbedtls_ssl_handshake( &connection.ssl ) ) != 0 )
	{
		if( ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE )
		{
			mbedtls_printf( "  [ #%ld ]  failed: mbedtls_ssl_handshake returned -0x%04x\n",
			                connection.thread_id, -ret );
			goto thread_exit;
		}
	}

	mbedtls_printf( "  [ #%ld ]  ok\n", connection.thread_id );

	/*
	 * 6. Read the HTTP Request
	 */
	mbedtls_printf( "  [ #%ld ]  < Read from client\n", connection.thread_id );

	do
	{
		len = sizeof( buf ) - 1;
		memset( buf, 0, sizeof( buf ) );
		ret = mbedtls_ssl_read( &connection.ssl, buf, len );

		if( ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE )
			continue;

		if( ret <= 0 )
		{
			switch( ret )
			{
				case MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY:
					mbedtls_printf( "  [ #%ld ]  connection was closed gracefully\n",
					                connection.thread_id );
					goto thread_exit;

				case MBEDTLS_ERR_NET_CONN_RESET:
					mbedtls_printf( "  [ #%ld ]  connection was reset by peer\n",
					                connection.thread_id );
					goto thread_exit;

				default:
					mbedtls_printf( "  [ #%ld ]  mbedtls_ssl_read returned -0x%04x\n",
					                connection.thread_id, -ret );
					goto thread_exit;
			}
		}

		len = ret;
		mbedtls_printf( "  [ #%ld ]  %d bytes read\n=====\n%s\n=====\n",
		                connection.thread_id, len, (char *) buf );

		if( ret > 0 )
			break;
	}
	while( 1 );

	// TODO: assemble chunks into request buffer
	http_request_t* request = parse_http_headers((char *) buf, len);
	if (!request) {
		fprintf(stderr, "[thread %d] Warning: bad request\n", connection.thread_id);
	} else {
		fprintf(stderr, "[thread %d] Received request: %s\n", connection.thread_id, request->uri);
		slide_api_call_t* call = interpret_api_request(request);
		if (call) {
			if (execute_slide_api_call(&connection, call)) {
				// success!
			}
		}
	}

	// write
//	bool send_ok = ssl_send(&connection, buf, len);
//	if (!send_ok) goto thread_exit;

	mbedtls_printf( "  [ #%ld ]  . Closing the connection...", connection.thread_id );

	while( ( ret = mbedtls_ssl_close_notify( &connection.ssl ) ) < 0 )
	{
		if( ret != MBEDTLS_ERR_SSL_WANT_READ &&
		    ret != MBEDTLS_ERR_SSL_WANT_WRITE )
		{
			mbedtls_printf( "  [ #%ld ]  failed: mbedtls_ssl_close_notify returned -0x%04x\n",
			                connection.thread_id, ret );
			goto thread_exit;
		}
	}

	mbedtls_printf( " ok\n" );

	ret = 0;

	thread_exit:

#ifdef MBEDTLS_ERROR_C
	if( ret != 0 )
	{
		char error_buf[100];
		mbedtls_strerror( ret, error_buf, 100 );
		mbedtls_printf("  [ #%ld ]  Last error was: -0x%04x - %s\n\n",
		               connection.thread_id, -ret, error_buf );
	}
#endif

	mbedtls_net_free( connection.client_fd );
	mbedtls_ssl_free( &connection.ssl );

	connection.thread_info->thread_complete = 1;

	return( NULL );
}

static int thread_create( mbedtls_net_context *client_fd )
{
	int ret, i;

	/*
	 * Find in-active or finished thread slot
	 */
	for( i = 0; i < MAX_NUM_THREADS; i++ )
	{
		if( threads[i].active == 0 )
			break;

		if( threads[i].data.thread_complete == 1 )
		{
			mbedtls_printf( "  [ main ]  Cleaning up thread %d\n", i );
			pthread_join(threads[i].thread, NULL );
			memset( &threads[i], 0, sizeof(pthread_info_t) );
			break;
		}
	}

	if( i == MAX_NUM_THREADS )
		return( -1 );

	/*
	 * Fill thread-info for thread
	 */
	memcpy( &threads[i].data, &base_info, sizeof(base_info) );
	threads[i].active = 1;
	memcpy( &threads[i].data.client_fd, client_fd, sizeof( mbedtls_net_context ) );

	if( ( ret = pthread_create( &threads[i].thread, NULL, handle_ssl_connection,
	                            &threads[i].data ) ) != 0 )
	{
		return( ret );
	}

	return( 0 );
}



int main( void )
{
	int ret;
	mbedtls_net_context listen_fd, client_fd;
	const char pers[] = "ssl_pthread_server";

	mbedtls_entropy_context entropy;
	mbedtls_ctr_drbg_context ctr_drbg;
	mbedtls_ssl_config conf;
	mbedtls_x509_crt srvcert;
	mbedtls_x509_crt cachain;
	mbedtls_pk_context pkey;
#if defined(MBEDTLS_MEMORY_BUFFER_ALLOC_C)
	unsigned char alloc_buf[100000];
#endif
#if defined(MBEDTLS_SSL_CACHE_C)
	mbedtls_ssl_cache_context cache;
#endif

#if defined(MBEDTLS_MEMORY_BUFFER_ALLOC_C)
	mbedtls_memory_buffer_alloc_init( alloc_buf, sizeof(alloc_buf) );
#endif

#if defined(MBEDTLS_SSL_CACHE_C)
	mbedtls_ssl_cache_init( &cache );
#endif

	mbedtls_x509_crt_init( &srvcert );
	mbedtls_x509_crt_init( &cachain );

	mbedtls_ssl_config_init( &conf );
	mbedtls_ctr_drbg_init( &ctr_drbg );
	memset( threads, 0, sizeof(threads) );
	mbedtls_net_init( &listen_fd );
	mbedtls_net_init( &client_fd );

	mbedtls_mutex_init( &debug_mutex );

	base_info.config = &conf;

	/*
	 * We use only a single entropy source that is used in all the threads.
	 */
	mbedtls_entropy_init( &entropy );

	/*
	 * 1. Load the certificates and private RSA key
	 */
	mbedtls_printf( "\n  . Loading the server cert. and key..." );
	fflush( stdout );

	/*
	 * This demonstration program uses embedded test certificates.
	 * Instead, you may want to use mbedtls_x509_crt_parse_file() to read the
	 * server and CA certificates, as well as mbedtls_pk_parse_keyfile().
	 */
	ret = mbedtls_x509_crt_parse( &srvcert, (const unsigned char *) mbedtls_test_srv_crt,
	                              mbedtls_test_srv_crt_len );
	if( ret != 0 )
	{
		mbedtls_printf( " failed\n  !  mbedtls_x509_crt_parse returned %d\n\n", ret );
		goto exit;
	}

	ret = mbedtls_x509_crt_parse( &cachain, (const unsigned char *) mbedtls_test_cas_pem,
	                              mbedtls_test_cas_pem_len );
	if( ret != 0 )
	{
		mbedtls_printf( " failed\n  !  mbedtls_x509_crt_parse returned %d\n\n", ret );
		goto exit;
	}

	mbedtls_pk_init( &pkey );
	ret =  mbedtls_pk_parse_key( &pkey, (const unsigned char *) mbedtls_test_srv_key,
	                             mbedtls_test_srv_key_len, NULL, 0 );
	if( ret != 0 )
	{
		mbedtls_printf( " failed\n  !  mbedtls_pk_parse_key returned %d\n\n", ret );
		goto exit;
	}

	mbedtls_printf( " ok\n" );

	/*
	 * 1b. Seed the random number generator
	 */
	mbedtls_printf( "  . Seeding the random number generator..." );

	if( ( ret = mbedtls_ctr_drbg_seed( &ctr_drbg, mbedtls_entropy_func, &entropy,
	                                   (const unsigned char *) pers,
	                                   strlen( pers ) ) ) != 0 )
	{
		mbedtls_printf( " failed: mbedtls_ctr_drbg_seed returned -0x%04x\n",
		                -ret );
		goto exit;
	}

	mbedtls_printf( " ok\n" );

	/*
	 * 1c. Prepare SSL configuration
	 */
	mbedtls_printf( "  . Setting up the SSL data...." );

	if( ( ret = mbedtls_ssl_config_defaults( &conf,
	                                         MBEDTLS_SSL_IS_SERVER,
	                                         MBEDTLS_SSL_TRANSPORT_STREAM,
	                                         MBEDTLS_SSL_PRESET_DEFAULT ) ) != 0 )
	{
		mbedtls_printf( " failed: mbedtls_ssl_config_defaults returned -0x%04x\n",
		                -ret );
		goto exit;
	}

	mbedtls_ssl_conf_rng( &conf, mbedtls_ctr_drbg_random, &ctr_drbg );
	mbedtls_ssl_conf_dbg( &conf, my_mutexed_debug, stdout );

	/* mbedtls_ssl_cache_get() and mbedtls_ssl_cache_set() are thread-safe if
	 * MBEDTLS_THREADING_C is set.
	 */
#if defined(MBEDTLS_SSL_CACHE_C)
	mbedtls_ssl_conf_session_cache( &conf, &cache,
	                                mbedtls_ssl_cache_get,
	                                mbedtls_ssl_cache_set );
#endif

	mbedtls_ssl_conf_ca_chain( &conf, &cachain, NULL );
	if( ( ret = mbedtls_ssl_conf_own_cert( &conf, &srvcert, &pkey ) ) != 0 )
	{
		mbedtls_printf( " failed\n  ! mbedtls_ssl_conf_own_cert returned %d\n\n", ret );
		goto exit;
	}

	mbedtls_printf( " ok\n" );

	/*
	 * 2. Setup the listening TCP socket
	 */
	mbedtls_printf( "  . Bind on https://localhost:2000/ ..." );
	fflush( stdout );

	if( ( ret = mbedtls_net_bind( &listen_fd, NULL, "2000", MBEDTLS_NET_PROTO_TCP ) ) != 0 )
	{
		mbedtls_printf( " failed\n  ! mbedtls_net_bind returned %d\n\n", ret );
		goto exit;
	}

	mbedtls_printf( " ok\n" );

	reset:
#ifdef MBEDTLS_ERROR_C
	if( ret != 0 )
	{
		char error_buf[100];
		mbedtls_strerror( ret, error_buf, 100 );
		mbedtls_printf( "  [ main ]  Last error was: -0x%04x - %s\n", -ret, error_buf );
	}
#endif

	/*
	 * 3. Wait until a client connects
	 */
	mbedtls_printf( "  [ main ]  Waiting for a remote connection\n" );

	if( ( ret = mbedtls_net_accept( &listen_fd, &client_fd,
	                                NULL, 0, NULL ) ) != 0 )
	{
		mbedtls_printf( "  [ main ] failed: mbedtls_net_accept returned -0x%04x\n", ret );
		goto exit;
	}

	mbedtls_printf( "  [ main ]  ok\n" );
	mbedtls_printf( "  [ main ]  Creating a new thread\n" );

	if( ( ret = thread_create( &client_fd ) ) != 0 )
	{
		mbedtls_printf( "  [ main ]  failed: thread_create returned %d\n", ret );
		mbedtls_net_free( &client_fd );
		goto reset;
	}

	ret = 0;
	goto reset;

	exit:
	mbedtls_x509_crt_free( &srvcert );
	mbedtls_pk_free( &pkey );
#if defined(MBEDTLS_SSL_CACHE_C)
	mbedtls_ssl_cache_free( &cache );
#endif
	mbedtls_ctr_drbg_free( &ctr_drbg );
	mbedtls_entropy_free( &entropy );
	mbedtls_ssl_config_free( &conf );

	mbedtls_net_free( &listen_fd );

	mbedtls_mutex_free( &debug_mutex );

#if defined(MBEDTLS_MEMORY_BUFFER_ALLOC_C)
	mbedtls_memory_buffer_alloc_free();
#endif

#if defined(_WIN32)
	mbedtls_printf( "  Press Enter to exit this program.\n" );
	fflush( stdout ); getchar();
#endif

	mbedtls_exit( ret );
}