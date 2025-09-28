#ifndef HTTPS_REQUEST_H
#define HTTPS_REQUEST_H

#include "buffer.h"
#include "connection.h"

#include <cjson/cJSON.h>

#define HTTP_PROTOCOL_VER "1.1"
#define CONNECTION_STATUS "keep-alive"
#define USER_AGENT "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/130.0.0.0 Safari/537.36"

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

bool configure_get_header  (char * dest, const size_t dest_size, const char * host, const char * path) ;
bool configure_post_header (char * dest, const size_t dest_size, const char * host, const char * path, const size_t content_length) ;

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
cJSON* get_json_response(const HttpsRequest* req, SSL_CTX* ssl_ctx, Connection* conn, const char* protocol_ver);

#endif