#include <time.h>
#include <ctype.h>
#include <netdb.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <pthread.h>
#include <stdbool.h>
#include <arpa/inet.h>
#include <cjson/cJSON.h>
#include <openssl/ssl.h>
#include "uthash.h"

#include "uthash.h"
// hold data in memory to be processed later
typedef struct
{
    size_t size;
    char* data;
} Buffer;

Buffer init_buffer()
{
    Buffer buffer;
    buffer.data = NULL;
    buffer.size = 0;
    return buffer;
}

void write_data_to_buffer(Buffer *buffer, const char* data, const size_t n)
{
    const size_t new_size = buffer->size + n + 1;
    
    char *new_data = realloc(buffer->data, new_size);
    if (!new_data) {
        printf("write_data_to_buffer: failed to reallocate %zu bytes\n", new_size);
        return;
    }

    buffer->data = new_data;
    memcpy(&buffer->data[buffer->size], data, n);
    buffer->size += n;
    buffer->data[buffer->size] = '\0';
}

bool buffer_ready(const Buffer *buffer)
{
    if (!buffer){
        printf("buffer_ready: 'buffer' arg is NULL\n");
        return false;
    }
    
    return (buffer->size > 0) && (buffer->data != NULL);
}

void free_buffer(Buffer *buffer)
{
    if (!buffer) {
        printf("buffer_ready: 'buffer' arg is NULL\n");
        return;
    }

    if (buffer->data) free(buffer->data);
    buffer->data = NULL;
    buffer->size = 0;
}

void create_file_from_memory(const char* filename, const Buffer buffer) 
{
    FILE* fp = fopen(filename, "wb");
    if (!fp) 
        printf("could not write memory into \"%s\"\n", filename);
    else {
        fwrite(buffer.data, 1, buffer.size, fp);
        fclose(fp);
    } 
}

SSL_CTX *ctx = NULL;

typedef struct {
    SSL *ssl;
    int sockfd;
    char host[64];
    bool connected;
    pthread_mutex_t mutex;
    struct addrinfo *address_information;
} PersistentConnection;

#define HTTPS "443"
#define N_CONN MAX_THREADS

void init_persistent_connection(PersistentConnection *connection, const char *host, const char *port)
{
    memset(connection, 0, sizeof(PersistentConnection));
    connection->sockfd = -1; 
    connection->connected = false;
    strncpy(connection->host, host, sizeof(connection->host) - 1);
    pthread_mutex_init(&connection->mutex, NULL);
}

bool file_descriptor_is_valid(const int fd)
{
    return fd >= 0;
}

void disconnect(PersistentConnection *connection)
{
    if (!connection) return;

    if (connection->address_information) {
        freeaddrinfo(connection->address_information);
    }

    if (connection->ssl) {
        SSL_shutdown(connection->ssl);
        SSL_free(connection->ssl);
        connection->ssl = NULL;
    }

    if (file_descriptor_is_valid(connection->sockfd)) {
        close(connection->sockfd);
        connection->sockfd = -1;
    }

    connection->connected = false;
}

bool establish_connection(PersistentConnection *connection)
{
    if (connection == NULL) {
        printf("establish_connection: 'connection' argument is NULL\n");
        return false;
    }

    else if (!connection->host[0]) {
        printf("establish_connection: 'host' argument is empty\n");
        return false;
    }

    disconnect(connection);

    struct addrinfo desired_address_information = {0};
    desired_address_information.ai_family = AF_INET;
    desired_address_information.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(connection->host, HTTPS, &desired_address_information, &connection->address_information) != 0) {
        printf("establish_persistent_connection: getaddrinfo failed for %s:%s\n", connection->host, HTTPS);
        return false;
    }

    // socket init
    connection->sockfd = socket(connection->address_information->ai_family, connection->address_information->ai_socktype, connection->address_information->ai_protocol);
    if (connection->sockfd < 0) {
        printf("establish_persistent_connection: socket creation failed\n");
        disconnect(connection);
        return false;
    }

    // host connection
    if (connect(connection->sockfd, connection->address_information->ai_addr, connection->address_information->ai_addrlen) != 0) {
        printf("establish_persistent_connection: connect failed for the host: \"%s\"\n", connection->host);
        disconnect(connection);
        return false;
    }

    // SSL init
    connection->ssl = SSL_new(ctx);
    if (!connection->ssl) {
        printf("establish_persistent_connection: SSL_new failed\n");
        disconnect(connection);
        return false;
    }

    // set up ssl over the socket
    SSL_set_fd(connection->ssl, connection->sockfd);
    if (SSL_connect(connection->ssl) != 1) {
        printf("establish_persistent_connection: SSL_connect failed for host %s\n", connection->host);
        disconnect(connection);
        return false;
    }

    return true;
}

void free_persistent_connection(PersistentConnection *connection)
{
    disconnect(connection);
    pthread_mutex_destroy(&connection->mutex);
}

typedef struct
{
    Buffer header;
    Buffer body;
} HTTPS_Response;

HTTPS_Response create_https_response()
{
    HTTPS_Response response;
    response.header = response.body = init_buffer();
    return response;
}

void free_https_response(HTTPS_Response* response)
{
    if (response == NULL) return;
    free_buffer(&response->header);
    free_buffer(&response->body);
}

bool https_response_ready(HTTPS_Response* response)
{
    if (response == NULL) return false;
    return buffer_ready(&response->header) && buffer_ready(&response->body);
}

bool connected_to_wifi()
{
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return false;
    
    struct sockaddr_in server = {
        .sin_family = AF_INET,
        .sin_port = htons(53), 
        .sin_addr.s_addr = inet_addr("8.8.8.8") 
    };
    
    bool connected = (connect(sock, (struct sockaddr*)&server, sizeof(server)) == 0);
    close(sock);
    return connected;
}

void get_https_request_code(char* response_header, const size_t n, char https_request_code[n])
{
    if (response_header == NULL) return;

    char* start = response_header + strlen("HTTP/1.1"); 
    bool in_request_code = false;
    int i = 0;

    for (char* current = start; current && (i < n); current++) {
        const char c = (*current);

        if (in_request_code == false) {
            if (isdigit(c)) {
                in_request_code = true;
            }
        }

        if (in_request_code) {
            if (isdigit(c) == false) {
                break;
            }

            https_request_code[i++] = c;
        }
    }

    https_request_code[i] = '\0';
}

bool https_request_code_is_valid(const char* request_code)
{
    return (strcmp(request_code, "200") == 0); 
}

typedef struct
{
    char path[256];
    char body[2048];
    char header[2048];
} PreparedRequest;

// read one line from ssl stream or n bytes into buffer (whichever comes first)
size_t ssl_read_line(SSL *ssl, char *buffer, const size_t n) 
{
    if (!buffer) {
        printf("ssl_read_line: buffer is NULL\n");
        return 0;
    }

    size_t pos = 0;
    char c;

    while (pos < n - 1) {
        int byte = SSL_read(ssl, &c, 1);
        if (byte <= 0) {
            printf("ssl_read_line: SSL_read returned %d\n", byte);
            return 0;
        }

        buffer[pos++] = c;

        if (c == '\n') {
            break;
        }
    }

    buffer[pos] = '\0';

    return pos;
}

Buffer ssl_read_header(SSL* ssl)
{
    Buffer header = init_buffer();

    char line[1024] = {0};
    
    while(strcmp(line, "\r\n") != 0) {
        const size_t len = ssl_read_line(ssl, line, sizeof(line));
        write_data_to_buffer(&header, line, len);
    }

    return header;
}

// read n bytes from ssl stream into buffer
void ssl_read_n(SSL *ssl, Buffer *buffer, const size_t n)
{
    char data[4096] = {0};
    size_t bytes_remaining = n;
    while (bytes_remaining > 0) {
        size_t to_read = bytes_remaining < sizeof(data) - 1 ? bytes_remaining : sizeof(data) - 1;
        
        int read = SSL_read(ssl, data, to_read);
        if (read <= 0) {
            printf("ssl_read_n: SSL read returned %d\n", read);
            break;
        }

        write_data_to_buffer(buffer, data, read);
        
        bytes_remaining -= read;
    }      
}

size_t get_content_len_from_header(const char *header)
{
    // find the content length parameter
    char *location = strstr(header, "Content-Length:");
    
    // find the first numeric char
    char *first_numeric = location;
    while (first_numeric && !isdigit(*first_numeric)) {
        first_numeric++;
    } 

    // read every numeric char into a buffer
    int i = 0;
    char bytes[16] = {0};
    while (first_numeric && isdigit(*first_numeric)) {
        bytes[i++] = *first_numeric;
        first_numeric++;
    }

    // return numeric representation
    return atoi(bytes);

    return 0;
}

HTTPS_Response send_https_request(const PreparedRequest request, PersistentConnection *connection)
{
    pthread_mutex_lock(&connection->mutex);

    if (connected_to_wifi() == false) {
        printf("send_https_request: not connected to the wifi\n");
        connection->connected = false;
        pthread_mutex_unlock(&connection->mutex);
        return (HTTPS_Response){0};
    }

    if (connection->connected == false) {
        connection->connected = establish_connection(connection);
        if (connection->connected == false) {
            printf("send_https_request: failed to establish connection\n");
            pthread_mutex_unlock(&connection->mutex);
            return (HTTPS_Response){0};
        }
    }

    int header_write_status = SSL_write(connection->ssl, request.header, strlen(request.header));
    if (header_write_status <= 0) {
        printf("send_https_request: SSL_write (header) failed, returned %d\n", header_write_status);
        connection->connected = false;
        pthread_mutex_unlock(&connection->mutex);
        return (HTTPS_Response){0};
    } 

    if (request.body[0] != '\0') {
        int body_write_status = SSL_write(connection->ssl, request.body, strlen(request.body));
        if (body_write_status <= 0) {
            printf("send_https_request: SSL_write (body) failed, returned %d\n", body_write_status);
            connection->connected = false;
            pthread_mutex_unlock(&connection->mutex);
            return (HTTPS_Response){0};
        }
    }

    HTTPS_Response response = create_https_response();

    response.header = ssl_read_header(connection->ssl);
    if (buffer_ready(&response.header) == false) {
        printf("send_https_request: failed to read header from ssl stream\n");
        connection->connected = false;
        pthread_mutex_unlock(&connection->mutex);
        return (HTTPS_Response){0};
    }

    char https_request_code[4];
    get_https_request_code(response.header.data, sizeof(https_request_code), https_request_code);
    if (https_request_code_is_valid(https_request_code) == false) {
        printf("send_https_request: invalid https request code (%s)\n", https_request_code);
        connection->connected = false;
        pthread_mutex_unlock(&connection->mutex);
        return (HTTPS_Response){0};
    }

    if (strstr(response.header.data, "Content-Length:")) {
        size_t content_length = get_content_len_from_header(response.header.data);
        if (content_length > 0) {
            ssl_read_n(connection->ssl, &response.body, content_length);
        }

        else {
            printf("send_https_request: invalid content length read from header\n");
            connection->connected = false;
            pthread_mutex_unlock(&connection->mutex);
            return (HTTPS_Response){0};
        }
    }

    else if (strstr(response.header.data, "Transfer-Encoding: chunked")) {
        const char *crlf = "\r\n";
        const size_t crlf_len = strlen(crlf);
        
        int chunk_size = -1; 
        while (chunk_size != 0) {
            char hex[16] = {0};
            int len = ssl_read_line(connection->ssl, hex, sizeof(hex));
            if (len <= crlf_len) {
                printf("send_https_request: failed to read chunk size\n");
                connection->connected = false;
                pthread_mutex_unlock(&connection->mutex);
                return (HTTPS_Response){0};
            }

            hex[len - crlf_len] = '\0';

            chunk_size = strtol(hex, NULL, 16);
            ssl_read_n(connection->ssl, &response.body, chunk_size);

            char trailing_crlf[16];
            ssl_read_line(connection->ssl, trailing_crlf, sizeof(trailing_crlf));
        }
    }

    pthread_mutex_unlock(&connection->mutex);

    return response;
}















































long int get_file_length(FILE* fp)
{
    const long original_position = ftell(fp);

    fseek(fp, 0, SEEK_END);

    const long file_len = ftell(fp) - original_position;    

    fseek(fp, original_position, SEEK_SET);

    return file_len;
}

char* read_file_into_buffer(const char* filepath)
{
    if (filepath == NULL) return NULL;

    FILE* fp = fopen(filepath, "r");
    if (fp == NULL) {
        printf("read_file_into_buffer: fopen returned NULL for \"%s\"\n", filepath);
        return NULL;
    }

    const long len = get_file_length(fp);
    if (len == 0) {
        printf("read_file_into_buffer: get_file_length returned 0\n");
        fclose(fp); fp = NULL;
        return NULL;
    }

    char* buffer = malloc((sizeof(char) * (len + 1)));
    if (buffer == NULL) {
        printf("read_file_into_buffer: malloc returned NULL\n");
        fclose(fp); fp = NULL;
        return NULL;
    }

    const unsigned long chars_read = fread(buffer, sizeof(char), len, fp);
    buffer[chars_read] = '\0';

    fclose(fp); fp = NULL;

    return buffer;
}

bool json_string_is_valid(const cJSON* json_str)
{
    return json_str && cJSON_IsString(json_str) && (json_str->valuestring[0] != '\0'); 
}

bool json_number_is_valid(const cJSON* json_num)
{
    return json_num && cJSON_IsNumber(json_num); 
}

bool expired(const double expiration_date)
{
    return time(NULL) >= (time_t) expiration_date;
}

typedef struct
{
    char name[32];
    char value[512];
    double expiration_date;
    UT_hash_handle hh;
} Cookie;

#define LOGIN_COOKIE_FILE "cookies.json"

Cookie* create_cookie(const char* name, const char* value, const double expiration_date)
{
    if ((name == NULL) || (value == NULL) || expired(expiration_date)) {
        printf("create_cookie: args are NULL\n");
        return NULL;
    }

    Cookie* cookie = malloc(sizeof(Cookie));
    if (cookie == NULL) {
        printf("create_cookie: malloc returned NULL\n");
        return NULL;
    }

    strncpy(cookie->name, name, sizeof(cookie->name) - 1);
    strncpy(cookie->value, value, sizeof(cookie->value) - 1);

    return cookie;
}

bool cookie_is_needed(const char* cookie_name)
{
    if (cookie_name == NULL) return false;

    return (strcmp(cookie_name, "SID") == 0) ||
           (strcmp(cookie_name, "HSID") == 0) ||
           (strcmp(cookie_name, "SSID") == 0) ||
           (strcmp(cookie_name, "APISID") == 0) ||
           (strcmp(cookie_name, "SAPISID") == 0) ||
           (strcmp(cookie_name, "PREF") == 0) ||
           (strcmp(cookie_name, "LOGIN_INFO") == 0) ||
           (strcmp(cookie_name, "__Secure-1PSID") == 0) ||
           (strcmp(cookie_name, "__Secure-3PSID") == 0) ||
           (strcmp(cookie_name, "__Secure-1PSIDTS") == 0) ||
           (strcmp(cookie_name, "__Secure-3PSIDTS") == 0) ||
           (strcmp(cookie_name, "__Secure-1PSIDCC") == 0) ||
           (strcmp(cookie_name, "__Secure-3PSIDCC") == 0) ||
           (strcmp(cookie_name, "SIDCC") == 0);
}

bool cookie_is_ready(const Cookie* cookie)
{
    return cookie && (cookie->name[0] != '\0') && (cookie->value[0] != '\0');
} 

void print_cookie(Cookie* cookie)
{
    if (cookie == NULL) return;
    printf("%s: %s\n", cookie->name, cookie->value);
}

void print_cookie_jar(Cookie** jar)
{
    if (HASH_COUNT(*jar) == 0) return;

    Cookie* cookie, *tmp;
    HASH_ITER(hh, *jar, cookie, tmp) {
        print_cookie(cookie);
    }
}

Cookie* find_cookie_in_jar(const char* cookie_name, Cookie** jar)
{
    if (cookie_name == NULL) return NULL;
    
    Cookie* found;
    HASH_FIND_STR(*jar, cookie_name, found);
    return found;
}

void add_cookie_to_jar(Cookie* cookie, Cookie** jar)
{
    if (cookie == NULL) {
        printf("add_cookie_to_jar: cookie is NULL\n");
        return;
    }

    HASH_ADD_STR(*jar, name, cookie);
}

void remove_cookie_from_jar(Cookie* cookie, Cookie** jar)
{
    if (HASH_COUNT(*jar) == 0) {
        printf("remove_cookie_from_jar: jar is already empty\n");
        return;
    }

    if (cookie == NULL) {
        printf("remove_cookie_from_jar: cookie is NULL\n");
        return;
    }

    HASH_DEL(*jar, cookie);

    free(cookie); cookie = NULL;
}

void empty_cookie_jar(Cookie** jar)
{
    if (HASH_COUNT(*jar) == 0) {
        printf("remove_cookie_from_jar: jar is already empty\n");
        return;
    }

    Cookie* cookie, *tmp;
    HASH_ITER(hh, *jar, cookie, tmp) {
        remove_cookie_from_jar(cookie, jar);
    }
}

void parse_login_cookies(Cookie** jar)
{   
    char* buffer;
    if ((buffer = read_file_into_buffer(LOGIN_COOKIE_FILE)) == NULL) {
        return;
    }    

    cJSON* json = cJSON_Parse(buffer);
    if (json == NULL || (cJSON_IsArray(json) == false)) {
        printf("parse_cookies: cJSON_Parse returned invalid json obj \n");
        free(buffer); buffer = NULL;
        return;
    }

    cJSON *array_item;
    cJSON_ArrayForEach (array_item, json) {
        const cJSON* name = cJSON_GetObjectItem(array_item, "name");
        const cJSON* value = cJSON_GetObjectItem(array_item, "value"); 
        const cJSON* expirationDate = cJSON_GetObjectItem(array_item, "expirationDate");

        if (json_string_is_valid(name) && cookie_is_needed(name->valuestring) && json_string_is_valid(value) && json_number_is_valid(expirationDate) && (expired(expirationDate->valuedouble) == false)) {
            Cookie* cookie = create_cookie(name->valuestring, value->valuestring, expirationDate->valuedouble);
            if (cookie_is_ready(cookie)) {
                add_cookie_to_jar(cookie, jar);
            }
        }
    }

    free(buffer); buffer = NULL;
    cJSON_Delete(json); json = NULL;
}

void configure_cookie_line(Cookie** jar, const size_t n, char cookie_line[n])
{
    const char* cookie_seperator = "; ";

    size_t char_offset = 0, cookies_added = 0;

    Cookie* cookie, *tmp;
    HASH_ITER(hh, *jar, cookie, tmp) {
        const size_t char_written = snprintf((cookie_line + char_offset), (n - char_offset - 1), 
                                "%s%s=%s", (cookies_added > 0 ? cookie_seperator : ""), cookie->name, cookie->value);
        
        const size_t new_char_offset = char_offset + char_written;

        if ((char_written < 0) || (new_char_offset >= n)) {
            printf("configure_cookie_line: %zu is not enough chars (terminated at %zu)\n", n, new_char_offset);
            cookie_line[0] = '\0';
            return;
        }

        char_offset = new_char_offset;
        cookies_added++;
    }

    cookie_line[char_offset] = '\0';
}

int configure_post_header(const size_t n, char post_header[n], const char *host, const char *path, const char* cookie, const char* authorization, const size_t post_body_length)
{
    return snprintf(post_header, n,
            "POST %s HTTP/1.1\r\n"
            "Host: %s\r\n"
            "User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:125.0) Gecko/20100101 Firefox/125.0\r\n"
            "Authorization: %s\r\n"
            "Content-Type: application/json\r\n"
            "Accept: application/json\r\n"
            "Origin: https://www.youtube.com\r\n"
            "Referer: https://www.youtube.com/\r\n"
            "X-Goog-AuthUser: 0\r\n"
            "Content-Length: %zu\r\n"
            "Connection: keep-alive\r\n"
            "Cookie: %s\r\n"
            "\r\n",
            path, host, authorization, post_body_length, cookie);
}

size_t configure_post_body(const size_t n, char post_body[n])
{
    return snprintf(post_body, n - 1,
        "{\n"
        "  \"context\": {\n"
        "    \"client\": {\n"
        "      \"hl\": \"en\",\n"
        "      \"gl\": \"US\",\n"
        "      \"clientName\": \"WEB\",\n"
        "      \"clientVersion\": \"2.20240430.09.00\",\n"
        "      \"clientFormFactor\": \"UNKNOWN_FORM_FACTOR\",\n"
        "      \"browserName\": \"Firefox\",\n"
        "      \"browserVersion\": \"125.0\",\n"
        "      \"osName\": \"Windows\",\n"
        "      \"osVersion\": \"10.0\",\n"
        "      \"platform\": \"DESKTOP\"\n"
        "    },\n"
        "    \"user\": {\n"
        "      \"lockedSafetyMode\": false\n"
        "    },\n"
        "    \"request\": {\n"
        "      \"useSsl\": true,\n"
        "      \"internalExperimentFlags\": [],\n"
        "      \"consistencyTokenJars\": []\n"
        "    }\n"
        "  },\n"
        "  \"browseId\": \"FEwhat_to_watch\"\n"
        "}"
    );  
}

void configure_authorization_line(const char* origin, const char* SAPISID, const size_t n, char authorization_line[n])
{
    if (SAPISID == NULL) {
        printf("configure_authorization_line: SAPISID is NULL\n");
        authorization_line[0] = '\0';
        return;
    }

    const time_t timestamp = time(NULL);
    
    char input[64];
    snprintf(input, sizeof(input), "%ld %s %s", timestamp, origin, SAPISID);

    unsigned char hash[SHA_DIGEST_LENGTH];  
    SHA1((unsigned char *) input, strlen(input), hash);

    char hexed_hash[(SHA_DIGEST_LENGTH * 2) + 1];
    for (int i = 0; i < SHA_DIGEST_LENGTH; i++) {
        sprintf(hexed_hash + (i * 2), "%02x", hash[i]);
    }
    
    hexed_hash[SHA_DIGEST_LENGTH * 2] = '\0';    

    snprintf(authorization_line, n, "SAPISIDHASH %ld_%s", timestamp, hexed_hash);
}

int main()
{
    ctx = SSL_CTX_new(TLS_client_method());
    if (!ctx) {
        printf("error initalizing SSL_CTX object\n");
        return 1;
    } 

    Cookie* cookie_jar = NULL;

    parse_login_cookies(&cookie_jar);
    char cookie_line[2048];
    configure_cookie_line(&cookie_jar, sizeof(cookie_line), cookie_line);
    
    char authorization_line[1024];
    Cookie* SAPISID_cookie = find_cookie_in_jar("SAPISID", &cookie_jar); 
    if (SAPISID_cookie == NULL) {
        return 1;
    }

    configure_authorization_line("www.youtube.com", SAPISID_cookie->value, sizeof(authorization_line), authorization_line);

    PreparedRequest post = {0};
    configure_post_body(sizeof(post.body), post.body);
    configure_post_header(sizeof(post.header), post.header, "www.youtube.com", "/youtubei/v1/browse?key=AIzaSyAO_FJ2SlqU8Q4STEHLGCilw_Y9_11qcW8", cookie_line, authorization_line, strlen(post.body));

    PersistentConnection connection;
    init_persistent_connection(&connection, "www.youtube.com", HTTPS);

    connection.connected = establish_connection(&connection);
    if (connection.connected == false) {
        return 1;
    }

    printf("%s\n%s\n", post.header, post.body);

    HTTPS_Response response = send_https_request(post, &connection);
    if (https_response_ready(&response)) {
        create_file_from_memory("header.txt", response.header);
        create_file_from_memory("body.json", response.body);
        free_https_response(&response);
    }

    empty_cookie_jar(&cookie_jar);
    
    SSL_CTX_free(ctx);
    free_persistent_connection(&connection);
    
    return 0;
}