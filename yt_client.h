#ifndef YT_CLIENT_H
#define YT_CLIENT_H

#include "query.h"
#include "buffer.h"
#include "connection.h"

#include <stdlib.h>
#include <stdbool.h>
#include <cjson/cJSON.h>
#include <openssl/ssl.h>

#define CRLF "\r\n"
#define HTTP_PROTOCOL_VER "1.1"
#define VALID_HTTPS_RESPONSE_CODE "200"
#define CONTENT_LENGTH_HEADER_TAG "Content-Length:"
#define CHUNKED_ENCODING "chunked"
#define TRANSFER_ENCODING_HEADER_TAG "Transfer-Encoding:"

#define CONNECTION_STATUS "keep-alive"
#define USER_AGENT "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/130.0.0.0 Safari/537.36"

#define CLIENT_NAME "WEB"
#define CLIENT_VER "2.20250730"
#define YT_API_PLAYLIST_BROWSE_ID_PREFIX "VL"    
#define YT_API_TRENDING_BROWSE_ID "FEtrending" 
#define YT_API_CHANNEL_VIDEOS_PARAMS "EgZ2aWRlb3PyBgQKAjoA"  

typedef struct
{
    char header[512];
    char path[256];
    char* payload;
} HttpsRequest;

// payload config
cJSON* configure_base_payload();
cJSON* configure_client_object();
cJSON* configure_post_payload(const Query* query, const char* continuation_token);
bool add_view_trending_videos_payload(cJSON* root);
bool add_view_related_videos_payload(cJSON* root, const char* videoId);
bool add_view_channel_videos_payload(cJSON* root, const char* channel_id);
bool add_continuation_payload(cJSON* root, const char* continuation_token);
bool add_view_playlist_videos_payload(cJSON* root, const char* playlist_id);
bool add_view_user_input_payload(cJSON* root, const char* user_input, const SortType sort_type, const MediaType media_type);

// path and header config
bool configure_youtube_internal_api_path(char* dest, const size_t dest_size, QueryType query_type, const char* key);
bool configure_get_header(char* dest, const size_t dest_size, const char* host, const char* path);
bool configure_post_header(char* dest, const size_t dest_size, const char* host, const char* path, const size_t content_length);

// post request config
HttpsRequest configure_post_request(const Query query, const char* host, const char* api_key, const char* continuation_token);
bool post_request_is_ready(const HttpsRequest post);

// ssl communication
bool ssl_write_request(SSL* ssl, const HttpsRequest req);
int ssl_read_n(SSL* ssl, Buffer* buffer, const size_t n);
int ssl_read_line(SSL* ssl, char* dest, const size_t dest_size);
int ssl_read_header(SSL* ssl, char* dest, const size_t dest_size);
int ssl_read_chunk(SSL* ssl, Buffer* buffer);

// response handling
bool status_code_is_valid(const char* response_header);
void get_http_header_tag_value(const char* header, const char* name, char* dest, const size_t dest_size);
Buffer get_https_response(const HttpsRequest request, SSL_CTX* ssl_ctx, Connection* connection);

#endif