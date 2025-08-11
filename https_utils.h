#ifndef HTTPS_REQUEST_H
#define HTTPS_REQUEST_H

#include "buffer.h"
#include "connection.h"

#include <stdlib.h>
#include <stdbool.h>
#include <openssl/types.h>

#define CRLF "\r\n"
#define VALID_HTTPS_RESPONSE_CODE "200"
#define CONTENT_LENGTH_HEADER_TAG "Content-Length:"
#define TRANSFER_ENCODING_HEADER_TAG "Transfer-Encoding:"
#define CHUNKED_ENCODING "chunked"

typedef struct
{
    char header[512];
    char path[256];
    char* payload;
} HttpsRequest;

// request configuration
bool configure_get_header(char* dest, const size_t dest_size, const char* host, const char* path, const char* user_agent, const char* connection_status, const char* protocol_ver);
bool configure_post_header(char* dest, const size_t dest_size, const char* host, const char* path, const char* user_agent, const char* connection_status, const size_t content_length, const char* protocol_ver);
bool post_request_is_ready(const HttpsRequest post);

// ssl communication
bool ssl_write_request(SSL* ssl, const HttpsRequest req);
int ssl_read_n(SSL* ssl, Buffer* buffer, const size_t n);
int ssl_read_line(SSL* ssl, char* dest, const size_t dest_size);
int ssl_read_header(SSL* ssl, char* dest, const size_t dest_size);
int ssl_read_chunk(SSL* ssl, Buffer* buffer);

// response handling
bool status_code_is_valid(const char* response_header, const char* http_protocol_ver);
void get_http_header_tag_value(const char* header, const char* name, char* dest, const size_t dest_size);
Buffer get_https_response(const HttpsRequest request, SSL_CTX* ssl_ctx, Connection* connection, const char* http_protocol_ver);

#endif