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

#include "common.h"
#undef MIN
#undef MAX // these are redefined by TLSe

#include <stdio.h>
#include <sys/types.h>
#ifdef _WIN32
    #include <winsock2.h>
    #define socklen_t int
    #define sleep(x)    Sleep(x*1000)
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <netdb.h> 
#endif
#define LTM_DESC
#define TLS_AMALGAMATION
#define LTC_NO_ASM
#include "tlse.c"

#include "tlsclient.h"
#include "platform.h"
#include "tiff.h"
#include "openslide_api.h" // TODO: remove/refactor, needed because of viewer.h
#include "viewer.h"

void error(char *msg) {
    perror(msg);
    exit(0);
}


// Use this version with DTLS (preserving message boundary)
int send_pending_udp(int client_sock, struct TLSContext *context, struct sockaddr_in *clientaddr, socklen_t socket_len) {
    unsigned int out_buffer_len = 0;
    unsigned int offset = 0;
    int send_res = 0;
    const unsigned char *out_buffer;
    do {
        out_buffer = tls_get_message(context, &out_buffer_len, offset);
        if (out_buffer) {
            send_res += sendto(client_sock, (const char*)out_buffer, out_buffer_len, 0, (struct sockaddr *)clientaddr, socket_len);
            offset += out_buffer_len;
        }
    } while (out_buffer);
    tls_buffer_clear(context);
    return send_res;
}


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

int validate_certificate(struct TLSContext *context, struct TLSCertificate **certificate_chain, int len) {
    int i;
    if (certificate_chain) {
        for (i = 0; i < len; i++) {
            struct TLSCertificate *certificate = certificate_chain[i];
            // check certificate ...
        }
    }
    //return certificate_expired;
    //return certificate_revoked;
    //return certificate_unknown;
    return no_error;
}


void init_networking() {
	static bool is_tls_initialized;
	if (!is_tls_initialized) {
#ifdef _WIN32
		WSADATA wsa_data = {};
		int err = WSAStartup(MAKEWORD(2, 2), &wsa_data);
		if (err != 0) {
			printf("WSAStartup failed with error: %d\n", err);
			return;
		}
		if (LOBYTE(wsa_data.wVersion) != 2 || HIBYTE(wsa_data.wVersion) != 2) {
			printf("Warning: requested Winsock.dll version 2.2 but found %d.%d\n",
					LOBYTE(wsa_data.wVersion), HIBYTE(wsa_data.wVersion));
		}
#else
		signal(SIGPIPE, SIG_IGN);
#endif

		tls_init();


		is_tls_initialized = true;
	}

}

// TODO: reduce stdout spam messages
u8 *do_http_request(const char *hostname, i32 portno, const char *uri, i32 *bytes_read, i32 thread_id) {
	i64 start = get_clock();
	i64 sockfd = socket(AF_INET, SOCK_STREAM, 0);

	if (sockfd < 0) {
		printf("[thread %d] ERROR opening socket\n", thread_id);
		return NULL;
	}
	u32 timeout_ms = 5000;
	setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, (char*)&timeout_ms, sizeof(timeout_ms));
	setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout_ms, sizeof(timeout_ms));
//	u32 optval = 1;
//	setsockopt(sockfd, SOL_SOCKET, SO_KEEPALIVE, (char*)&optval, sizeof(optval));
	struct hostent* server = gethostbyname(hostname);
	if (server == NULL) {
		printf("[thread %d] ERROR, no such host\n", thread_id);
		closesocket(sockfd);
		sockfd = 0;
		return NULL;
	}
	struct sockaddr_in serv_addr = { .sin_family = AF_INET };
	memcpy((char *)&serv_addr.sin_addr.s_addr, (char *)server->h_addr, server->h_length);
	serv_addr.sin_port = htons(portno);
	if (connect(sockfd,(struct sockaddr *)&serv_addr,sizeof(serv_addr)) < 0) {
		printf("[thread %d] ERROR connecting\n", thread_id);
		closesocket(sockfd);
		sockfd = 0;
		return NULL;
	}
	struct TLSContext* tls_context = tls_create_context(0, TLS_V13);
	// the next line is needed only if you want to serialize the connection context or kTLS is used
//	tls_make_exportable(tls_context, 1);
	int ret = tls_client_connect(tls_context);
	if (ret < 0) {
		printf("[thread %d] tls_client_connect() failed with error %d\n", thread_id, ret);
	}
//	printf("TLS boilerplate is done in %g seconds\n", get_seconds_elapsed(start, get_clock()));
	ret = send_pending(sockfd, tls_context);
	if (ret < 0) {
		printf("[thread %d] send_pending() returned error %d\n", thread_id, ret);
	}

	u8 receive_buffer[0xFFFF]; // receive in 64K byte chunks
	i32 receive_size;
	i32 sent = 0;

	i32 read_buffer_size = MEGABYTES(2); // TODO: reallocate if necessary
	u8* read_buffer = calloc(read_buffer_size, 1);
	u8* read_buffer_pos = read_buffer;
	i32 total_bytes_read = 0;

	for (;;) {
		receive_size = recv(sockfd, (char*)receive_buffer, sizeof(receive_buffer), 0);
		if (receive_size < 0) {
			int error_id = WSAGetLastError();
			char* prefix = "do_http_request";
			char* message_buffer;
			/*size_t size = */FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
			                                 NULL, error_id, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&message_buffer, 0, NULL);
			printf("[thread %d] %s: (error code 0x%x) %s\n", thread_id, prefix, (u32)error_id, message_buffer);
			LocalFree(message_buffer);
			break;
		} else if (receive_size == 0) {
#if REMOTE_CLIENT_VERBOSE
			printf("[thread %d] Gracefully closed\n", thread_id);
#endif
			break;
		}
		ret = tls_consume_stream(tls_context, receive_buffer, receive_size, validate_certificate);
		if (ret < 0) {
#if REMOTE_CLIENT_VERBOSE
			printf("[thread %d] tls_consume_stream() returned error %d\n", thread_id, ret);
#endif
		}
		send_pending(sockfd, tls_context);
		if (tls_established(tls_context)) {
			if (!sent) {
				static const char requestfmt[] = "GET %s HTTP/1.1\r\nConnection: close\r\n\r\n";
				char request[4096];
				snprintf(request, sizeof(request), requestfmt, uri);
				size_t request_len = strlen(request);

				// try kTLS (kernel TLS implementation in linux >= 4.13)
				// note that you can use send on a ktls socket
				// recv must be handled by TLSe
				if (!tls_make_ktls(tls_context, sockfd)) {
					// call send as on regular TCP sockets
					// TLS record layer is handled by the kernel
					send(sockfd, request, request_len, 0);
				} else {
					tls_write(tls_context, (unsigned char *)request, request_len);
					send_pending(sockfd, tls_context);
				}
				sent = 1;
			}

			// TODO: use realloc to resize read buffer if needed?
			i32 read_size = tls_read(tls_context, read_buffer_pos, read_buffer_size - total_bytes_read - 1);
			if (read_size > 0) {
//				fwrite(read_buffer, read_size, 1, stdout);
				read_buffer_pos += read_size;
				total_bytes_read += read_size;
			}

		}
	}
	*bytes_read = total_bytes_read;

	// now we should have the whole HTTP response
#if REMOTE_CLIENT_VERBOSE
	printf("[thread %d] HTTP read finished, length = %d\n", thread_id, total_bytes_read);
#endif
//	fwrite(read_buffer, total_bytes_read, 1, stdout);


	tls_destroy_context(tls_context);
	closesocket(sockfd);

	float seconds_elapsed = get_seconds_elapsed(start, get_clock());
#if REMOTE_CLIENT_VERBOSE
	printf("[thread %d] Open remote took %g seconds\n", thread_id, seconds_elapsed);
#endif

	return read_buffer;
}


u8 *download_remote_chunk(const char *hostname, i32 portno, const char *filename, i64 chunk_offset, i64 chunk_size,
                          i32 *bytes_read, i32 thread_id) {

	char uri[2048] = {0};
	snprintf(uri, sizeof(uri), "/slide/%s/%lld/%lld", filename, chunk_offset, chunk_size);
	u8* read_buffer = do_http_request(hostname, portno, uri, bytes_read, thread_id);
	return read_buffer;

}

u8 *download_remote_batch(const char *hostname, i32 portno, const char *filename, i64 *chunk_offsets, i64 *chunk_sizes,
                          i32 batch_size, i32 *bytes_read, i32 thread_id) {
	ASSERT(batch_size > 0);
	char uri[4092] = {0};
	char* pos = uri;
	i32 bytes_printed = snprintf(uri, sizeof(uri), "/slide/%s", filename);
	pos += bytes_printed;
	i32 bytes_left = sizeof(uri) - bytes_printed;
	for (i32 i = 0; i < batch_size; ++i) {
		if (bytes_left <= 0) {
			ASSERT(!"uri became too long");
			return NULL;
		}
		bytes_printed += snprintf(pos, bytes_left, "/%lld/%lld", chunk_offsets[i], chunk_sizes[i]);
		pos = uri + bytes_printed;
		bytes_left = sizeof(uri) - bytes_printed;
	}
	u8* read_buffer = do_http_request(hostname, portno, uri, bytes_read, thread_id);
	return read_buffer;
}

typedef struct {
	i64 start_clock;
	i64 sockfd;
	struct TLSContext* tls_context;
} tls_connection_t;

tls_connection_t* open_remote_connection(const char* hostname, i32 portno, void* alloced_mem_for_struct) {
	tls_connection_t* connection = (tls_connection_t*) alloced_mem_for_struct;
	if (connection) {
		memset(connection, 0, sizeof(*connection));
		connection->start_clock = get_clock();
		connection->sockfd = socket(AF_INET, SOCK_STREAM, 0);
		if (connection->sockfd < 0) {
			printf("ERROR opening socket\n");
			return NULL;
		}
		// Set timeout interval
		u32 timeout_ms = 2000;
		setsockopt(connection->sockfd, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout_ms, sizeof(timeout_ms));
		setsockopt(connection->sockfd, SOL_SOCKET, SO_SNDTIMEO, (char*)&timeout_ms, sizeof(timeout_ms));
		struct hostent* server = gethostbyname(hostname);
		if (server == NULL) {
			printf("ERROR, no such host\n");
			closesocket(connection->sockfd);
			connection->sockfd = 0;
			return NULL;
		}
		struct sockaddr_in serv_addr = { .sin_family = AF_INET };
		memcpy((char *)&serv_addr.sin_addr.s_addr, (char *)server->h_addr, server->h_length);
		serv_addr.sin_port = htons((u16)portno);
		if (connect(connection->sockfd,(struct sockaddr *)&serv_addr,sizeof(serv_addr)) < 0) {
			printf("Error: couldn't connect to %s:%d\n", hostname, portno);
			closesocket(connection->sockfd);
			connection->sockfd = 0;
			return NULL;
		}
		connection->tls_context = tls_create_context(0, TLS_V13);
		if (!connection->tls_context) {
			printf("Error: couldn't create TLS context\n");
		}
		// the next line is needed only if you want to serialize the connection context or kTLS is used
//	tls_make_exportable(tls_context, 1);
		tls_client_connect(connection->tls_context);
//	printf("TLS boilerplate is done in %g seconds\n", get_seconds_elapsed(start, get_clock()));
		send_pending(connection->sockfd, connection->tls_context);
	}

	return connection;
}

float close_remote_connection(tls_connection_t* connection) {
	tls_destroy_context(connection->tls_context);
	closesocket(connection->sockfd);

	float seconds_elapsed = get_seconds_elapsed(connection->start_clock, get_clock());
	return seconds_elapsed;
}

bool32 remote_request(tls_connection_t* connection, const char* request, i32 request_len, mem_t* mem_buffer) {
	// TODO: refactor
	u8 receive_buffer[0xFFFF]; // receive in 64K byte chunks
	i32 receive_size;
	i32 sent = 0;

	u8* read_buffer_pos = mem_buffer->data;
	i32 total_bytes_read = 0;

	while ((receive_size = recv(connection->sockfd, (char*)receive_buffer, sizeof(receive_buffer), 0)) > 0) {
		tls_consume_stream(connection->tls_context, receive_buffer, receive_size, validate_certificate);
		send_pending(connection->sockfd, connection->tls_context);
		if (tls_established(connection->tls_context)) {
			if (!sent) {

				// try kTLS (kernel TLS implementation in linux >= 4.13)
				// note that you can use send on a ktls socket
				// recv must be handled by TLSe
				if (!tls_make_ktls(connection->tls_context, connection->sockfd)) {
					// call send as on regular TCP sockets
					// TLS record layer is handled by the kernel
					send(connection->sockfd, request, request_len, 0);
				} else {
					tls_write(connection->tls_context, (unsigned char *)request, request_len);
					send_pending(connection->sockfd, connection->tls_context);
				}
				sent = 1;
			}

			// TODO: use realloc to resize read buffer if needed?
			i32 read_size = tls_read(connection->tls_context, read_buffer_pos, mem_buffer->capacity - total_bytes_read - 1);
			if (read_size > 0) {
//				fwrite(read_buffer, read_size, 1, stdout);
				read_buffer_pos += read_size;
				total_bytes_read += read_size;
			}

		}
	}

	mem_buffer->len = (size_t)total_bytes_read;
	return (sent && total_bytes_read > 0);
}

mem_t* download_remote_caselist(const char *hostname, i32 portno, const char *filename) {
	mem_t* result = NULL;

	tls_connection_t* connection = open_remote_connection(hostname, portno, alloca(sizeof(tls_connection_t)));
	if (connection) {

		static const char requestfmt[] = "GET /slide_set/%s HTTP/1.1\r\nConnection: close\r\n\r\n";
		char request[4096];
		snprintf(request, sizeof(request), requestfmt, filename);
		i32 request_len = (i32)strlen(request);

		mem_t* mem_buffer = platform_allocate_mem_buffer(MEGABYTES(2));
		bool32 read_ok = remote_request(connection, request, request_len, mem_buffer);
		float seconds_elapsed = close_remote_connection(connection);
		if (read_ok) {
			result = mem_buffer; // don't free (ownership passes to caller)
			printf("Downloaded case list '%s' in %g seconds.\n", filename, seconds_elapsed);
		} else {
			free(mem_buffer);
		}
	}

	return result;
}

bool32 open_remote_slide(app_state_t *app_state, const char *hostname, i32 portno, const char *filename) {

	bool32 success = false;

	static const char requestfmt[] = "GET /slide/%s/header HTTP/1.1\r\nConnection: close\r\n\r\n";
	char request[4096];
	snprintf(request, sizeof(request), requestfmt, filename);
	i32 request_len = (i32)strlen(request);

	tls_connection_t* connection = open_remote_connection(hostname, portno, alloca(sizeof(tls_connection_t)));
	if (!connection) {
		return false;
	}

	mem_t* mem_buffer = platform_allocate_mem_buffer(MEGABYTES(2));
	bool32 read_ok = remote_request(connection, request, request_len, mem_buffer);
	float seconds_elapsed = close_remote_connection(connection);

	if (read_ok) {
		// now we should have the whole HTTP response
#if REMOTE_CLIENT_VERBOSE
		printf("HTTP read finished, length = %d\n", mem_buffer->len);
#endif
//	fwrite(read_buffer, total_bytes_read, 1, stdout);

		tiff_t tiff = {0};
		if (tiff_deserialize(&tiff, mem_buffer->data, mem_buffer->len)) {
			tiff.is_remote = true;
			tiff.location = (network_location_t){ .hostname = hostname, .portno = portno, .filename = filename };

			unload_all_images(app_state);
			add_image_from_tiff(app_state, tiff);
			success = true;
		} else {
			tiff_destroy(&tiff);
		}

	}
	free(mem_buffer);


//	float seconds_elapsed = close_remote_connection(connection);
	printf("Open remote took %g seconds\n", seconds_elapsed);
	return success;
}

bool32 get_remote_directory_listing() {
	return false;
}

