#include "utils.h"
#include "yt_client.h"

#include <ctype.h>
#include <stdio.h>

cJSON* configure_base_payload()
{
    cJSON* client = configure_client_object();
    if (client == NULL) {
        fprintf(stderr, "configure_base_payload: client is null\n");
        return NULL;
    }

    cJSON* context = cJSON_CreateObject();
    if (context == NULL) {
        fprintf(stderr, "configure_base_payload: context is null\n");
        cJSON_Delete(client); client = NULL;
        return NULL;
    }
    
    if (cJSON_AddItemToObject(context, "client", client) == false) {
        fprintf(stderr, "configure_base_payload: failed to add client to context object\n");
        cJSON_Delete(client); client = NULL;
        cJSON_Delete(context); context = NULL;
        return NULL;
    }  
    
    cJSON* root = cJSON_CreateObject();
    if (root == NULL) {
        fprintf(stderr, "configure_base_payload: root is null\n");
        cJSON_Delete(context); context = NULL;
        return NULL;
    }

    if (cJSON_AddItemToObject(root, "context", context) == false) {
        fprintf(stderr, "configure_base_payload: failed to add context object to root\n");
        cJSON_Delete(context); context = NULL;
        cJSON_Delete(root); root = NULL;
    }  

    return root; 
}

cJSON* configure_client_object()
{
    cJSON* client = cJSON_CreateObject();
    if (client == NULL) {
        fprintf(stderr, "configure_client_object: failed to create object\n");
        return NULL;
    }

    if ((cJSON_AddStringToObject(client, "clientName", CLIENT_NAME) == NULL) || 
        (cJSON_AddStringToObject(client, "clientVersion", CLIENT_VER) == NULL)) {
        fprintf(stderr, "configure_client_object: failed to add client elements\n");
        cJSON_Delete(client); client = NULL;
    }

    return client;
}

cJSON* configure_post_payload(const Query* query, const char* continuation_token)
{
    if (query == NULL) return NULL;

    cJSON* root = configure_base_payload();
    if (root == NULL) {
        printf("configure_post_payload: failed to create base payload\n");
        return NULL;
    }

    if ((query->attr == QUERTY_ATTR_APPEND) && (add_continuation_payload(root, continuation_token) == false))  {
        fprintf(stderr, "configure_post_payload: failed to add continuation payload\n");
        cJSON_Delete(root); root = NULL;
    }

    else if (query->attr == QUERY_ATTR_REPLACE) {
        bool success = false;

        switch (query->type) {
            case QUERY_TYPE_VIEW_VIDEO: 
            case QUERY_TYPE_VIEW_RELATED:  success = add_view_related_videos_payload(root, query->focused_id); break;    
            case QUERY_TYPE_USER_INPUT:    success = add_view_user_input_payload(root, query->string, query->sort, query->media); break;
            case QUERY_TYPE_VIEW_CHANNEL:  success = add_view_channel_videos_payload(root, query->focused_id);  break;
            case QUERY_TYPE_VIEW_PLAYLIST: success = add_view_playlist_videos_payload(root, query->focused_id); break;
            case QUERY_TYPE_VIEW_TRENDING: success = add_view_trending_videos_payload(root); break;
            default:    
                fprintf(stderr, "configure_post_payload: QueryType %d does not have a payload\n", query->type);
        }

        if (success == false) {
            fprintf(stderr, "configure_post_payload: failed to add payload\n");
            cJSON_Delete(root); root = NULL;
        }
    }

    return root;
}

bool add_view_trending_videos_payload(cJSON* root)
{
    if (root == NULL) return false;

    return cJSON_AddStringToObject(root, "browseId", YT_API_TRENDING_BROWSE_ID);
}

bool add_view_related_videos_payload(cJSON* root, const char* video_id)
{
    if ((root == NULL) || (valid_string(video_id) == false)) return false;

    return cJSON_AddStringToObject(root, "videoId", video_id);
}

bool add_view_channel_videos_payload(cJSON* root, const char* channel_id)
{
    if ((root == NULL) || (valid_string(channel_id) == false)) return false;

    return cJSON_AddStringToObject(root, "browseId", channel_id) &&
           cJSON_AddStringToObject(root, "params", YT_API_CHANNEL_VIDEOS_PARAMS);
}

bool add_continuation_payload(cJSON* root, const char* continuation_token)
{
    if ((root == NULL) || (valid_string(continuation_token) == false)) return false;

    return cJSON_AddStringToObject(root, "continuation", continuation_token);
}

bool add_view_playlist_videos_payload(cJSON* root, const char* playlist_id)
{
    if ((root == NULL) || (valid_string(playlist_id) == false)) return false;

    char browse_id[64] = {0};

    const int written = snprintf(browse_id, sizeof(browse_id),"%s%s", YT_API_PLAYLIST_BROWSE_ID_PREFIX, playlist_id);
    if ((written < 0) || (written >= sizeof(browse_id))) {
        fprintf(stderr, "add_view_playlist_videos_payload: snprintf returned %d\n", written);
        return false;
    }

    return cJSON_AddStringToObject(root, "browseId", browse_id);
}

bool add_view_user_input_payload(cJSON* root, const char* user_input, const SortType sort_type, const MediaType media_type)
{
    if ((root == NULL) || (valid_string(user_input) == false)) return false;

    const char* sort_param = sort_type_to_search_param(sort_type);
    const char* media_param = media_type_to_search_param(media_type);

    if ((sort_param == NULL) || (media_param == NULL)) {
        fprintf(stderr, "add_view_user_input_payload: SortType (%d) or MediaType (%d) returned invalid search param\n", sort_type, media_type);
        return false;
    }

    char params[32] = {0};
    
    const int written = snprintf(params, sizeof(params), "%s%s", sort_param, media_param);
    if ((written < 0) || (written >= sizeof(params))) {
        fprintf(stderr, "add_view_user_input_payload: snprintf returned %d\n", written);
        return false;
    }

    return cJSON_AddStringToObject(root, "query", user_input) && 
           cJSON_AddStringToObject(root, "params", params);
}

bool configure_youtube_internal_api_path(char* dest, const size_t dest_size, QueryType query_type, const char* key)
{
    const char* endpoint = query_type_to_endpoint(query_type);

    if ((endpoint == NULL) || (dest == NULL) || (valid_string(key) == false)) return false;

    const size_t written = snprintf(dest, dest_size, "/youtubei/v1/%s?key=%s", endpoint, key);

    return (written > 0) && (written < dest_size);
}

bool configure_get_header(char* dest, const size_t dest_size, const char* host, const char* path)
{
    if ((dest == NULL) || (valid_string(host) == false) || (valid_string(path) == false)) return false;

    const int len =  snprintf(dest, dest_size,
                "GET %s HTTP/%s\r\n"
                        "Host: %s\r\n"
                        "User-Agent: %s\r\n"
                        "Connection: %s\r\n"
                        "\r\n",
                        path, HTTP_PROTOCOL_VER, host, USER_AGENT, CONNECTION_STATUS);
    
    return (len > 0) && (len < dest_size);
}

bool configure_post_header(char* dest, const size_t dest_size, const char* host, const char* path, const size_t content_length)
{
    if ((dest == NULL) || (valid_string(host) == false) || (valid_string(path) == false)) return false;

    const int len = snprintf(dest, dest_size,
                        "POST %s HTTP/%s\r\n"
                        "Host: %s\r\n"
                        "User-Agent: %s\r\n"
                        "Content-Type: application/json\r\n"
                        "Accept: application/json\r\n"
                        "Content-Length: %zu\r\n"
                        "Connection: %s\r\n"
                        "\r\n",
                        path, HTTP_PROTOCOL_VER, host, USER_AGENT, content_length, CONNECTION_STATUS);
    
    return (len > 0) && (len < dest_size);
}

HttpsRequest configure_post_request(const Query query, const char* host, const char* api_key, const char* continuation_token)
{
    if (valid_string(host) == false) return (HttpsRequest){0};

    HttpsRequest post = (HttpsRequest){0};

    if (configure_youtube_internal_api_path(post.path, sizeof(post.path), query.type, api_key) == false) {
        fprintf(stderr, "configure_post_request: failed to resolve path\n");
        return post;
    }

    cJSON* payload = configure_post_payload(&query, continuation_token);

    if (payload == NULL) {
        fprintf(stderr, "configure_post_request: payload is null\n");
        return post;
    }

    post.payload = cJSON_Print(payload);

    if (configure_post_header(post.header, sizeof(post.header), host, post.path, strlen(post.payload)) == false) {
        fprintf(stderr, "configure_post_request: failed to resolve header\n");
        free(post.payload); post.payload = NULL;
    }
    
    cJSON_Delete(payload); payload = NULL;
    
    return post;
}

bool post_request_is_ready(const HttpsRequest post)
{
    return (valid_string(post.header)) && (valid_string(post.payload));
}

bool ssl_write_request(SSL* ssl, const HttpsRequest req)
{
    if (ssl == NULL) return false;

    int header_status;
    if ((header_status = SSL_write(ssl, req.header, strlen(req.header))) <= 0) {
        fprintf(stderr, "ssl_write_request: SSL_write returned %d for request header\n", header_status);
        return false;
    }

    int body_status;
    if (valid_string(req.payload) && 
       ((body_status = SSL_write(ssl, req.payload, strlen(req.payload))) <= 0)) {
        fprintf(stderr, "ssl_write_request: SSL_write returned %d for request body\n", body_status);
        return false;
    }

    return true;
}

int ssl_read_n(SSL* ssl, Buffer* buffer, const size_t n)
{
    if ((ssl == NULL) || (buffer == NULL)) return -1;

    char data[4096] = {0};

    size_t bytes_read = 0;
    size_t bytes_remaining = n;

    while (bytes_remaining > 0) {
        size_t to_read = (bytes_remaining < sizeof(data)) ? bytes_remaining : sizeof(data);
        
        int read = SSL_read(ssl, data, to_read);
        if (read <= 0) {
            fprintf(stderr, "ssl_read_n: SSL_read returned %d\n", read);
            return read;
        }

        bytes_read += read;
        bytes_remaining -= read; 

        buffer_write_data(buffer, data, read);
    }      

    return bytes_read;
}

int ssl_read_line(SSL* ssl, char* dest, const size_t dest_size) 
{
    if ((ssl == NULL) || (dest == NULL)) return -1;

    size_t pos = 0;
    char c;

    while (pos < dest_size - 1) {
        const int byte = SSL_read(ssl, &c, sizeof(c));
        if (byte <= 0) {
            fprintf(stderr, "ssl_read_line: SSL_read returned %d\n", byte);
            return byte;
        }

        dest[pos++] = c;

        if (c == '\n') break;
    }

    dest[pos] = '\0';

    return pos;
}

int ssl_read_header(SSL* ssl, char* dest, const size_t dest_size)
{
    if ((ssl == NULL) || (dest == NULL)) return -1;

    const char* last_line = "\r\n";

    size_t total_len = 0;

    char line[1024] = {0};
    int line_len = 0;

    while(strcmp(line, last_line) != 0) {
        line_len = ssl_read_line(ssl, line, sizeof(line));
        if (line_len <= 0) {
            fprintf(stderr, "ssl_read_header: invalid line read\n");
            dest[total_len] = '\0';
            return line_len;
        }

        strncat(dest, line, dest_size - total_len - 1);

        total_len += line_len;
    }

    dest[total_len] = '\0';

    return total_len;
}

int ssl_read_chunk(SSL* ssl, Buffer* buffer)
{
    if ((ssl == NULL) || (buffer == NULL)) return -1;

    const unsigned long crlf_len = strlen(CRLF);

    char hex[16] = {0};
    int len = ssl_read_line(ssl, hex, sizeof(hex));
    if (len <= crlf_len) {
        fprintf(stderr, "ssl_read_chunk: failed to read chunk size\n");
        return -1;
    }

    hex[len - crlf_len] = '\0';

    const int chunk_size = strtol(hex, NULL, 16);
    int read = ssl_read_n(ssl, buffer, chunk_size);
    if (read != chunk_size) {
        fprintf(stderr, "ssl_read_chunk: (%d/%d) bytes read\n", read, chunk_size);
        return -1;
    }

    char trailing_crlf[4];
    int crlf_read = ssl_read_line(ssl, trailing_crlf, sizeof(trailing_crlf));
    if (crlf_read != crlf_len) {
        fprintf(stderr, "ssl_read_chunk: failed to read trailing crlf\n");
        return -1;
    }

    return chunk_size;
}

bool status_code_is_valid(const char* response_header)
{
    if (valid_string(response_header) == false) return false;

    char* status_line = strstr(response_header, "HTTP/" HTTP_PROTOCOL_VER);
    if (status_line == NULL) {
        fprintf(stderr, "status_code_is_valid: request code not found\n");
        return false;
    }

    status_line += strlen("HTTP/" HTTP_PROTOCOL_VER);

    char* end = strstr(status_line, CRLF);

    if (end) {
        char response_code[8] = {0};
        memcpy(response_code, status_line, end - status_line);
        return strstr(response_code, VALID_HTTPS_RESPONSE_CODE);
    }

    fprintf(stderr, "status_code_is_valid: response header ill formatted\n");

    return false;
}

void get_http_header_tag_value(const char* header, const char* tag, char* dest, const size_t dest_size)
{
    if ((valid_string(header) == false) || (valid_string(tag) == false) || (dest == NULL)) return;

    const char* header_line = strstr(header, tag);
    if (header_line == NULL) {
        fprintf(stderr, "get_http_header_value: \"%s\" was not found in response header\n", tag);
        return;
    }

    char* start = strchr(header_line, ':');
    if (start) {
        char* ptr = start + 1; 

        while(*ptr && isspace(*ptr)) {
            ptr++;
        } 

        size_t i;
        for (i = 0; (i < dest_size) && (*ptr) && (*ptr != '\r'); i++, ptr++) {
            dest[i] = *ptr;
        }

        dest[i] = '\0';
    }
}

Buffer get_https_response(const HttpsRequest request, SSL_CTX* ssl_ctx, Connection* connection)
{
    Buffer res = buffer_init();

    if ((ssl_ctx == NULL) || (connection == NULL)) return res;

    pthread_mutex_lock(&connection->mutex);

    if (connected_to_internet() == false) {
        fprintf(stderr, "get_https_response: no internet connection\n");
        connection->connected = false;
        goto cleanup;
    }

    if (connection->connected == false) {
        if ((connection->connected = connection_establish(connection, ssl_ctx)) == false) {
            fprintf(stderr,"get_https_response: failed to establish connection\n");
            goto cleanup;
        }
    }

    if (ssl_write_request(connection->ssl, request) == false) {
        printf("get_https_response: failed to write request\n");
        connection->connected = false;
        goto cleanup;
    }

    char header[4096] = {0};
    if (ssl_read_header(connection->ssl, header, sizeof(header)) <= 0) {
        printf("get_https_response: failed to read header\n");
        goto cleanup;
    }

    if (status_code_is_valid(header) == false) {
        fprintf(stderr, "get_https_response: invalid response code\n");
        write_string_to_file("error_header.txt", header);
        goto cleanup;
    }

    char len_str[16];
    get_http_header_tag_value(header, CONTENT_LENGTH_HEADER_TAG, len_str, sizeof(len_str));

    const int content_length = strtol(len_str, NULL, 10);
    if (content_length > 0) {
        const int read = ssl_read_n(connection->ssl, &res, content_length);
        if (read != content_length) {
            fprintf(stderr, "get_https_response: (%d/%d) read\n", read, content_length);
            buffer_free(&res);
        }
    }

    else {
        char encoding_type[16];
        get_http_header_tag_value(header, TRANSFER_ENCODING_HEADER_TAG, encoding_type, sizeof(encoding_type));

        if (strcmp(encoding_type, CHUNKED_ENCODING) == 0) {
            int read;
            while ((read = ssl_read_chunk(connection->ssl, &res)) > 0)
                ;

            if (read < 0) {
                buffer_free(&res);
            }
        }
    }

    cleanup:
        pthread_mutex_unlock(&connection->mutex);
        return res;
}