#include "include/https_utils.h"

#include "include/utils.h"

#include <ctype.h>

bool configure_get_header (char* dest, const size_t dest_size, const char* host, const char* path)
{
    if ((dest == NULL) || (!valid_string(host)) || (!valid_string(path)))
        return false;

    const int written =  snprintf(dest, dest_size,
                "GET %s HTTP/%s\r\n"
                        "Host: %s\r\n"
                        "User-Agent: %s\r\n"
                        "Connection: %s\r\n"
                        "\r\n",
                        path, HTTP_PROTOCOL_VER, host, USER_AGENT, CONNECTION_STATUS);
    
    return (written > 0) && (written < dest_size);
}

bool configure_post_header (char* dest, const size_t dest_size, const char* host, const char* path, const size_t content_length)
{ 
    if ((dest == NULL) || (!valid_string(host)) || (!valid_string(path)))
        return false;

    const int written = snprintf(dest, dest_size,
                        "POST %s HTTP/%s\r\n"
                        "Host: %s\r\n"
                        "User-Agent: %s\r\n"
                        "Content-Type: */*\r\n"
                        "Accept: */*\r\n"
                        "Content-Length: %zu\r\n"
                        "Connection: %s\r\n"
                        "\r\n",
                        path, HTTP_PROTOCOL_VER, host, USER_AGENT, content_length, CONNECTION_STATUS);
    
    return (written > 0) && (written < dest_size);
}

bool ssl_write_request(SSL* ssl, const HttpsRequest req)
{
    if (!ssl) 
        return false;

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
    if (!ssl || !buffer)
        return -1;

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

        write_to_buffer(buffer, data, read);
    }      

    return bytes_read;
}

int ssl_read_line(SSL* ssl, char* dest, const size_t dest_size) 
{
    if (!ssl || !dest) 
        return -1;

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
    if (!ssl || !dest)
        return -1;

    const char* last_line = "\r\n";

    size_t total_len = 0;

    char line[1024] = {0};
    int line_len = 0;

    while((strcmp(line, last_line) != 0) && (total_len < dest_size - 1)) {
        if ((line_len = ssl_read_line(ssl, line, sizeof(line))) <= 0) {
            fprintf(stderr, "ssl_read_header: ssl read line returned (%d)\n", line_len);
            dest[total_len] = '\0';
            return line_len;
        }

        const size_t copy_len = line_len + total_len >= dest_size ? 
                                dest_size - total_len - 1 :
                                line_len;

        memcpy(dest + total_len, line, copy_len);

        total_len += copy_len;
    }

    dest[total_len] = '\0';

    return total_len;
}

int ssl_read_chunk(SSL* ssl, Buffer* buffer)
{
    if (!ssl || !buffer) 
        return -1;

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

bool status_code_is_valid(const char* response_header, const char* http_protocol_ver)
{
    if (!valid_string(response_header) || !valid_string(http_protocol_ver)) 
        return false;

    char* status_line = strstr(response_header, http_protocol_ver);
    if (!status_line) {
        fprintf(stderr, "status_code_is_valid: request code not found\n");
        return false;
    }

    status_line += strlen(http_protocol_ver);

    char* end = strstr(status_line, CRLF);

    if (end) {
        char response_code[32] = {0};
        memcpy(response_code, status_line, end - status_line);
        return strstr(response_code, VALID_HTTPS_RESPONSE_CODE);
    }

    fprintf(stderr, "status_code_is_valid: response header ill formatted\n");
    printf("%s\n", response_header);
    return false;
}

void get_http_header_tag_value(const char* header, const char* tag, char* dest, const size_t dest_size)
{
    if (!valid_string(header) || !valid_string(tag) || !dest) 
        return;

    const char* header_line = strstr(header, tag);
    if (!header_line) 
        return;

    char* start = strchr(header_line, ':');
    if (start) {
        char* ptr = start + 1; 

        while(*ptr && isspace(*ptr)) 
            ptr++;

        size_t i;
        for (i = 0; (i < dest_size) && (*ptr) && (*ptr != '\r'); i++, ptr++) 
            dest[i] = *ptr;
        

        dest[i] = '\0';
    }
}

Buffer get_https_response(const HttpsRequest request, SSL_CTX* ssl_ctx, Connection* connection, const char* http_protocol_ver)
{
    Buffer res = buffer_init();

    if (!ssl_ctx || !connection || !valid_string(http_protocol_ver)) 
        return res;

    pthread_mutex_lock(&connection->mutex);

    if (!connected_to_internet()) {
        fprintf(stderr, "get_https_response: no internet connection\n");
        connection->connected = false;
        goto cleanup;
    }

    if (!connection->connected) {
        if ((connection->connected = connection_establish(connection, ssl_ctx)) == false) {
            fprintf(stderr,"get_https_response: failed to establish connection\n");
            goto cleanup;
        }
    }

    if (!ssl_write_request(connection->ssl, request)) {
        printf("get_https_response: failed to write request\n");
        connection->connected = false;
        goto cleanup;
    }

    char header[4096] = {0};
    if (ssl_read_header(connection->ssl, header, sizeof(header)) <= 0) {
        printf("get_https_response: failed to read header\n");
        connection->connected = false;
        goto cleanup;
    }

    if (!status_code_is_valid(header, http_protocol_ver)) {
        fprintf(stderr, "get_https_response: invalid response code\n");
        write_string_to_file("error_header.txt", header);
        goto cleanup;
    }

    char len_str[16] = {0};
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
        char encoding_type[16] = {0};
        get_http_header_tag_value(header, TRANSFER_ENCODING_HEADER_TAG, encoding_type, sizeof(encoding_type));

        if (strcmp(encoding_type, CHUNKED_ENCODING) == 0) {
            int read;
            while ((read = ssl_read_chunk(connection->ssl, &res)) > 0)
                ;

            if (read < 0) 
                buffer_free(&res);
        }
    }

    cleanup:
        pthread_mutex_unlock(&connection->mutex);
        return res;
}

cJSON* get_json_response(const HttpsRequest* req, SSL_CTX* ssl_ctx, Connection* conn, const char* protocol_ver)
{
    if (!req || !ssl_ctx || !conn || !valid_string(protocol_ver)) 
        return NULL;

    Buffer res = get_https_response((*req), ssl_ctx, conn, protocol_ver);
    if (!buffer_is_ready(&res)) {
        fprintf(stderr, "get_json_response: invalid response recived\n");
        return NULL;
    }

    cJSON* ret = cJSON_Parse(res.data);

    buffer_free(&res);
    
    return ret;
}