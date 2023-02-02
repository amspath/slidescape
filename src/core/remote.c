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

#include "common.h"
#include "platform.h"
#include "viewer.h"

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
#define mbedtls_time            time
#define mbedtls_time_t          time_t
#define mbedtls_fprintf         fprintf
#define mbedtls_printf          printf
#define mbedtls_exit            exit
#define MBEDTLS_EXIT_SUCCESS    EXIT_SUCCESS
#define MBEDTLS_EXIT_FAILURE    EXIT_FAILURE
#endif /* MBEDTLS_PLATFORM_C */

#include "mbedtls/net_sockets.h"
#include "mbedtls/debug.h"
#include "mbedtls/ssl.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/error.h"
#include "mbedtls/certs.h"

//#define SERVER_PORT "443"
//#define SERVER_PORT "4433"
//#define SERVER_NAME "google.com"
//#define SERVER_NAME "localhost"
//#define GET_REQUEST "GET / HTTP/1.0\r\n\r\n"

#define DEBUG_LEVEL DO_DEBUG

typedef struct {
	i64 start_clock;
	i64 sockfd;
	mbedtls_net_context server_fd;
	mbedtls_entropy_context entropy;
	mbedtls_ctr_drbg_context ctr_drbg;
	mbedtls_ssl_context ssl;
	mbedtls_ssl_config conf;
	mbedtls_x509_crt cacert;
} tls_connection_t;

static void my_debug( void *ctx, int level,
                      const char *file, int line,
                      const char *str )
{
	((void) level);

	mbedtls_fprintf( (FILE *) ctx, "%s:%04d: %s", file, line, str );
	fflush(  (FILE *) ctx  );
}

tls_connection_t* open_remote_connection(const char* hostname, i32 portno, void* alloced_mem_for_struct) {
	int ret = 1, len;
	int exit_code = MBEDTLS_EXIT_FAILURE;
	uint32_t flags;
	unsigned char buf[1024];
	const char *pers = "ssl_client1";
	tls_connection_t* connection = (tls_connection_t*) alloced_mem_for_struct;
	connection->start_clock = get_clock();

#if defined(MBEDTLS_DEBUG_C)
	mbedtls_debug_set_threshold( DEBUG_LEVEL );
#endif

	/*
	 * 0. Initialize the RNG and the session data
	 */
	mbedtls_net_init( &connection->server_fd );
	mbedtls_ssl_init( &connection->ssl );
	mbedtls_ssl_config_init( &connection->conf );
	mbedtls_x509_crt_init( &connection->cacert );
	mbedtls_ctr_drbg_init( &connection->ctr_drbg );

	console_print_verbose( "\n  . Seeding the random number generator..." );
	fflush( stdout );

	mbedtls_entropy_init( &connection->entropy );
	if( ( ret = mbedtls_ctr_drbg_seed( &connection->ctr_drbg, mbedtls_entropy_func, &connection->entropy,
	                                   (const unsigned char *) pers,
	                                   strlen( pers ) ) ) != 0 )
	{
		console_print_verbose( " failed\n  ! mbedtls_ctr_drbg_seed returned %d\n", ret );
		goto exit;
	}

	// TODO: console append to last entry?
	console_print_verbose( " ok\n" );

	/*
	 * 0. Initialize certificates
	 */
	console_print_verbose( "  . Loading the CA root certificate ..." );

	ret = mbedtls_x509_crt_parse( &connection->cacert, (const unsigned char *) mbedtls_test_cas_pem,
	                              mbedtls_test_cas_pem_len );
	if( ret < 0 )
	{
		console_print_verbose( " failed\n  !  mbedtls_x509_crt_parse returned -0x%x\n\n", (unsigned int) -ret );
		goto exit;
	}

	console_print_verbose( " ok (%d skipped)\n", ret );

	/*
     * 1. Start the connection
     */
	console_print_verbose( "  . Connecting to tcp/%s/%d...", hostname, portno );

	char server_port_string[32];
	snprintf(server_port_string, sizeof(server_port_string), "%d", portno);

	if( ( ret = mbedtls_net_connect( &connection->server_fd, hostname,
	                                 server_port_string, MBEDTLS_NET_PROTO_TCP ) ) != 0 )
	{
		console_print_error( " failed\n  ! mbedtls_net_connect returned %d\n\n", ret );
		goto exit;
	}

	console_print_verbose( " ok\n" );

	/*
	 * 2. Setup stuff
	 */
	console_print_verbose( "  . Setting up the SSL/TLS structure..." );

	if( ( ret = mbedtls_ssl_config_defaults( &connection->conf,
	                                         MBEDTLS_SSL_IS_CLIENT,
	                                         MBEDTLS_SSL_TRANSPORT_STREAM,
	                                         MBEDTLS_SSL_PRESET_DEFAULT ) ) != 0 )
	{
		console_print_error( " failed\n  ! mbedtls_ssl_config_defaults returned %d\n\n", ret );
		goto exit;
	}

	console_print_verbose( " ok\n" );

	/* OPTIONAL is not optimal for security,
	 * but makes interop easier in this simplified example */
	mbedtls_ssl_conf_authmode( &connection->conf, MBEDTLS_SSL_VERIFY_OPTIONAL );
	mbedtls_ssl_conf_ca_chain( &connection->conf, &connection->cacert, NULL );
	mbedtls_ssl_conf_rng( &connection->conf, mbedtls_ctr_drbg_random, &connection->ctr_drbg );
	mbedtls_ssl_conf_dbg( &connection->conf, my_debug, stdout );

	if( ( ret = mbedtls_ssl_setup( &connection->ssl, &connection->conf ) ) != 0 )
	{
		console_print_error( " failed\n  ! mbedtls_ssl_setup returned %d\n\n", ret );
		goto exit;
	}

	if( ( ret = mbedtls_ssl_set_hostname( &connection->ssl, hostname ) ) != 0 )
	{
		console_print_error( " failed\n  ! mbedtls_ssl_set_hostname returned %d\n\n", ret );
		goto exit;
	}

	mbedtls_ssl_set_bio( &connection->ssl, &connection->server_fd, mbedtls_net_send, mbedtls_net_recv, NULL );

	/*
	 * 4. Handshake
	 */
	console_print_verbose( "  . Performing the SSL/TLS handshake..." );

	while( ( ret = mbedtls_ssl_handshake( &connection->ssl ) ) != 0 )
	{
		if( ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE )
		{
			console_print_error( " failed\n  ! mbedtls_ssl_handshake returned -0x%x\n\n", (unsigned int) -ret );
			goto exit;
		}
	}

	console_print_verbose( " ok\n" );

	/*
	 * 5. Verify the server certificate
	 */
	console_print_verbose( "  . Verifying peer X.509 certificate..." );

	/* In real life, we probably want to bail out when ret != 0 */
	if( ( flags = mbedtls_ssl_get_verify_result( &connection->ssl ) ) != 0 )
	{
		char vrfy_buf[512];

		console_print_verbose( " failed\n" );

		mbedtls_x509_crt_verify_info( vrfy_buf, sizeof( vrfy_buf ), "  ! ", flags );

		console_print_verbose( "%s\n", vrfy_buf );
	}
	else
		console_print_verbose( " ok\n" );


//	mbedtls_ssl_close_notify( &connection->ssl );

	exit_code = MBEDTLS_EXIT_SUCCESS;

	exit:

#ifdef MBEDTLS_ERROR_C
	if( exit_code != MBEDTLS_EXIT_SUCCESS )
	{
		char error_buf[100];
		mbedtls_strerror( ret, error_buf, 100 );
		console_print_error("Last error was: %d - %s\n\n", ret, error_buf );
		mbedtls_net_free( &connection->server_fd );

		mbedtls_x509_crt_free( &connection->cacert );
		mbedtls_ssl_free( &connection->ssl );
		mbedtls_ssl_config_free( &connection->conf );
		mbedtls_ctr_drbg_free( &connection->ctr_drbg );
		mbedtls_entropy_free( &connection->entropy );

		return NULL;

	}
#endif

	return connection;

}

float close_remote_connection(tls_connection_t* connection) {
	mbedtls_ssl_close_notify( &connection->ssl );

	mbedtls_net_free( &connection->server_fd );

	mbedtls_x509_crt_free( &connection->cacert );
	mbedtls_ssl_free( &connection->ssl );
	mbedtls_ssl_config_free( &connection->conf );
	mbedtls_ctr_drbg_free( &connection->ctr_drbg );
	mbedtls_entropy_free( &connection->entropy );

	float seconds_elapsed = get_seconds_elapsed(connection->start_clock, get_clock());
	return seconds_elapsed;
}

bool remote_request(tls_connection_t* connection, const char* request, i32 request_len, memrw_t* mem_buffer) {

	bool success = false;
	/*
	 * 3. Write the GET request
	 */
//	mbedtls_printf( "  > Write to server:" );
//	fflush( stdout );

//	u8 buf[1024];
//	i32 len = sprintf( (char *) buf, GET_REQUEST );
	i32 ret = 1;
	i32 len = request_len;

#ifdef REMOTE_VERBOSE
	console_print("Writing request: %s", request);
#endif

	while( ( ret = mbedtls_ssl_write( &connection->ssl, (const u8*)request, request_len ) ) <= 0 )
	{
		if( ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE )
		{
			console_print_error( " failed\n  ! mbedtls_ssl_write returned %d\n\n", ret );
			goto exit;
		}
	}

	len = ret;
#ifdef REMOTE_VERBOSE
	console_print( " %d bytes written\n\n%s", request_len, request );
#endif

	/*
	 * 7. Read the HTTP response
	 */
#ifdef REMOTE_VERBOSE
	console_print( "  < Read from server:" );
#endif
	fflush( stdout );

	u8 read_buffer[KILOBYTES(16)];

	do
	{
		len = sizeof( read_buffer );
		memset( read_buffer, 0, sizeof( read_buffer ) );
		ret = mbedtls_ssl_read( &connection->ssl, read_buffer, len );

		if( ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE )
			continue;

		if( ret == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY ) {
			ret = 0;
			success = true;
			break;
		}

		if( ret < 0 )
		{
			console_print_error( "failed\n  ! mbedtls_ssl_read returned %d\n\n", ret );
			break;
		}

		if( ret == 0 )
		{
//			mbedtls_printf( "\n\nEOF\n\n" );
			success = true;
			break;
		}

		len = ret;
#ifdef REMOTE_VERBOSE
		console_print( " %d bytes read\n", len );
#endif
		memrw_push_back(mem_buffer, read_buffer, len);
	}
	while( 1 );

	exit:
	return success;

}

u8 *do_http_request(const char *hostname, i32 portno, const char *uri, i32 *bytes_read, i32 thread_id) {
	tls_connection_t* connection = open_remote_connection(hostname, portno, alloca(sizeof(tls_connection_t)));
	if (connection) {
		static const char requestfmt[] = "GET %s HTTP/1.1\r\nConnection: close\r\n\r\n";
		char request[4096];
		snprintf(request, sizeof(request), requestfmt, uri);
		size_t request_len = strlen(request);

		memrw_t mem_buffer = memrw_create(MEGABYTES(1));
		bool read_ok = remote_request(connection, request, request_len, &mem_buffer);
		float seconds_elapsed = close_remote_connection(connection);
		if (read_ok) {
			console_print_verbose( "[thread %d] http request: %d bytes read in %g seconds\n", thread_id, mem_buffer.used_size, seconds_elapsed );
			if (bytes_read) {
				*bytes_read = mem_buffer.used_size;
			}
			return mem_buffer.data;
//			console_print("Downloaded case list '%s' in %g seconds.\n", filename, seconds_elapsed);
		} else {
			memrw_destroy(&mem_buffer);
			return NULL;
		}
	}
	return NULL;
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

u8* download_remote_caselist(const char* hostname, i32 portno, const char* filename, i32* bytes_read) {
	char uri[2048];
	snprintf(uri, sizeof(uri), "/slide_set/%s", filename);
	u8* read_buffer = do_http_request(hostname, portno, uri, bytes_read, 0);
	return read_buffer;
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

	memrw_t mem_buffer = memrw_create(MEGABYTES(2));
	bool32 read_ok = remote_request(connection, request, request_len, &mem_buffer);
	float seconds_elapsed = close_remote_connection(connection);

	if (read_ok && mem_buffer.used_size > 0) {
		// now we should have the whole HTTP response
#if REMOTE_CLIENT_VERBOSE
		console_print("HTTP read finished, length = %d\n", mem_buffer->len);
#endif
//	fwrite(read_buffer, total_bytes_read, 1, stdout);

		tiff_t tiff = {0};
		if (tiff_deserialize(&tiff, mem_buffer.data, mem_buffer.used_size)) {
			tiff.is_remote = true;
			tiff.location = (network_location_t){ .hostname = hostname, .portno = portno, .filename = filename };

			unload_all_images(app_state);
			image_t image = {0};
			bool is_valid = init_image_from_tiff(&image, tiff, false, NULL);
			add_image(app_state, image, true, false);
			success = is_valid;
		} else {
			tiff_destroy(&tiff);
		}

	}
	memrw_destroy(&mem_buffer);


//	float seconds_elapsed = close_remote_connection(connection);
	console_print("Open remote took %g seconds\n", seconds_elapsed);
	return success;
}

#if DO_DEBUG
void do_remote_connection_test() {
	const char* hostname = "google.com";
	tls_connection_t* connection = open_remote_connection(hostname, 443, alloca(sizeof(tls_connection_t)));
	if (connection) {
		static const char request[] = "GET / HTTP/1.0\r\nConnection: close\r\n\r\n";
		memrw_t mem_buffer = memrw_create(MEGABYTES(1));
		bool read_ok = remote_request(connection, request, strlen(request), &mem_buffer);
		float seconds_elapsed = close_remote_connection(connection);
		if (read_ok) {
			console_print( "Remote connection test (%s): %d bytes read in %g seconds\n", hostname, mem_buffer.used_size, seconds_elapsed );
			memrw_push_back(&mem_buffer, "\0", 1);
			FILE* test_out = fopen("test_google.html", "wb");
			fwrite(mem_buffer.data, mem_buffer.used_size-1, 1, test_out);
			fclose(test_out);
//			console_print("Downloaded case list '%s' in %g seconds.\n", filename, seconds_elapsed);
		} else {
		}
		memrw_destroy(&mem_buffer);
	}
}
#endif



