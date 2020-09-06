/*
  Slideviewer, a whole-slide image viewer for digital pathology.
  Copyright (C) 2019-2020  Pieter Valkema

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

#include "common.h"
#include "platform.h"
#undef MIN
#undef MAX // redefined by tlse.c

#include <stdio.h>
#include <string.h>    //strlen
#include <sys/stat.h>
#include <pthread.h>

#include <time.h>
#include <errno.h>

#ifdef _WIN32
#include <winsock2.h>
#define socklen_t int
#else
#include <sys/socket.h>
#include <arpa/inet.h>
#endif

#define LTM_DESC
#define TLS_AMALGAMATION
#define LTC_NO_ASM
#include "tlse.c"

#include "tiff.h"
#include "stringutils.h"

#define THREAD_COUNT 16
#define SERVER_VERBOSE 1

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

bool32 load_keys(struct TLSContext *context, char *fname, char *priv_fname) {
	unsigned char buf[0xFFFF];
	unsigned char buf2[0xFFFF];
	int size = read_from_file(fname, buf, 0xFFFF);
	bool32 certificate_loaded = false;
	bool32 private_key_loaded = false;
	int size2 = read_from_file(priv_fname, buf2, 0xFFFF);
	int ret = 0;
	if (size > 0) {
		if (context) {
			ret = tls_load_certificates(context, buf, size);
			certificate_loaded = (ret > 0);
			ret = tls_load_private_key(context, buf2, size2);
			private_key_loaded = (ret > 0);
			// tls_print_certificate(fname);
		}
	}
	if (!certificate_loaded) {
		fprintf(stderr, "Could not load certificate: %s\n", fname);
	}
	if (!private_key_loaded) {
		fprintf(stderr, "Could not load private key: %s\n", priv_fname);
	}

	return (certificate_loaded && private_key_loaded);
}

/*
// Use this version with DTLS (preserving message boundary)
int send_pending_udp(int client_sock, struct TLSContext *context, struct sockaddr_in *clientaddr, socklen_t socket_len) {
    unsigned int out_buffer_len = 0;
    unsigned int offset = 0;
    int send_res = 0;
    const unsigned char *out_buffer;
    do {
        out_buffer = tls_get_message(context, &out_buffer_len, offset);
        if (out_buffer) {
            send_res += sendto(client_sock, out_buffer, out_buffer_len, 0, (struct sockaddr *)clientaddr, socket_len);
            offset += out_buffer_len;
        }
    } while (out_buffer);
    tls_buffer_clear(context);
    return send_res;
}
*/

int send_pending(int client_sock, struct TLSContext *context) {
	unsigned int out_buffer_len = 0;
	const unsigned char *out_buffer = tls_get_write_buffer(context, &out_buffer_len);
	unsigned int out_buffer_index = 0;
	int send_res = 0;
	while ((out_buffer) && (out_buffer_len > 0)) {
		int res = send(client_sock, (char *)&out_buffer[out_buffer_index], out_buffer_len, 0);
		if (res <= 0) {
			send_res = res;
			break;
		}
		out_buffer_len -= res;
		out_buffer_index += res;
	}
	tls_buffer_clear(context);
	return send_res;
}

// verify signature
int verify_signature(struct TLSContext *context, struct TLSCertificate **certificate_chain, int len) {
	if (len) {
		struct TLSCertificate *cert = certificate_chain[0];
		if (cert) {
			snprintf(identity_str, sizeof(identity_str), "%s, %s(%s) (issued by: %s)", cert->subject, cert->entity, cert->location, cert->issuer_entity);
			fprintf(stderr, "Verified: %s\n", identity_str);
		}
	}
	return no_error;
}

char** split_into_lines(char* buffer, i64* num_lines) {
	size_t lines_counted = 0;
	size_t capacity = 0;
	char** lines = NULL;
	bool32 newline = true;
	char* pos = buffer;
	int c;
	do {
		c = *pos;
		if (c == '\n' || c == '\r') {
			*pos = '\0';
			newline = true;
		} else if (newline || c == '\0') {
			size_t line_index = lines_counted++;
			if (lines_counted > capacity) {
				capacity = MAX(capacity, 8) * 2;
				lines = (char**) realloc(lines, capacity * sizeof(char*));
			}
			lines[line_index] = pos;
			newline = false;
		}
		++pos;
	} while (c != '\0');
	if (num_lines) *num_lines = lines_counted;
	return lines;
}

void strip_character(char* s, char character_to_strip) {
	if (!s) return;
	char c;
	while ((c = *s)) {
		if (c == character_to_strip) *s = '\0';
		++s;
	}
}

char* find_next_token(char* s, char separator) {
	if (!s) return NULL;
	char c;
	while ((c = *s++)) {
		if (c == separator) return s;
	}
	return NULL;
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
	i64 num_lines = 0;
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

bool32 send_buffer_to_client(struct TLSContext* context, int client_sock, u8* send_buffer, u64 send_size) {
	u8* send_buffer_pos = send_buffer;
	u32 send_size_remaining = (u32)send_size;

	bool32 sent = false;
	i32 bytes_written = 0;
	i32 total_bytes_written = 0;
	while (!sent) {
		bytes_written = tls_write(context, send_buffer_pos, send_size_remaining);
		total_bytes_written += bytes_written;
		send_buffer_pos += bytes_written;
		send_size_remaining -= bytes_written;
		if (total_bytes_written >= send_size) {
			sent = true;
		} else {
			send_pending(client_sock, context);
		}
	}
	return sent;
}

bool32 execute_slide_set_api_call(struct TLSContext *context, int client_sock, slide_api_call_t *call) {
	bool32 success = false;

	char path_buffer[2048];
	path_buffer[0] = '\0';
	locate_file_prepend_env(call->filename, "SLIDES_DIR", path_buffer, sizeof(path_buffer));
	mem_t* file_mem = platform_read_entire_file(path_buffer);
	if (file_mem) {
		success = send_buffer_to_client(context, client_sock, file_mem->data, file_mem->len);
	}

	return success;
}

bool32 execute_slide_api_call(struct TLSContext *context, int client_sock, slide_api_call_t *call) {
	if (!call || !call->command) return false;
	bool32 success = false;

	if (strcmp(call->command, "slide_set") == 0) {
		success = execute_slide_set_api_call(context, client_sock, call);
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
					push_buffer_t buffer = {0};
					tiff_serialize(&tiff, &buffer);
					u64 send_size = ((u64)buffer.data - (u64)buffer.raw_memory) + buffer.used_size;
					u8* send_buffer = buffer.raw_memory;
					success = send_buffer_to_client(context, client_sock, send_buffer, send_size);

//				    tls_close_notify(context);
//				    send_pending(client_sock, context);
					tiff_destroy(&tiff);
					free(buffer.raw_memory);
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

							u8* send_buffer_pos = send_buffer;
							u64 send_size_remaining = send_size;

							bool32 sent = false;
							i32 bytes_written = 0;
							i32 total_bytes_written = 0;
							while (!sent) {
								bytes_written = tls_write(context, send_buffer_pos, send_size_remaining);
								total_bytes_written += bytes_written;
								send_buffer_pos += bytes_written;
								send_size_remaining -= bytes_written;
								if (total_bytes_written >= send_size) {
									sent = true;
									success = true;
								} else {
									send_pending(client_sock, context);
								}
							}
//									tls_close_notify(context);
//									send_pending(client_sock, context);

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

struct TLSContext *server_context;


void *connection_handler(void *socket_desc) {
	//Get the socket descriptor
	int client_sock = *(int*)socket_desc;
	int ret;

	char client_message[0xFFFF];


	struct TLSContext *context = tls_accept(server_context);
	if (!context) {
		fprintf(stderr, "[socket %d] tls_accept() failed\n", client_sock);
		goto cleanup;
	}

	// uncomment next line to request client certificate
//        tls_request_client_certificate(context);

	// make the TLS context serializable (this must be called before negotiation)
//		tls_make_exportable(context, 1);

#if SERVER_VERBOSE
	fprintf(stderr, "[socket %d] Client connected\n", client_sock);
#endif

	int read_size;
	for (;;) {
		read_size = recv(client_sock, client_message, sizeof(client_message), 0);
		if (read_size < 0) {
			fprintf(stderr, "[socket %d] recv(1) returned %d\n", client_sock, read_size);
			perror("recv failed");
			goto cleanup;
		} else if (read_size == 0) {
#if SERVER_VERBOSE
			fprintf(stderr, "[socket %d] Gracefully closed\n", client_sock);
#endif
			break;
		} else {
			ret = tls_consume_stream(context, (u8*) client_message, read_size, verify_signature);
			if (ret < 0) {
				fprintf(stderr, "[socket %d] Error in stream consume\n", client_sock);
				goto cleanup;
			} else if (ret == 0) {
				fprintf(stderr, "[socket %d] stream consume returned 0\n", client_sock);
				continue;
			} else {
				break; // done
			}
		}

	}


	send_pending(client_sock, context);

	if (read_size > 0) {
#if SERVER_VERBOSE
		fprintf(stderr, "USED CIPHER: %s\n", tls_cipher_name(context));
#endif
		int ref_packet_count = 0;
		int res;
		for (;;) {
			read_size = recv(client_sock, client_message, sizeof(client_message) , 0);
			if (read_size < 0) {
				fprintf(stderr, "[socket %d] recv (2) returned %d\n", client_sock, read_size);
				perror("recv failed");
				goto cleanup;
			} else if (read_size == 0) {
				fprintf(stderr, "[socket %d] Gracefully closed\n", client_sock);
				break;
			}
			if (tls_consume_stream(context, (u8*) client_message, read_size, verify_signature) < 0) {
				fprintf(stderr, "[socket %d] Error in stream consume\n", client_sock);
				goto cleanup;
			}
			send_pending(client_sock, context);
			if (tls_established(context) == 1) {
				unsigned char read_buffer[0xFFFF];
				int read_size = tls_read(context, read_buffer, sizeof(read_buffer) - 1);
				if (read_size > 0) {
					read_buffer[read_size] = 0;
					unsigned char export_buffer[0xFFF];
					// simulate serialization / deserialization to another process
					char sni[0xFF];
					sni[0] = 0;
					if (context->sni)
						snprintf(sni, 0xFF, "%s", context->sni);

					// note: the export context stuff does not seem to work on the server??
#if 0
					/* COOL STUFF => */ int size = tls_export_context(context, export_buffer, sizeof(export_buffer), 1);
                        if (size > 0) {
    /* COOLER STUFF => */   struct TLSContext *imported_context = tls_import_context(export_buffer, size);
    // This is cool because a context can be sent to an existing process.
    // It will work both with fork and with already existing worker process.
                            fprintf(stderr, "Imported context (size: %i): %x\n", size, imported_context);
                            if (imported_context) {
                                // destroy old context
                                tls_destroy_context(context);
                                // simulate serialization/deserialization of context
                                context = imported_context;
                            }
                        }
#endif
					// interpret the request
					http_request_t* request = parse_http_headers((char *) read_buffer, read_size);
					if (!request) {
						fprintf(stderr, "[socket %d] Warning: bad request\n", client_sock);
					} else {
						fprintf(stderr, "[socket %d] Received request: %s\n", client_sock, request->uri);
						slide_api_call_t* call = interpret_api_request(request);
						if (call) {
							if (execute_slide_api_call(context, client_sock, call)) {
								// success!
							}
						}
					}

					tls_close_notify(context);
					send_pending(client_sock, context);

					break;
				}
			}
		}
	}

	cleanup:
#ifdef __WIN32
	shutdown(client_sock, SD_BOTH);
	closesocket(client_sock);
#else
	shutdown(client_sock, SHUT_RDWR);
        close(client_sock);
#endif
	tls_destroy_context(context);

	return 0;
}


void* worker(void* arg_ptr) {

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

int main(int argc , char *argv[]) {
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

	pthread_t threads[THREAD_COUNT] = {0};

	pthread_cond_init(&semaphore_work_available, NULL);
	pthread_mutex_init(&work_mutex, NULL);

	pthread_mutex_lock(&work_mutex);

	for (i64 i = 1; i < COUNT(threads); ++i) {
		if (pthread_create(threads + i, NULL, &worker, (void*)i) != 0) {
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
