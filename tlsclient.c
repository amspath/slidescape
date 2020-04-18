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
#include "tlse.c"

#include "tlsclient.h"
#include "platform.h"

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
            send_res += sendto(client_sock, out_buffer, out_buffer_len, 0, (struct sockaddr *)clientaddr, socket_len);
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

struct TLSContext* tls_context;

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



		is_tls_initialized = true;
	}

}

void open_remote_slide(const char* hostname, i32 portno, const char* request_get) {

	i64 start = get_clock();
	i64 sockfd = socket(AF_INET, SOCK_STREAM, 0);
	u32 optval = 1;
	if (sockfd < 0) {
		printf("ERROR opening socket\n");
		return;
	}
	setsockopt(sockfd, SOL_SOCKET, SO_KEEPALIVE, (char*)&optval, sizeof(optval));
	struct hostent* server = gethostbyname(hostname);
	if (server == NULL) {
		printf("ERROR, no such host\n");
		closesocket(sockfd);
		sockfd = 0;
		return;
	}
	struct sockaddr_in serv_addr = { .sin_family = AF_INET };
	memcpy((char *)&serv_addr.sin_addr.s_addr, (char *)server->h_addr, server->h_length);
	serv_addr.sin_port = htons(portno);
	if (connect(sockfd,(struct sockaddr *)&serv_addr,sizeof(serv_addr)) < 0) {
		printf("ERROR connecting\n");
		closesocket(sockfd);
		sockfd = 0;
		return;
	}
	tls_context = tls_create_context(0, TLS_V13);
	// the next line is needed only if you want to serialize the connection context or kTLS is used
	tls_make_exportable(tls_context, 1);
	tls_client_connect(tls_context);
//	printf("TLS boilerplate is done in %g seconds\n", get_seconds_elapsed(start, get_clock()));
	send_pending(sockfd, tls_context);
	u8 client_message[0xFFFF];
	i32 read_size;
	i32 sent = 0;
	while ((read_size = recv(sockfd, (char*)client_message, sizeof(client_message) , 0)) > 0) {
		tls_consume_stream(tls_context, client_message, read_size, validate_certificate);
		send_pending(sockfd, tls_context);
		if (tls_established(tls_context)) {
			if (!sent) {
				static const char requestfmt[] = "GET /%s HTTP/1.1\r\nConnection: close\r\n\r\n\0";
				char request[4096];
				snprintf(request, sizeof(request), requestfmt, request_get);
				size_t request_len = COUNT(requestfmt) + strlen(request);

				// try kTLS (kernel TLS implementation in linux >= 4.13)
				// note that you can use send on a ktls socket
				// recv must be handled by TLSe
				if (!tls_make_ktls(tls_context, sockfd)) {
					// call send as on regular TCP sockets
					// TLS record layer is handled by the kernel
					send(sockfd, request, strlen(request), 0);
				} else {
					tls_write(tls_context, (unsigned char *)request, strlen(request));
					send_pending(sockfd, tls_context);
				}
				sent = 1;
			}

			unsigned char read_buffer[0xFFFF];
			int read_size2 = tls_read(tls_context, read_buffer, 0xFFFF - 1);
			if (read_size2 > 0) {
				fwrite(read_buffer, read_size2, 1, stdout);
			}
		}
	}

	tls_destroy_context(tls_context);
	closesocket(sockfd);

	float seconds_elapsed = get_seconds_elapsed(start, get_clock());
	printf("Open remote took %g seconds\n", seconds_elapsed);
}
