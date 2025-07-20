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

#include "raylib.h"
#include "raylib/src/raylib.h"
#define RAYGUI_IMPLEMENTATION
#include "raygui.h"

#define MAX_THREADS 4

int bound_index_to_array (const int pos, const int array_size)
{
    return (pos + array_size) % array_size;
}

typedef struct
{
	double start_time;
	double life_time; // duration in seconds
} Timer;

void start_timer(Timer *timer, const double lifetime) 
{
    if (!timer) {
        printf("start_timer: 'timer' arg is NULL\n");
        return;
    }

    else if (lifetime < 0) {
        printf("start_timer: lifetime is negative\n");
        return;
    }

	timer->start_time = GetTime();
	timer->life_time = lifetime;
}

bool timer_done(Timer timer)
{ 
    const double elapsed = GetTime() - timer.start_time;
	return elapsed >= timer.life_time; 
} 

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

// availible forms of content that youtube provides
typedef enum
{
    ANY,
    VIDEO,
    CHANNEL,
    PLAYLIST,
    LIVE,
    UNDF,
} MediaType; 
#define N_MEDIA_TYPES 5

char* media_type_to_url(const MediaType media_type)
{
    switch (media_type) {
        case VIDEO: return "SAhAB";
        case CHANNEL: return "SAhAC";
        case PLAYLIST: return "SAhAD";
        case LIVE: return "SBBABQAE";
        case ANY: return "%253D";
        default:
            printf("media_type_to_url: passed MediaType is invalid\n");
            return NULL;
    }
}

char* media_type_to_host(const MediaType media_type)
{
    switch (media_type) {
        case PLAYLIST:
        case LIVE:
        case VIDEO: return "i.ytimg.com";
        case CHANNEL: return "yt3.ggpht.com";
        case ANY: return "www.youtube.com";
        default:
            printf("media_type_to_host: passed MediaType is invalid\n");
            return NULL; 
    }
}

char* media_type_to_text(const MediaType media_type)
{
    switch (media_type) {
        case VIDEO: return "VIDEO";
        case CHANNEL: return "CHANNEL";
        case PLAYLIST: return "PLAYLIST";
        case LIVE: return "LIVE";
        case ANY: return "ANY";
        case UNDF: return "UNDF";
        default:
            printf("media_type_to_text: passed MediaType is invalid\n");
            return NULL; 
    }

    return NULL;
}

// availible sorting types youtube provides 
typedef enum 
{
    BY_RELEVANCE,
    BY_UPLOAD_DATE,
    BY_VIEW_COUNT,
    BY_RATING,
} SortType; 
#define N_SORT_TYPES 4

char* sort_type_to_url(const SortType sort_type)
{
    switch (sort_type) {
        case BY_RELEVANCE: return "CAA";
        case BY_UPLOAD_DATE: return "CAI";
        case BY_VIEW_COUNT: return "CAM";
        case BY_RATING: return "CAE";
        default:
            printf("sort_type_to_url: passed SortType is invalid\n");
            return NULL;
    }
}

char* sort_type_to_text(const SortType sort_type)
{
    switch (sort_type) {
        case BY_RELEVANCE: return "Relevence";
        case BY_UPLOAD_DATE: return "Upload Date";
        case BY_VIEW_COUNT: return "Views"; 
        case BY_RATING: return "Rating";
        default:
            printf("sort_type_to_text: passed SortType is invalid\n");
            return NULL;
    }
}

// search result entry containing media metadata and thumbnail reference.
typedef struct SearchResult
{
    MediaType media_type;               

    char id[64];                // used to identify the availible media types                 
    char title[256];            // name of the content           
    char author[128];           // creator of video, livestream or playlist         
    char subscriber_count[16];  // X.XX k/M/B formatted   
    char view_count[16];        // ^         
    char date_published[32];    // 'X years/months/weeks/seconds ago'    
    char duration[16];          // HH:MM:SS formatted           
    char video_count[32];       // # of videos that a playlist contains        
    char thumbnail_path[256];   // path to thumbnail link, relative to its host (see media type to host)    
    bool thumbnail_loaded;

    struct SearchResult* next; 
} SearchResult;

SearchResult* init_search_result()
{
    SearchResult *search_result = (SearchResult*) malloc(sizeof(SearchResult));
    if (search_result == NULL) {
        printf("init_search_result: malloc returned NULL\n");
        return NULL;
    }

    search_result->media_type = UNDF;
    search_result->thumbnail_loaded = false;
    memset(search_result->id, 0, sizeof(search_result->id));
    memset(search_result->title, 0, sizeof(search_result->title));
    memset(search_result->author, 0, sizeof(search_result->author));
    memset(search_result->duration, 0, sizeof(search_result->duration));
    memset(search_result->view_count, 0, sizeof(search_result->view_count));
    memset(search_result->video_count, 0, sizeof(search_result->video_count));
    memset(search_result->thumbnail_path, 0, sizeof(search_result->thumbnail_path));
    memset(search_result->date_published, 0, sizeof(search_result->date_published));
    memset(search_result->subscriber_count, 0, sizeof(search_result->subscriber_count));

    return search_result;
}

void free_search_result(SearchResult *search_result)
{
    if (!search_result) return;
    free(search_result);
}

void print_search_result(const SearchResult *search_result) 
{
    printf("id) %s title) %s author) %s subs) %s views) %s date) %s length) %s video count) %s type) %d thumbnail) %s\n", 
            search_result->id, search_result->title, search_result->author, search_result->subscriber_count, search_result->view_count, search_result->date_published, search_result->duration, search_result->video_count, search_result->media_type, search_result->thumbnail_path);
}

// linked list of search results returned from a query
typedef struct
{
    size_t count;           
    SearchResult* head;    
    SearchResult* tail;     
    char continuation_token[2048];
} Results;

Results init_results() 
{
    Results search_results;
    search_results.head = search_results.tail = NULL;
    search_results.count = 0;
    memset(search_results.continuation_token, 0, sizeof(search_results.continuation_token));
    return search_results;
}

void add_search_result(Results *results, SearchResult *search_result)
{
    if (!results) {
        printf("add_search_result: 'results' arg is NULL\n");
        return;
    }

    else if (!search_result) {
        printf("add_search_result: 'search_result' arg is NULL\n");
        return;
    }

    search_result->next = NULL;

    if (results->count == 0) 
        results->head = results->tail = search_result;

    else {
        results->tail->next = search_result;
        results->tail = results->tail->next;
    }

    results->count++;
}

void free_results(Results *results) 
{
    if (!results) return;

    while (results->head) {
        SearchResult *to_free = results->head;
        results->head = results->head->next;
        free_search_result(to_free);
    }

    results->head = results->tail = NULL;
    results->count = 0;
}

void print_results(const Results* results)
{
    for (SearchResult *current = results->head; current != NULL; current = current->next) {
        print_search_result(current);
    } 
}

typedef enum
{
    NEW,
    APPENDING,
    TRENDING,
} SearchType;

// represents user-defined parameters for a YouTube search request
typedef struct
{
    bool allow_youtube_shorts;      
    char string[256];        
    MediaType media;          
    SortType sort;
    SearchType search_type;    
} Query;

// holds raw thumbnail image data fetched from an HTTP request
// intended for later conversion to a Texture (see LoadTextureFromMemory in raylib)
typedef struct RawThumbnail
{
    Buffer data;              
    char search_result_id[256];     
    struct RawThumbnail *next;
} RawThumbnail;

void free_raw_thumbnail(RawThumbnail *raw_thumbnail)
{
    if (!raw_thumbnail) return;
    if (buffer_ready(&raw_thumbnail->data)) free_buffer(&raw_thumbnail->data);
    free(raw_thumbnail);
}

// thread-safe queue for storing in-memory thumbnail data. 
// supports appending from a background thread and consuming from the main thread
typedef struct 
{
    size_t count;
    RawThumbnail *head;
    RawThumbnail *tail;  
    pthread_mutex_t mutex;
} RawThumbnailQueue;

RawThumbnailQueue init_thumbnail_queue()
{
    RawThumbnailQueue thumbnail_queue;
    thumbnail_queue.count = 0;
    thumbnail_queue.head = thumbnail_queue.tail = NULL;
    pthread_mutex_init(&thumbnail_queue.mutex, NULL);
    return thumbnail_queue;
}

void enqueue_thumbnail(RawThumbnailQueue *thumbnail_queue, RawThumbnail *thumbnail_data) 
{
    if (!thumbnail_queue) {
        printf("enqueue_thumbnail: 'thumbnail_queue' arg is NULL\n");
        return;
    }

    else if (!thumbnail_data) {
        printf("enqueue_thumbnail: 'thumbnail_data' arg is NULL\n");
        return;
    }

    thumbnail_data->next = NULL;

    if (thumbnail_queue->count == 0) 
        thumbnail_queue->head = thumbnail_queue->tail = thumbnail_data;
    else {
        thumbnail_queue->tail->next = thumbnail_data;
        thumbnail_queue->tail = thumbnail_data;
    }

    thumbnail_queue->count++;
}

RawThumbnail* dequeue_thumbnail(RawThumbnailQueue *thumbnail_queue)
{
    if (!thumbnail_queue) {
        printf("dequeue_thumbnail: 'thumbnail_queue' arg is NULL\n");
        return NULL;
    }

    if (thumbnail_queue->count == 0) {
        printf("dequeue_thumbnail: 'thumbnail_queue' arg is empty\n");
        return NULL;
    }

    RawThumbnail *ret = thumbnail_queue->head;

    thumbnail_queue->head = ret->next;
    if (!thumbnail_queue->head) {
        thumbnail_queue->tail = NULL;
    }

    thumbnail_queue->count--;

    return ret;
}

void free_thumbnail_queue(RawThumbnailQueue *thumbnail_queue)
{
    if (!thumbnail_queue) return;
    while (thumbnail_queue->head) free_raw_thumbnail(dequeue_thumbnail(thumbnail_queue));
    pthread_mutex_destroy(&thumbnail_queue->mutex);
}

// read one line from ssl stream or n bytes into buffer (whichever comes first)
size_t ssl_read_line(SSL *ssl, char *buffer, const size_t n) 
{
    if (!buffer) {
        printf("ssl_read_line: buffer is NULL\n");
        return 0;
    }
    
    const char *CRLF = "\r\n";
    const size_t CRLF_len = strlen(CRLF);

    size_t pos = 0;
    char c;

    while (pos < n - 1) {
        // read one char
        int byte = SSL_read(ssl, &c, 1);
        if (byte <= 0) {
            printf("ssl_read_line: SSL_read returned %d\n", byte);
            return 0;
        }

        // add character to buffer
        buffer[pos++] = c;

        // checking if we've reached end of line
        if ((pos >= CRLF_len) && strstr(buffer, CRLF)) {
            break;
        }
    }

    buffer[pos] = '\0';
    
    return pos;
}

size_t read_header(SSL *ssl, char *header, size_t n)
{
    size_t total_len = 0;
    const char *header_end = "\r\n\r\n";

    while ((strstr(header, header_end) == NULL) && (total_len < n - 1)) {
        const size_t len = ssl_read_line(ssl, (header + total_len), (n - total_len));
        if (len == 0) {
            printf("read_header: read_line returned 0 bytes read\n");
            break;  
        }

        total_len += len;
    }   

    return total_len;
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

typedef struct
{
    char path[256];
    char body[2048];
    char header[1024];
} PreparedRequest;

bool header_contains_tag(const char *header, const char *tag)
{
    return strstr(header, tag);
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
    PersistentConnection connections[N_CONN];
    size_t current_conn;
} ConnectionPool;

ConnectionPool init_connection_pool(const char* host)
{
    ConnectionPool pool = {0};
    for (int c = 0; c < N_CONN; c++) init_persistent_connection(&pool.connections[c], host, HTTPS);
    return pool;
}

void free_connection_pool(ConnectionPool* connection_pool)
{
    if (connection_pool == NULL) return;
    for(int c = 0; c < N_CONN; c++) free_persistent_connection(&connection_pool->connections[c]);
}

void cycle_connection(ConnectionPool* connection_pool)
{
    connection_pool->current_conn = bound_index_to_array((connection_pool->current_conn + 1), N_CONN);
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

Buffer send_https_request(const PreparedRequest request, PersistentConnection *connection)
{
    pthread_mutex_lock(&connection->mutex);

    if (connected_to_wifi() == false) {
        printf("send_https_request: not connected to the wifi\n");
        connection->connected = false;
        pthread_mutex_unlock(&connection->mutex);
        return (Buffer){0};
    }

    if (connection->connected == false) {
        connection->connected = establish_connection(connection);
        if (connection->connected == false) {
            printf("send_https_request: failed to establish connection\n");
            pthread_mutex_unlock(&connection->mutex);
            return (Buffer){0};
        }
    }

    // connection->connected = establish_connection(connection);
    // if (connection->connected == false) {
    //     printf("send_https_request: failed connection attempt\n");
    //     pthread_mutex_unlock(&connection->mutex);
    //     return (Buffer){0};
    // }

    int header_write_status = SSL_write(connection->ssl, request.header, strlen(request.header));
    if (header_write_status <= 0) {
        printf("send_https_request: SSL_write (header) failed, returned %d\n", header_write_status);
        connection->connected = false;
        pthread_mutex_unlock(&connection->mutex);
        return (Buffer){0};
    } 

    if (request.body[0] != '\0') {
        int body_write_status = SSL_write(connection->ssl, request.body, strlen(request.body));
        if (body_write_status <= 0) {
            printf("send_https_request: SSL_write (body) failed, returned %d\n", body_write_status);
            connection->connected = false;
            pthread_mutex_unlock(&connection->mutex);
            return (Buffer){0};
        }
    }

    char header[4096] = {0};
    size_t header_len = read_header(connection->ssl, header, sizeof(header));
    header[header_len] = '\0';
    if (header_len == 0) {
        printf("send_https_request: failed to read header from ssl stream\n");
        connection->connected = false;
        pthread_mutex_unlock(&connection->mutex);
        return (Buffer){0};
    }

    char https_request_code[4];
    get_https_request_code(header, sizeof(https_request_code), https_request_code);
    if (https_request_code_is_valid(https_request_code) == false) {
        printf("send_https_request: invalid https request code (%s)\n", https_request_code);
        connection->connected = false;
        pthread_mutex_unlock(&connection->mutex);
        return (Buffer){0};
    }

    Buffer response = init_buffer();

    if (header_contains_tag(header, "Content-Length:")) {
        size_t content_length = get_content_len_from_header(header);
        if (content_length > 0) {
            ssl_read_n(connection->ssl, &response, content_length);
        }

        else {
            printf("send_https_request: invalid content length read from header\n");
            connection->connected = false;
            pthread_mutex_unlock(&connection->mutex);
            return (Buffer){0};
        }
    }

    else if (header_contains_tag(header, "Transfer-Encoding: chunked")) {
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
                return (Buffer){0};
            }

            hex[len - crlf_len] = '\0';

            chunk_size = strtol(hex, NULL, 16);
            ssl_read_n(connection->ssl, &response, chunk_size);

            char trailing_crlf[16];
            ssl_read_line(connection->ssl, trailing_crlf, sizeof(trailing_crlf));
        }
    }

    pthread_mutex_unlock(&connection->mutex);

    return response;
}

int configure_get_header(const size_t n, char get_header[n], const char *host, const char *path)
{
    int chars_written = snprintf(get_header, n,
        "GET %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        // "User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:125.0) Gecko/20100101 Firefox/125.0\r\n"
        "User-Agent: CustomClient/1.0\r\n"
        "Connection: keep-alive\r\n"
        // "Connection: close\r\n"
        "\r\n",
        path, host);

    if (chars_written >= n) {
        printf("configure_get_request: buffer is too small (%d bytes needed)\n", chars_written);
        return -1;
    }

    else return chars_written;
}

int configure_post_header(const size_t n, char post_header[n], const char *host, const char *path, const size_t post_body_length)
{
    return snprintf(post_header, n,
            "POST %s HTTP/1.1\r\n"
            "Host: %s\r\n"
            "User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:125.0) Gecko/20100101 Firefox/125.0\r\n"
            "Content-Type: application/json\r\n"
            "Accept: application/json\r\n"
            "Content-Length: %zu\r\n"
            "Connection: keep-alive\r\n"
            // "Connection: close\r\n"
            "\r\n",
            path, host, post_body_length);
}

size_t configure_post_body(const size_t n, char post_body[n], const Query query, const char* continuation_token)
{
    size_t body_len = snprintf(post_body, n - 1,
                        "{\n"
                        "  \"context\": {\n"
                        "    \"client\": {\n"
                        "      \"clientName\": \"WEB\",\n"
                        "      \"clientVersion\": \"2.20210721.00.00\"\n"
                        "    }\n"
                        "  },\n");  

    switch (query.search_type) {
        case NEW: {
            char params[16];
            const char* sort_url = sort_type_to_url(query.sort);
            const char* media_url = media_type_to_url(query.media);
            snprintf(params, sizeof(params), "%s%s", sort_url, media_url);

            body_len += snprintf(post_body + body_len, n - body_len,
                    "  \"params\": \"%s\",\n"
                            "  \"query\": \"%s\"\n", params, query.string);
            break;
        }

        case APPENDING: 
            body_len += snprintf(post_body + body_len, n - body_len,
                    "  \"continuation\": \"%s\"\n", continuation_token);
            break;

        case TRENDING: 
            body_len += snprintf(post_body + body_len, n - body_len,
                    "  \"browseId\": \"FEtrending\"\n");
            break;
    }

    strcat(post_body, "}");
    
    return body_len + 1;
}

void remove_leading_whitespace(char *string)
{
    if (!string) {
        printf("remove_trailing_whitespace: string is NULL\n");
        return;
    }

    size_t n = strlen(string);
    if (n == 0) {
        printf("remove_trailing_whitespace: string is empty\n");
        return;
    }

    // move ptr to the first nonwhitespace character
    char *ptr = string;
    while (ptr && isspace((unsigned char)*ptr)) {
        ptr++;
    }

    // write elements over leading whitespace
    if (*ptr != '\0') {
        memmove(string, ptr, n + 1);
    }

    else *string = '\0';
}

void remove_trailing_whitespace(char *string)
{
    if (!string) {
        printf("remove_trailing_whitespace: string is NULL\n");
        return;
    }

    size_t n = strlen(string);
    if (n == 0) {
        printf("remove_trailing_whitespace: string is empty\n");
        return;
    }

    // move ptr to last nonwhitespace char
    char *last_char = string + n - 1;
    while (last_char && isspace((unsigned char)*last_char)) {
        last_char--;
    }

    // end the string at the next whitespace
    *(last_char + 1) = '\0';
}

void format_view_count(char* view_count)
{
    if (!view_count) {
        printf("format_view_count: string arg is NULL\n");
        return;
    }

    // need to extract the real numbers
    char no_commas[12] = {0};

    int k = 0;
    for(int i = 0; view_count[i] != '\0'; i++) {
        if (isdigit(view_count[i])) {
            no_commas[k++] = view_count[i];
        }
    }

    no_commas[k] = '\0';

    // find the int representation
    const float raw_view_count = strtof(no_commas, NULL);

    // format view string
    int chars_written;
    if (raw_view_count < 1e3) // 0 - 999
        chars_written = sprintf(view_count, "%d", (int)raw_view_count);
    else if (raw_view_count < 1e4) // 1,000 - 9,999
        chars_written = sprintf(view_count, "%.2fk", (raw_view_count / 1e3));
    else if (raw_view_count < 1e5) // 10,000 - 99,999
        chars_written = sprintf(view_count, "%.1fk", (raw_view_count / 1e3));
    else if (raw_view_count < 1e6) // 100,009 - 999,999
        chars_written = sprintf(view_count, "%.0fk", (raw_view_count / 1e3));
    else if (raw_view_count < 1e7) // 1,000,000 - 9,999,999
        chars_written = sprintf(view_count, "%.2fM", (raw_view_count / 1e6));
    else if (raw_view_count < 1e8) // 10,000,000 - 99,999,999
        chars_written = sprintf(view_count, "%.1fM", (raw_view_count / 1e6));
    else if (raw_view_count < 1e9) // 100,000,000 - 999,999,999
        chars_written = sprintf(view_count, "%.0fM", (raw_view_count / 1e6));
    else if (raw_view_count < 1e10) // 1,000,000,000 - 9,999,999,999
        chars_written = sprintf(view_count, "%.2fB", (raw_view_count / 1e9));
    else if (raw_view_count < 1e11) // 10,000,000,000 - 99,999,999,999
        chars_written = sprintf(view_count, "%.1fB", (raw_view_count / 1e9));
    else if (raw_view_count < 1e12) // 100,000,000,000 - 999,999,999,999
        chars_written = sprintf(view_count, "%.0fB", (raw_view_count / 1e9));
    
    // remove ".0"
    char *reduntant = strstr(view_count, ".0");
    if (reduntant) {
        char *letter_char = view_count + chars_written - 1;
        memmove(reduntant, letter_char, 2);
    }
}

typedef struct 
{
    char search_result_id[64];
    PreparedRequest request;
    PersistentConnection *connection;
    RawThumbnailQueue *thumbnail_queue;
} LoadThumbnailArgs;

LoadThumbnailArgs* create_load_thumbnail_args(const char* id, PreparedRequest request, PersistentConnection* connection, RawThumbnailQueue* thumbnail_queue)
{
    LoadThumbnailArgs* targs = (LoadThumbnailArgs*) malloc(sizeof(LoadThumbnailArgs));
    if (targs == NULL) {
        printf("create_load_thumbnail_args: malloc returned NULL for 'targs'\n");
        return NULL;
    }

    strncpy(targs->search_result_id, id, sizeof(targs->search_result_id) - 1);
    targs->request = request;
    targs->connection = connection;
    targs->thumbnail_queue = thumbnail_queue;

    return targs;
}

void* load_thumbnail(void *args)
{
    LoadThumbnailArgs *targs = (LoadThumbnailArgs*) args;
    
    Buffer thumbnail_buffer = send_https_request(targs->request, targs->connection);
    if (buffer_ready(&thumbnail_buffer) == false) {
        printf("load_thumbnail: send_http_request returned invalid buffer\n");
        free(targs);
        return NULL;
    }

    RawThumbnail *thumbnail_data = malloc(sizeof(RawThumbnail));
    if (thumbnail_data == NULL) {
        printf("load_thumbnail: malloc returned NULL for thumbnail_data\n");
        free(targs);
        return NULL;
    }

    thumbnail_data->data = thumbnail_buffer;
    strcpy(thumbnail_data->search_result_id, targs->search_result_id);

    pthread_mutex_lock(&targs->thumbnail_queue->mutex);
    enqueue_thumbnail(targs->thumbnail_queue, thumbnail_data);
    pthread_mutex_unlock(&targs->thumbnail_queue->mutex);

    free(targs);
    return NULL;
}

typedef struct ThreadTask
{
    void *(*funct)(void *);
    void *args;
    struct ThreadTask *next;
} ThreadTask;

typedef struct
{
    ThreadTask *head;
    ThreadTask *tails;
    size_t count;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
} TaskQueue;

TaskQueue init_task_queue()
{
    TaskQueue tq;
    tq.count = 0;
    tq.head = tq.tails = NULL;
    pthread_mutex_init(&tq.mutex, NULL);
    pthread_cond_init(&tq.cond, NULL);
    return tq;
}

void enqueue_task(ThreadTask *task, TaskQueue *queue)
{
    if (!task) {
        printf("enqueue_task: Task arg is NULL\n");
        return;
    }

    else if (!queue) {
        printf("enqueue_task: TaskQueue arg is NULL\n");
        return;
    }
    
    if (queue->count == 0 || !queue->head) 
        queue->head = queue->tails = task;
    else {
        task->next = NULL;
        queue->tails->next = task;
        queue->tails = task;
    }

    queue->count++;
}

ThreadTask* dequeue_task(TaskQueue *queue)
{
    if (!queue) {
        printf("dequeue_task: TaskQueue arg is NULL\n");
        return NULL;
    }

    if (queue->count == 0 || !queue->head) {
        printf("dequeue_task: TaskQueue arg is empty\n");
        return NULL;
    }

    ThreadTask *ret = queue->head;
    queue->head = queue->head->next;
    queue->count--;
    
    return ret; 
}

void free_task_queue(TaskQueue *queue)
{
    while (queue->head) free(dequeue_task(queue));
    pthread_mutex_destroy(&queue->mutex);
    pthread_cond_destroy(&queue->cond);
}

bool application_running = true;
void* worker_thread_funct(void* args)
{
    TaskQueue* task_queue = (TaskQueue*)args;
    
    while (application_running) {
        pthread_mutex_lock(&task_queue->mutex);
        while ((task_queue->count == 0) && application_running) 
            pthread_cond_wait(&task_queue->cond, &task_queue->mutex);

        if (application_running == false) {
            pthread_mutex_unlock(&task_queue->mutex);
            break;
        }

        ThreadTask *task = dequeue_task(task_queue);
        pthread_mutex_unlock(&task_queue->mutex); 
        task->funct(task->args);
        free(task);
    }

    return NULL;
}

void init_thread_pool(const size_t nthreads, pthread_t thread_pool[nthreads], void* (*worker_funct)(void*), void* worker_args)
{
    for (int t = 0; t < nthreads; t++) 
        pthread_create(&thread_pool[t], NULL, worker_funct, worker_args);
}

void free_thread_pool(const size_t nthreads, pthread_t thread_pool[nthreads])
{
    for (int t = 0; t < nthreads; t++) 
        pthread_join(thread_pool[t], NULL);
}

typedef struct
{
    bool allow_youtube_shorts;
    SearchType search_type;
    PreparedRequest request;
    Results *search_results;
    RawThumbnailQueue *thumbnail_queue;
    PersistentConnection *youtube_connection;
} SearchThreadArgs;

SearchThreadArgs* create_search_thread_args(const Query query, PreparedRequest request, Results* search_results, RawThumbnailQueue* thumbnail_queue, PersistentConnection* youtube_connection)
{
    SearchThreadArgs* search_thread_args = (SearchThreadArgs*) malloc(sizeof(SearchThreadArgs));
    if (search_thread_args == NULL) {
        printf("create_search_thread_args: malloc returned NULL for 'search_thread_args'\n");
        return NULL;
    }

    search_thread_args->allow_youtube_shorts = query.allow_youtube_shorts;
    search_thread_args->search_type = query.search_type;
    search_thread_args->request = request;
    search_thread_args->search_results = search_results;
    search_thread_args->thumbnail_queue = thumbnail_queue;
    search_thread_args->youtube_connection = youtube_connection;

    return search_thread_args;
}

cJSON* find_object(cJSON* root, const char *key)
{
    if ((root == NULL) || (key == NULL)) {
        return NULL;
    }

    else if (root->string && (strcmp(root->string, key) == 0)) {
        return root;
    }

    if ((root->type == cJSON_Object) || (root->type == cJSON_Array)) {
        for (cJSON* child = root->child; child; child = child->next) {
            cJSON* found = find_object(child, key);
            if (found) {
                return found;
            }
        }
    }

    return NULL;
}

void get_continuation_token(cJSON *json, const SearchType search_typ, const size_t n, char continuation_token[n])
{
    cJSON* continuationEndpoint = find_object(json, "continuationEndpoint");
    cJSON* continuationCommand = continuationEndpoint ? cJSON_GetObjectItem(continuationEndpoint, "continuationCommand") : NULL;
    cJSON* token = continuationCommand ? cJSON_GetObjectItem(continuationCommand, "token") : NULL;
    
    if ((token == NULL) || (cJSON_IsString(token) == false)) {
        printf("get_continuation_token: failed to extract token\n");
        memset(continuation_token, 0, n);
        return;
    }

    strncpy(continuation_token, token->valuestring, n - 1);
}

static int elements_added = 0; 
static bool delete_old_nodes = false;
static bool search_finished = true;

bool video_is_youtube_short(const cJSON *videoRenderer) 
{
    cJSON *navigationEndpoint = cJSON_GetObjectItem(videoRenderer, "navigationEndpoint");
    cJSON *commandMetadata = navigationEndpoint ? cJSON_GetObjectItem(navigationEndpoint, "commandMetadata") : NULL;
    cJSON *webCommandMetadata = commandMetadata ? cJSON_GetObjectItem(commandMetadata, "webCommandMetadata") : NULL;
    cJSON *url = webCommandMetadata ? cJSON_GetObjectItem(webCommandMetadata, "url") : NULL;
    if (url && cJSON_IsString(url)) {
        return strstr(url->valuestring, "/shorts");
    }

    return false;
}

bool video_is_live(const cJSON* videoRenderer)
{
    cJSON *badges = videoRenderer ? cJSON_GetObjectItem(videoRenderer, "badges") : NULL;
    cJSON *arrayItem = (badges && cJSON_IsArray(badges)) ? cJSON_GetArrayItem(badges, 0) : NULL;
    cJSON *metadataBadgeRenderer = arrayItem ? cJSON_GetObjectItem(arrayItem, "metadataBadgeRenderer") : NULL;
    cJSON *label = metadataBadgeRenderer ? cJSON_GetObjectItem(metadataBadgeRenderer, "label") : NULL;
    if (label && cJSON_IsString(label)) {
        return strcmp("LIVE", label->valuestring) == 0;
    }

    return false;
}

void parse_video_from_json(const cJSON* videoRenderer, const bool allow_youtube_shorts, SearchResult* video)
{
    if (video == NULL) {
        printf("parse_video_from_json: 'video' is NULL\n");
        return;
    }

    else if (videoRenderer == NULL) {
        printf("parse_video_from_json: 'videoRenderer' is NULL\n");
        video->media_type = UNDF;
        return;
    }

    if ((allow_youtube_shorts == false) && (video_is_youtube_short(videoRenderer) == true)) {
        video->media_type = UNDF;
        return;
    }

    // ID
    cJSON *videoId = cJSON_GetObjectItem(videoRenderer, "videoId");
    if (videoId && videoId->valuestring && videoId->valuestring[0] != '\0') {
        strncpy(video->id, videoId->valuestring, sizeof(video->id) - 1);
    }

    else {
        printf("parse_video_from_json: failed to parse video id\n");
        video->media_type = UNDF;
        return;
    }

    const char *default_value = "(null)";

    // TITLE
    cJSON *title = cJSON_GetObjectItem(videoRenderer, "title");
    cJSON* runs = title ? cJSON_GetObjectItem(title, "runs") : NULL;
    cJSON *arrayItem = (runs && cJSON_IsArray(runs)) ? cJSON_GetArrayItem(runs, 0) : NULL;
    cJSON *text = arrayItem ? cJSON_GetObjectItem(arrayItem, "text") : NULL;
    if (text && cJSON_IsString(text)) {
        strncpy(video->title, text->valuestring, sizeof(video->title) - 1);
    }

    else {
        printf("parse_video_from_json: failed to parse title\n");
        strncpy(video->title, default_value, sizeof(video->title) - 1);
    }

    // THUMBNAIL PATH
    snprintf(video->thumbnail_path, sizeof(video->thumbnail_path), "/vi/%s/mqdefault.jpg", video->id);

    // AUTHOR
    cJSON *ownerText = cJSON_GetObjectItem(videoRenderer, "ownerText");
    runs = ownerText ? cJSON_GetObjectItem(ownerText, "runs") : NULL;
    arrayItem = (runs && cJSON_IsArray(runs)) ? cJSON_GetArrayItem(runs, 0) : NULL;
    text = arrayItem ? cJSON_GetObjectItem(arrayItem, "text") : NULL;
    if (text && cJSON_IsString(text)) {
        strncpy(video->author, text->valuestring, sizeof(video->author) - 1);
    }

    else {
        printf("parse_video_from_json: failed to parse author\n");
        strncpy(video->author, default_value, sizeof(video->author) - 1);
    }

    cJSON *viewCountText = cJSON_GetObjectItem(videoRenderer, "viewCountText");

    if (video_is_live(videoRenderer)) {
        video->media_type = LIVE;
        cJSON *runs = viewCountText ? cJSON_GetObjectItem(viewCountText, "runs") : NULL;
        cJSON *arrayItem = (runs && cJSON_IsArray(runs)) ? cJSON_GetArrayItem(runs, 0) : NULL;
        cJSON *text = arrayItem ? cJSON_GetObjectItem(arrayItem, "text") : NULL;
        if (text && cJSON_IsString(text)) {
            strncpy(video->view_count, text->valuestring, sizeof(video->view_count) - 1);
            format_view_count(video->view_count);
        }

        else {
            printf("parse_video_from_json: failed to parse view count (LIVE)\n");
            strncpy(video->view_count, default_value, sizeof(video->view_count) - 1);
        }
    }

    else {
        video->media_type = VIDEO;
        cJSON *simpleText = viewCountText ? cJSON_GetObjectItem(viewCountText, "simpleText") : NULL;
        if (simpleText && cJSON_IsString(simpleText)) {
            strncpy(video->view_count, simpleText->valuestring, sizeof(video->view_count));
            format_view_count(video->view_count);
        }

        else {
            printf("parse_video_from_json: failed to parse view count\n");
            strncpy(video->view_count, default_value, sizeof(video->view_count) - 1);
        }

        // DATE PUBLISHED
        cJSON *publishedTimeText = cJSON_GetObjectItem(videoRenderer, "publishedTimeText");
        simpleText = publishedTimeText ? cJSON_GetObjectItem(publishedTimeText, "simpleText") : NULL;
        if (simpleText && cJSON_IsString(simpleText)) {
            strncpy(video->date_published, simpleText->valuestring, sizeof(video->date_published) - 1);
        }
        
        else {
            printf("parse_video_from_json: failed to parse date published\n");
            strncpy(video->date_published, default_value, sizeof(video->date_published) - 1);
        }
        
        // VIDEO LENGTH
        cJSON *lengthText = cJSON_GetObjectItem(videoRenderer, "lengthText");
        simpleText = lengthText ? cJSON_GetObjectItem(lengthText, "simpleText") : NULL;
        if (simpleText && cJSON_IsString(simpleText)) {
            strncpy(video->duration, simpleText->valuestring, sizeof(video->duration) - 1);
        }
    
        else {
            printf("parse_video_from_json: failed to parse duration\n");
            strncpy(video->duration, default_value, sizeof(video->duration) - 1);
        }
    }
}

void parse_channel_from_json(const cJSON* channelRenderer, SearchResult* channel)
{
    if (channel == NULL) {
        printf("parse_video_from_json: 'video' is NULL\n");
        return;
    }

    else if (channelRenderer == NULL) {
        printf("parse_video_from_json: 'channelRenderer' is NULL\n");
        channel->media_type = UNDF;
        return;
    }

    channel->media_type = CHANNEL;

    // ID
    cJSON* channelId = cJSON_GetObjectItem(channelRenderer, "channelId");
    if (channelId && cJSON_IsString(channelId)) {
        strncpy(channel->id, channelId->valuestring, sizeof(channel->id) - 1);
    }
    
    else {
        printf("parse_channel_from_json: failed to parse channel id\n");
        channel->media_type = UNDF;
        return;
    }

    const char *default_value = "(null)";

    // TITLE
    cJSON *title = cJSON_GetObjectItem(channelRenderer, "title");
    cJSON *simpleText = title ? cJSON_GetObjectItem(title, "simpleText") : NULL;
    if (simpleText && cJSON_IsString(simpleText)) {
        strncpy(channel->title, simpleText->valuestring, sizeof(channel->title) - 1);
    }

    else {
        printf("parse_channel_from_json: failed to parse title\n");
        strncpy(channel->title, default_value, sizeof(channel->title) - 1);
    }

    // SUB COUNT
    cJSON *videoCountText = cJSON_GetObjectItem(channelRenderer, "videoCountText");
    simpleText = videoCountText ? cJSON_GetObjectItem(videoCountText, "simpleText") : NULL;
    if(simpleText && cJSON_IsString(simpleText)) {
        strncpy(channel->subscriber_count, simpleText->valuestring, sizeof(channel->subscriber_count) - 1);
    }

    else {
        // somethimes, the sub count is found here 
        cJSON *subscriberCountText = cJSON_GetObjectItem(channelRenderer, "subscriberCountText");
        simpleText = subscriberCountText ? cJSON_GetObjectItem(subscriberCountText, "simpleText") : NULL;
        if(simpleText && cJSON_IsString(simpleText)) {
            strncpy(channel->subscriber_count, simpleText->valuestring, sizeof(channel->subscriber_count) - 1);
        }   
        
        else {
            printf("parse_channel_from_json: failed to parse sub count\n");
            channel->subscriber_count[0] = '\0';
        }
    }

    // THUMBNAIL PATH
    cJSON *thumbnails = cJSON_GetObjectItem(cJSON_GetObjectItem(channelRenderer, "thumbnail"), "thumbnails");
    cJSON *url = thumbnails && cJSON_IsArray(thumbnails) ? cJSON_GetObjectItem(cJSON_GetArrayItem(thumbnails, 0), "url") : NULL;
    if(url && cJSON_IsString(url)) {
        // the path either starts with '/ytc', or just '/'
        char *path1 = strstr(url->valuestring, "/ytc");
        char *path2 = strrchr(url->valuestring, '/');
        strncpy(channel->thumbnail_path, path1 ? path1 : path2, sizeof(channel->thumbnail_path) - 1);
    }

    else {
        printf("parse_channel_from_json: failed to parse thumbnail path\n");
        channel->thumbnail_path[0] = '\0';
    }
}

void parse_playlist_from_json(const cJSON *lockupViewModel, SearchResult *playlist)
{
    if (playlist == NULL) {
        printf("parse_video_from_json: 'video' is NULL\n");
        return;
    }

    else if (lockupViewModel == NULL) {
        printf("parse_video_from_json: 'lockupViewModel' is NULL\n");
        playlist->media_type = UNDF;
        return;
    }

    playlist->media_type = PLAYLIST;


    // ID
    cJSON *contentId = cJSON_GetObjectItem(lockupViewModel, "contentId");
    if (contentId && cJSON_IsString(contentId)) {
        strncpy(playlist->id, contentId->valuestring, sizeof(playlist->id) - 1);
    }
    
    else {
        printf("parse_playlist_from_json: failed to parse playlist id\n");
        playlist->media_type = UNDF;
        return;
    }

    const char *default_value = "(null)";

    // TITLE
    cJSON *metadata = cJSON_GetObjectItem(lockupViewModel, "metadata");
    cJSON *lockupMetadataViewModel = metadata ? cJSON_GetObjectItem(metadata, "lockupMetadataViewModel") : NULL;
    cJSON *title = lockupMetadataViewModel ? cJSON_GetObjectItem(lockupMetadataViewModel, "title") : NULL;
    cJSON *content = title ? cJSON_GetObjectItem(title, "content") : NULL;

    const bool title_valid = content && cJSON_IsString(content);
    if (title_valid) {
        strncpy(playlist->title, content->valuestring, sizeof(playlist->title) - 1);
    }

    else {
        printf("parse_playlist_from_json: failed to parse title\n");
        strncpy(playlist->title, default_value, sizeof(playlist->title) - 1);
    }
    
    cJSON *contentImage = cJSON_GetObjectItem(lockupViewModel, "contentImage");
    cJSON *collectionThumbnailViewModel = contentImage ? cJSON_GetObjectItem(contentImage, "collectionThumbnailViewModel") : NULL;
    cJSON *primaryThumbnail = collectionThumbnailViewModel ? cJSON_GetObjectItem(collectionThumbnailViewModel, "primaryThumbnail") : NULL;
    cJSON *thumbnailViewModel = primaryThumbnail ? cJSON_GetObjectItem(primaryThumbnail, "thumbnailViewModel") : NULL;

    // THUMBNAIL PATH
    cJSON *rendererContext = cJSON_GetObjectItem(lockupViewModel, "rendererContext");
    cJSON *commandContext = rendererContext ? cJSON_GetObjectItem(rendererContext, "commandContext") : NULL;
    cJSON *onTap = commandContext ? cJSON_GetObjectItem(commandContext, "onTap") : NULL;
    cJSON *innertubeCommand = onTap ? cJSON_GetObjectItem(onTap, "innertubeCommand") : NULL;
    cJSON *watchEndpoint = innertubeCommand ? cJSON_GetObjectItem(innertubeCommand, "watchEndpoint") : NULL;
    cJSON *videoId = watchEndpoint ? cJSON_GetObjectItem(watchEndpoint, "videoId") : NULL;
    if (videoId && cJSON_IsString(videoId)) {
        snprintf(playlist->thumbnail_path, sizeof(playlist->thumbnail_path), "/vi/%s/mqdefault.jpg", videoId->valuestring);
    }

    else {
        printf("parse_playlist_from_json: failed to parse thumbnail path \n");
        playlist->thumbnail_path[0] = '\0';
    }

    // VIDEO COUNT
    cJSON *overlays = thumbnailViewModel ? cJSON_GetObjectItem(thumbnailViewModel, "overlays") : NULL;
    cJSON *thumbnailOverlayBadgeViewModel = overlays && cJSON_IsArray(overlays) ? cJSON_GetObjectItem(cJSON_GetArrayItem(overlays, 0), "thumbnailOverlayBadgeViewModel") : NULL;
    cJSON *thumbnailBadges = thumbnailOverlayBadgeViewModel ? cJSON_GetObjectItem(thumbnailOverlayBadgeViewModel, "thumbnailBadges") : NULL;
    cJSON *thumbnailBadgeViewModel = thumbnailBadges && cJSON_IsArray(thumbnailBadges) ? cJSON_GetObjectItem(cJSON_GetArrayItem(thumbnailBadges, 0), "thumbnailBadgeViewModel") : NULL;
    cJSON *text = thumbnailBadgeViewModel ? cJSON_GetObjectItem(thumbnailBadgeViewModel, "text") : NULL;
    const bool valid_video_count = text && cJSON_IsString(text);
    if (valid_video_count) {
        strncpy(playlist->video_count, text->valuestring, sizeof(playlist->video_count) - 1);
    }

    else {
        printf("parse_playlist_from_json: failed to parse video count\n");
        strncpy(playlist->video_count, default_value, sizeof(playlist->video_count) - 1);
    }
}

int create_results_from_json(cJSON* cjson, Results *results, const bool allow_youtube_shorts)
{
    if (results == NULL) {
        printf("create_results_from_json: 'results' is NULL\n");
        return 0;
    }

    int elements_added = 0;

    cJSON *item;
    cJSON_ArrayForEach (item, cjson) {
        SearchResult *search_result = init_search_result();
        if (search_result == NULL) {
            printf("create_results_from_json: init_search_result returned NULL\n");
            return 0;
        }
        
        cJSON *videoRenderer = cJSON_GetObjectItem(item, "videoRenderer");     // video
        cJSON *channelRenderer = cJSON_GetObjectItem(item, "channelRenderer"); // channel
        cJSON *lockupViewModel = cJSON_GetObjectItem(item, "lockupViewModel"); // playlist
            
        if (videoRenderer) {
            parse_video_from_json(videoRenderer, allow_youtube_shorts, search_result);
        }

        else if (channelRenderer) {
            parse_channel_from_json(channelRenderer, search_result);
        }
        
        else if (lockupViewModel) {
            parse_playlist_from_json(lockupViewModel, search_result);
        }

        if (search_result->media_type != UNDF) {
            elements_added++;
            add_search_result(results, search_result);
        }

        else free_search_result(search_result);
    }

    return elements_added;
}

cJSON* find_search_result_container(cJSON* json, const SearchType search_type)
{
    switch (search_type) {
        case NEW:
        case APPENDING: {
            cJSON* itemSectionRenderer = find_object(json, "itemSectionRenderer");
            cJSON* contents = itemSectionRenderer ? cJSON_GetObjectItem(itemSectionRenderer, "contents") : NULL;
            return contents;
        }
        case TRENDING: {
            cJSON* sectionListRenderer = find_object(json, "sectionListRenderer");
            cJSON* contents = sectionListRenderer ? cJSON_GetObjectItem(sectionListRenderer, "contents") : NULL;
            cJSON* arrayItem = contents && cJSON_IsArray(contents) ? cJSON_GetArrayItem(contents, 2) : NULL;
            cJSON* items = find_object(arrayItem, "items");
            return items;
        }
        default: return NULL;
    }
}

void* get_results_from_query(void* args)
{
    SearchThreadArgs* targs = (SearchThreadArgs*)args;
    float start_time = GetTime(), end_time; // preformance check

    Buffer http = send_https_request(targs->request, targs->youtube_connection);
    bool application_is_offline = (buffer_ready(&http) == false);
    if (application_is_offline) {
        SetWindowTitle("[offline] - metube");
        free(targs);
        search_finished = true;
        return NULL;
    }

    // char debug_filename[32];
    // time_t t;
    // time(&t);
    // sprintf(debug_filename, "%s.json", ctime(&t));
    // create_file_from_memory(debug_filename, http);

    cJSON* json = cJSON_Parse(http.data);
    if (json == NULL) {
        printf("get_results_from_query: cJSON_Parse returned NULL\n");
        SetWindowTitle("[offline] - metube");
        free_buffer(&http);
        free(targs);
        search_finished = true;
        return NULL;
    }
    
    cJSON* result_root = find_search_result_container(json, targs->search_type);
    
    get_continuation_token(json, targs->search_type, sizeof(targs->search_results->continuation_token), targs->search_results->continuation_token);

    elements_added = create_results_from_json(result_root, targs->search_results, targs->allow_youtube_shorts);
    search_finished = true;
    if (targs->search_type != APPENDING) {
        delete_old_nodes = true;
    }

    end_time = GetTime();
    printf("search took %f seconds, %d items found\n", end_time - start_time, elements_added);
    
    char app_title[512];
    snprintf(app_title, sizeof(app_title), "[search result(%d)] - metube", elements_added);
    SetWindowTitle(app_title);

    cJSON_Delete(json);
    free_buffer(&http);
    free(targs);
    return NULL;
}

void init_app()
{
    SetTargetFPS(60);
    SetTraceLogLevel(LOG_ERROR);
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    SetConfigFlags(FLAG_WINDOW_ALWAYS_RUN);
    InitWindow(1000, 750, "metube");
}

Texture2D load_texture_from_memory(const Buffer buffer, const float width, const float height)
{
    if (!buffer_ready(&buffer)) {
        printf("load_texture_from_memory: buffer obj passed is invalid\n");
        return (Texture2D){0};
    }

    else if (width < 0 || height < 0) {
        printf("load_texture_from_memory: thumbnail dimensions are invalid\n");
        return (Texture2D){0};
    }

    Image image = LoadImageFromMemory(".jpg", (unsigned char *)buffer.data, buffer.size);
    if (!IsImageReady(image)) {
        printf("load_texture_from_memory: LoadImageFromMemory returned invalid image obj\n");
        return (Texture2D){0};
    }

    ImageResize(&image, width, height);
    
    Texture2D ret = LoadTextureFromImage(image);
    if (!IsTextureReady(ret)) {
        printf("load_texture_from_memory: LoadTextureFromImage returned invalid Texture obj\n");
        return (Texture2D){0};
    }

    UnloadImage(image);

    return ret;
}

typedef struct
{
    Font font;
    int padding;
    int spacing;
    bool word_wrap;
} Ui;

// Draw text using font inside rectangle limits with support for text selection
void DrawTextBoxedSelectable(Ui ui, const char *text, Rectangle rec, float fontSize, Color tint, int selectStart, int selectLength, Color selectTint, Color selectBackTint)
{
    int length = TextLength(text);  // Total length in bytes of the text, scanned by codepoints in loop

    float textOffsetY = 0;          // Offset between lines (on line break '\n')
    float textOffsetX = 0.0f;       // Offset X to next character to draw

    float scaleFactor = fontSize/(float)ui.font.baseSize;     // Character rectangle scaling factor

    // Word/character wrapping mechanism variables
    enum { MEASURE_STATE = 0, DRAW_STATE = 1 };
    int state = ui.word_wrap? MEASURE_STATE : DRAW_STATE;

    int startLine = -1;         // Index where to begin drawing (where a line begins)
    int endLine = -1;           // Index where to stop drawing (where a line ends)
    int lastk = -1;             // Holds last value of the character position

    for (int i = 0, k = 0; i < length; i++, k++)
    {
        // Get next codepoint from byte string and glyph index in font
        int codepointByteCount = 0;
        int codepoint = GetCodepoint(&text[i], &codepointByteCount);
        int index = GetGlyphIndex(ui.font, codepoint);

        // NOTE: Normally we exit the decoding sequence as soon as a bad byte is found (and return 0x3f)
        // but we need to draw all of the bad bytes using the '?' symbol moving one byte
        if (codepoint == 0x3f) codepointByteCount = 1;
        i += (codepointByteCount - 1);

        float glyphWidth = 0;
        if (codepoint != '\n')
        {
            glyphWidth = (ui.font.glyphs[index].advanceX == 0) ? ui.font.recs[index].width*scaleFactor : ui.font.glyphs[index].advanceX*scaleFactor;

            if (i + 1 < length) glyphWidth = glyphWidth + ui.spacing;
        }

        // NOTE: When wordWrap is ON we first measure how much of the text we can draw before going outside of the rec container
        // We store this info in startLine and endLine, then we change states, draw the text between those two variables
        // and change states again and again recursively until the end of the text (or until we get outside of the container).
        // When wordWrap is OFF we don't need the measure state so we go to the drawing state immediately
        // and begin drawing on the next line before we can get outside the container.
        if (state == MEASURE_STATE)
        {
            // TODO: There are multiple types of spaces in UNICODE, maybe it's a good idea to add support for more
            // Ref: http://jkorpela.fi/chars/spaces.html
            if ((codepoint == ' ') || (codepoint == '\t') || (codepoint == '\n')) endLine = i;

            if ((textOffsetX + glyphWidth) > rec.width)
            {
                endLine = (endLine < 1)? i : endLine;
                if (i == endLine) endLine -= codepointByteCount;
                if ((startLine + codepointByteCount) == endLine) endLine = (i - codepointByteCount);

                state = !state;
            }
            else if ((i + 1) == length)
            {
                endLine = i;
                state = !state;
            }
            else if (codepoint == '\n') state = !state;

            if (state == DRAW_STATE)
            {
                textOffsetX = 0;
                i = startLine;
                glyphWidth = 0;

                // Save character position when we switch states
                int tmp = lastk;
                lastk = k - 1;
                k = tmp;
            }
        }
        else
        {
            if (codepoint == '\n')
            {
                if (!ui.word_wrap)
                {
                    textOffsetY += (ui.font.baseSize + ui.font.baseSize / 2.0f) * scaleFactor;
                    textOffsetX = 0;
                }
            }
            else
            {
                if (!ui.word_wrap && ((textOffsetX + glyphWidth) > rec.width))
                {
                    textOffsetY += (ui.font.baseSize + ui.font.baseSize / 2.0f) * scaleFactor;
                    textOffsetX = 0;
                }

                // When text overflows rectangle height limit, just stop drawing
                if ((textOffsetY + ui.font.baseSize*scaleFactor) > rec.height) break;

                // Draw selection background
                bool isGlyphSelected = false;
                if ((selectStart >= 0) && (k >= selectStart) && (k < (selectStart + selectLength)))
                {
                    DrawRectangleRec((Rectangle){ rec.x + textOffsetX - 1, rec.y + textOffsetY, glyphWidth, (float)ui.font.baseSize*scaleFactor }, selectBackTint);
                    isGlyphSelected = true;
                }

                // Draw current character glyph
                if ((codepoint != ' ') && (codepoint != '\t'))
                {
                    DrawTextCodepoint(ui.font, codepoint, (Vector2){ rec.x + textOffsetX, rec.y + textOffsetY }, fontSize, isGlyphSelected? selectTint : tint);
                }
            }

            if (ui.word_wrap && (i == endLine))
            {
                textOffsetY += (ui.font.baseSize + ui.font.baseSize / 2.0f) * scaleFactor;
                textOffsetX = 0;
                startLine = endLine;
                endLine = -1;
                glyphWidth = 0;
                selectStart += lastk - k;
                k = lastk;

                state = !state;
            }
        }

        if ((textOffsetX != 0) || (codepoint != ' ')) textOffsetX += glyphWidth;  // avoid leading spaces
    }
}

// Draw text using font inside rectangle limits
void DrawTextBoxed(const char *text, Rectangle rec, Ui ui, float fontSize, Color tint)
{
    DrawTextBoxedSelectable(ui, text, rec, fontSize, tint, 0, 0, WHITE, WHITE);
}

Rectangle padded_rectangle(const float padding, const Rectangle rect)
{
    return (Rectangle) { rect.x + padding, rect.y + padding, rect.width - padding, rect.height - (padding * 2) };
}

void draw_thumbnail_subtext(const Rectangle container, Ui ui, const Color text_color, const int font_size, const char* text)
{
    const Vector2 text_size = MeasureTextEx(ui.font, text, font_size, ui.spacing);
    const float content_width = text_size.x + (ui.padding * 2);
    const float content_height = text_size.y + (ui.padding * 2);
    
    const Rectangle length_area = {
        .x = container.x + container.width - content_width - ui.padding,
        .y = container.y + container.height - content_height - ui.padding,
        .width = content_width,
        .height = content_height
    };

    // draw box with text inside it
    DrawRectangleRec(length_area, Fade(BLACK, 0.7));
    DrawTextBoxed(text, padded_rectangle(ui.padding, length_area), ui, font_size, text_color);
}

bool draw_filter_toggle(const Rectangle container, const Rectangle button_bounds, const char *label_text, const char *value_text, const char *button_text, const Font font, const int padding)
{
    DrawTextEx(font, label_text, (Vector2){container.x + padding, button_bounds.y + padding}, 11, 2, BLACK);
    DrawTextEx(font, value_text, (Vector2){((container.x + container.width) * 0.45f), button_bounds.y + padding}, 11, 2, BLACK);
    return GuiButton(button_bounds, button_text);
}

void draw_filter_window(Query *query, const Rectangle container, const Font font, const int padding)
{
    DrawRectangleLinesEx(container, 1, GRAY);

    // buttons to switch filter params (the type of content and how they will be sorted)
    const char* button_text = "Switch";
    
    // adjust query sort type
    Rectangle sort_type_button_bounds = {
        .x = container.x + container.width - 55, 
        .y = container.y + padding,
        .width = 50, 
        .height = 17.5
    };

    if (draw_filter_toggle(container, sort_type_button_bounds, "Order:", sort_type_to_text(query->sort), button_text, font, padding)) {
        query->sort = (SortType) bound_index_to_array((query->sort + 1), N_SORT_TYPES);
    }
    
    // adjust query media type
    Rectangle media_type_button_bounds = {
        .x = sort_type_button_bounds.x,
        .y = sort_type_button_bounds.y + sort_type_button_bounds.height + padding,
        .width = 50,
        .height = 17.5,
    };

    if (draw_filter_toggle(container, media_type_button_bounds, "Type:", media_type_to_text(query->media), button_text, font, padding)) {
        query->media = (MediaType) bound_index_to_array((query->media + 1), N_MEDIA_TYPES);
    }

    // toggle wether to allow yt shorts or not 
    Rectangle allow_yt_short_button_bounds = {
        .x = sort_type_button_bounds.x,
        .y = media_type_button_bounds.y + media_type_button_bounds.height + padding,
        .width = 50,
        .height = 17.5,
    };

    if (draw_filter_toggle(container, allow_yt_short_button_bounds, "Allow Shorts:", (query->allow_youtube_shorts ? "Yes" : "No"), button_text, font, padding)) {
        query->allow_youtube_shorts = !query->allow_youtube_shorts;
    }
}

void youtube_internal_api_key(PersistentConnection *youtube_connection, const size_t n, char key[n])
{
    PreparedRequest request;
    request.body[0] = '\0';
    strcpy(request.path, "/");
    configure_get_header(sizeof(request.header), request.header, youtube_connection->host, request.path);

    Buffer youtube_page = send_https_request(request, youtube_connection);
    if (buffer_ready(&youtube_page) == false) {
        printf("youtube_internal_api_key: page response is invalid\n");
        return;
    }

    const char *tag = "\"INNERTUBE_API_KEY\"";
    char *tag_location = strstr(youtube_page.data, tag);
    if (tag_location == NULL) {
        printf("youtube_internal_api_key: \"%s\" not found\n", tag);
        return;
    }

    bool in_key = false;
    int i = 0;
    
    for (char* current = tag_location + strlen(tag); current && (i < n); current++) {
        const char c = *current;

        if (!in_key) {
            if (c == '"') 
                in_key = true;
        }
        
        else if (in_key) {
            if (c == '"') 
                break;
            else 
                key[i++] = c;
        }
    }

    key[i] = '\0';

    free_buffer(&youtube_page);
}

void draw_search_result(SearchResult *search_result, const Texture2D thumbnail, const Rectangle container, const Color color, const Ui ui)
{
    DrawRectangleRec(container, color);

    const Rectangle thumbnail_area = { 
        .x = container.x, 
        .y = container.y, 
        .width = container.width * 0.45f, 
        .height = container.height 
    };

    if (IsTextureReady(thumbnail)) {
        DrawTextureEx(thumbnail, (Vector2){thumbnail_area.x, thumbnail_area.y}, 0.0f, 1.0f, WHITE);
    }

    const Rectangle title_area = {
        .x = thumbnail_area.x + thumbnail_area.width,
        .y = thumbnail_area.y,
        .width = container.width - thumbnail_area.width,
        .height = thumbnail_area.height * 0.70f
    };

    if (search_result->title[0] != '\0') {
        DrawTextBoxed(search_result->title, padded_rectangle(ui.padding, title_area), ui, 12, BLACK);                            
    }

    const Rectangle subtext_area = {
        .x = thumbnail_area.x + thumbnail_area.width,
        .y = title_area.y + title_area.height,
        .width = title_area.width,
        .height = container.height - title_area.height,
    };

    switch (search_result->media_type) {
        case VIDEO:
            DrawTextBoxed(TextFormat("%s - %s views", search_result->date_published, search_result->view_count), padded_rectangle(ui.padding, subtext_area), ui, 11.5, BLACK);
            draw_thumbnail_subtext(thumbnail_area, ui, RAYWHITE, 12, search_result->duration);
            break;
        case LIVE:
            DrawTextBoxed(TextFormat("%s watching", search_result->view_count), padded_rectangle(ui.padding, subtext_area), ui, 12, BLACK);
            draw_thumbnail_subtext(thumbnail_area, ui, RAYWHITE, 12, "LIVE");
            break;
        case CHANNEL:
            DrawTextBoxed(search_result->subscriber_count, padded_rectangle(ui.padding, subtext_area), ui, 12, BLACK);
            break;
        case PLAYLIST:
            draw_thumbnail_subtext(thumbnail_area, ui, RAYWHITE, 12, search_result->video_count);
            break;
        default:    
            break;
    }
}

#define MINUTE 60
#define THUMBNAIL_LIFETIME (MINUTE * 1)

typedef struct
{
    char id [64];
    Timer timer;
    Texture2D thumbnail;
    UT_hash_handle hh;
} TextureCacheEntry;

TextureCacheEntry* init_cached_texture(const Texture2D texture, const char* id)
{
    if ((id == NULL) || (id[0] == '\0')) {
        printf("init_cached_texture: invalid id passed\n");
        return NULL;
    }
    
    TextureCacheEntry *cached_thumbnail = (TextureCacheEntry*) malloc(sizeof(TextureCacheEntry));
    if (cached_thumbnail == NULL) {
        printf("init_cached_texture: malloc returned NULL\n");
        return NULL;
    }

    cached_thumbnail->thumbnail = texture;
    strcpy(cached_thumbnail->id, id);
    start_timer(&cached_thumbnail->timer, THUMBNAIL_LIFETIME);

    return cached_thumbnail;
}

void free_cached_thumbnail(TextureCacheEntry *cached_entry)
{
    if (cached_entry == NULL) return;
    if (IsTextureReady(cached_entry->thumbnail)) UnloadTexture(cached_entry->thumbnail);
    free(cached_entry);
}

void cache_texture(TextureCacheEntry **hashtable, TextureCacheEntry *cached_entry)
{
    if (cached_entry == NULL) {
        printf("cache_texture: thumbnail to cache is NULL\n");
        return;
    }

    HASH_ADD_STR(*hashtable, id, cached_entry);
}

void delete_cached_texture(TextureCacheEntry **hashtable, TextureCacheEntry *cached_entry)
{
    if (HASH_COUNT(*hashtable) == 0) {
        printf("delete_cached_texture: hashtable is empty");
        return;
    }

    if (cached_entry == NULL) {
        printf("delete_cached_texturel: cached_entry is NULL\n");
        return;
    }

    HASH_DEL(*hashtable, cached_entry);

    free_cached_thumbnail(cached_entry);
}

void free_cached_textures(TextureCacheEntry **hashtable)
{
    if (hashtable == NULL) return;

    TextureCacheEntry *current, *tmp;
    HASH_ITER(hh, *hashtable, current, tmp) {
        delete_cached_texture(hashtable, current);
    }
}

TextureCacheEntry* find_cached_thumbnail(const char *id, TextureCacheEntry **hashtable)
{
    if (hashtable == NULL) return NULL;

    TextureCacheEntry *found = NULL;
    
    HASH_FIND_STR(*hashtable, id, found);
    
    return found;
}

bool cached_texture_exists(const char *id, TextureCacheEntry **hashtable)
{
    return find_cached_thumbnail(id, hashtable) != NULL;
}

void remove_expired_thumbnails(TextureCacheEntry **hashtable)
{
    if (hashtable == NULL) return;

    TextureCacheEntry *current, *tmp;

    HASH_ITER(hh, *hashtable, current, tmp) {
        if (current && timer_done(current->timer)) {
            delete_cached_texture(hashtable, current);
        }
    }
}

void process_thumbnail_queue(RawThumbnailQueue *queue, TextureCacheEntry **hashtable)
{
    if (queue == NULL) return;

    pthread_mutex_lock(&queue->mutex);

    while (queue->head != NULL) {
        RawThumbnail *raw_thumbnail = dequeue_thumbnail(queue);

        const Texture2D thumbnail = load_texture_from_memory(raw_thumbnail->data, 150, 80);
        if (IsTextureReady(thumbnail)) {
            TextureCacheEntry *cached_entry = init_cached_texture(thumbnail, raw_thumbnail->search_result_id);
            if (cached_entry) {
                cache_texture(hashtable, cached_entry);
            }
        }

        free_raw_thumbnail(raw_thumbnail);
    }

    pthread_mutex_unlock(&queue->mutex);
}

bool launch_task(TaskQueue* task_queue, void* targs, void* (*funct)(void*))
{
    ThreadTask* task = (ThreadTask*) malloc(sizeof(ThreadTask));
    if (task == NULL) {
        printf("launch_task: malloc returned NULL for 'task'\n");
        return false;
    }

    task->next = NULL;
    task->args = targs;
    task->funct = funct;

    pthread_mutex_lock(&task_queue->mutex);
    enqueue_task(task, task_queue);
    pthread_cond_signal(&task_queue->cond);
    pthread_mutex_unlock(&task_queue->mutex);

    return true;
}

int main()
{
    SSL_library_init();
    OpenSSL_add_all_algorithms();
    
    TextureCacheEntry *cached_thumbnails = NULL;
    RawThumbnailQueue thumbnail_queue = init_thumbnail_queue();

    Results results = init_results();

    TaskQueue task_queue = init_task_queue();
    pthread_t thread_pool[MAX_THREADS];
    init_thread_pool(MAX_THREADS, thread_pool, worker_thread_funct, &task_queue);
    
    ctx = SSL_CTX_new(TLS_client_method());
    if (!ctx) {
        printf("error initalizing SSL_CTX object\n");
        return 1;
    } 

    const char* youtube_host = media_type_to_host(ANY);
    ConnectionPool youtube_pool = init_connection_pool(youtube_host);

    // playlists and live videos also share the same host
    const char* video_host = media_type_to_host(VIDEO);
    ConnectionPool video_thumbnail_pool = init_connection_pool(video_host);

    const char* channel_host = media_type_to_host(CHANNEL);
    ConnectionPool channel_thumbnail_pool = init_connection_pool(channel_host);

    char internal_api_key[64];
    youtube_internal_api_key(&youtube_pool.connections[youtube_pool.current_conn], sizeof(internal_api_key), internal_api_key);
   
    printf("INTERNAL KEY: \"%s\"\n", internal_api_key);

    // when true, the application starts the search process
    bool search = false;
    char last_search[512] = {0};

    // the current_query that the user has constructed
    Query query = {
        .allow_youtube_shorts = true,
        .string = "",
        .media = ANY,
        .sort = BY_RELEVANCE,
        .search_type = NEW,
    };

    // used in 'GuiTextBox' function
    // only true when the text window is focused
    bool edit_mode = false;

    // scroll bar varaibles, no idea how this works, taken from raylib example...
    Vector2 scroll = { 10, 10 };
    Rectangle scrollView = { 0, 0 };

    init_app();

    Ui ui;
    ui.font = GetFontDefault();
    ui.padding = 5;
    ui.spacing = 2;
    ui.word_wrap = true;

    while (!WindowShouldClose())
    {
        if (HASH_COUNT(cached_thumbnails) > 0) {
            remove_expired_thumbnails(&cached_thumbnails);
        }

        if (thumbnail_queue.count > 0) {
            process_thumbnail_queue(&thumbnail_queue, &cached_thumbnails);
        }

        if (delete_old_nodes) {
            delete_old_nodes = false;

            int nodes_to_delete = results.count - elements_added;
            for (int i = 0; (i < nodes_to_delete) && results.head; i++) {
                SearchResult *r = results.head;
                results.head = results.head->next;
                free_search_result(r);
                results.count--;
            } 
        }

        if (search) {
            search = search_finished = false;

            free_thumbnail_queue(&thumbnail_queue);
            thumbnail_queue = init_thumbnail_queue();

            char app_title[512];
            snprintf(app_title, sizeof(app_title), "[%s(loading)] - metube", query.string);
            SetWindowTitle(app_title);

            // evade bot detection
            if (strcmp(last_search, query.string) == 0) {
                cycle_connection(&youtube_pool);
            }

            strncpy(last_search, query.string, sizeof(last_search) - 1);
            
            char path[128] = {0};
            const char *youtube_api_endpoint = (query.search_type == TRENDING) ? "browse" : "search";
            snprintf(path, sizeof(path), "/youtubei/v1/%s?key=%s", youtube_api_endpoint, internal_api_key);
            
            PreparedRequest post = {0};

            size_t body_len = configure_post_body(sizeof(post.body), post.body, query, results.continuation_token);
                              configure_post_header(sizeof(post.header), post.header, youtube_pool.connections[youtube_pool.current_conn].host, path, body_len);

            SearchThreadArgs* targs = create_search_thread_args(query, post, &results, &thumbnail_queue, &youtube_pool.connections[youtube_pool.current_conn]);
            if (targs && (launch_task(&task_queue, targs, get_results_from_query) == false)) {
                free(targs);
            }
        }

        BeginDrawing();

            ClearBackground(RAYWHITE);

            const Rectangle search_bar_bounds = {
                .x = ui.padding, 
                .y = ui.padding, 
                .width = 350, 
                .height = 25 
            };

            const Rectangle search_button_bounds = {
                .x = (search_bar_bounds.x + search_bar_bounds.width + ui.padding), 
                .y = search_bar_bounds.y, 
                .width = 50, 
                .height = 25
            };
            
            const Rectangle trending_button_bounds = {
                .x = search_button_bounds.x + search_button_bounds.width + ui.padding,
                .y = ui.padding,
                .width = 50,
                .height = 25,
            };

            if (GuiButton(trending_button_bounds, "Trending")) {
                search = search_finished;
                query.search_type = TRENDING;
            }

            // edit_mode toggles when search box is focused (T) or not (F)
            int text_box_status;
            if ((text_box_status = GuiTextBox(search_bar_bounds, query.string, sizeof(query.string), edit_mode))) {
                edit_mode = !edit_mode;
            }

            bool enter_key_pressed = text_box_status == 2;

            if (GuiButton(search_button_bounds, "Search") || enter_key_pressed) {
                // sanitize query
                remove_leading_whitespace(query.string);
                remove_trailing_whitespace(query.string);

                // load url encoded string into query 
                if (query.string[0] != '\0') {
                    search = search_finished;
                    query.search_type = NEW;
                }
            }

            const Rectangle filter_window_bounds = {
                .x = ui.padding, 
                .y = search_button_bounds.y + search_button_bounds.height + ui.padding, 
                .width = search_bar_bounds.width, 
                .height = 75
            };

            // toggle filter window on press
            draw_filter_window(&query, filter_window_bounds, ui.font, ui.padding);

            const float button_height = 30;
            const Rectangle load_more_button_bounds = {
                .x = search_bar_bounds.x,
                .height = button_height,
                .y = GetScreenHeight() - button_height - ui.padding,
                .width = search_bar_bounds.width,
            };

            if (results.count == 0 || results.continuation_token[0] == '\0') {
                GuiSetState(STATE_DISABLED);
            }

            if (GuiButton(load_more_button_bounds, "LOAD MORE")) {
                search = search_finished;
                query.search_type = APPENDING;
            }

            GuiSetState(STATE_NORMAL);
            
            const Rectangle scroll_window_bounds = { 
                .x = search_bar_bounds.x, 
                .y = search_bar_bounds.y + search_bar_bounds.height + filter_window_bounds.height + (ui.padding * 2), 
                .width = search_bar_bounds.width, 
                .height = GetScreenHeight() - scroll_window_bounds.y - load_more_button_bounds.height - (ui.padding * 2), 
            };

            const int content_height = 80;

            // how much space all search result squares take
            const Rectangle content_area = {
                .x = scroll_window_bounds.x,
                .y = scroll_window_bounds.y,
                .width = scroll_window_bounds.width,
                .height = content_height * results.count,
            };

            const bool vertical_scrollbar_visible = (content_area.height > scroll_window_bounds.height);
            const int SCROLLBAR_WIDTH = vertical_scrollbar_visible ? 12 : 0;

            GuiScrollPanel(scroll_window_bounds, NULL, content_area, &scroll, &scrollView);

            const Rectangle scissor_rect = padded_rectangle(1, scroll_window_bounds);
            
            BeginScissorMode(scissor_rect.x, scissor_rect.y, scissor_rect.width, scissor_rect.height);
            
            int i = 0;
            float y_level = scissor_rect.y;
            
            for (SearchResult *search_result = results.head; search_result; search_result = search_result->next, i++, y_level += content_height) {
                const Rectangle container = { 
                    .x = ui.padding, 
                    .y = y_level + scroll.y, 
                    .width = scissor_rect.width - SCROLLBAR_WIDTH,
                    .height = content_height 
                };

                const Color container_color = (i % 2) ? WHITE : RAYWHITE;

                Texture2D thumbnail = (Texture2D){0};
                TextureCacheEntry *cached = find_cached_thumbnail(search_result->id, &cached_thumbnails);
                if (cached) {
                    thumbnail = cached->thumbnail;
                    start_timer(&cached->timer, THUMBNAIL_LIFETIME); // refresh lifetime
                }

                else if ((search_result->thumbnail_loaded == false) && (search_result->thumbnail_path[0] != '\0')) {
                    search_result->thumbnail_loaded = true;
                    
                    ConnectionPool* pool = (search_result->media_type == CHANNEL) ? &channel_thumbnail_pool : &video_thumbnail_pool;
                    PersistentConnection* conn = &pool->connections[pool->current_conn];

                    PreparedRequest get = {0};
                    configure_get_header(sizeof(get.header), get.header, conn->host, search_result->thumbnail_path);    

                    LoadThumbnailArgs* targs = create_load_thumbnail_args(search_result->id, get, conn, &thumbnail_queue);
                    if (targs && (launch_task(&task_queue, targs, load_thumbnail) == false)) {
                        free(targs);
                    }

                    cycle_connection(pool);
                }

                if (CheckCollisionRecs(container, scissor_rect) == true) {
                    draw_search_result(search_result, thumbnail, container, container_color, ui);
                }                
            }
                
            EndScissorMode();

        EndDrawing();
    }

    // free worker thread stuff
    application_running = false;
    pthread_cond_broadcast(&task_queue.cond);
    free_thread_pool(MAX_THREADS, thread_pool);
    free_task_queue(&task_queue);         
    
    // deinit app
    UnloadFont(ui.font);
    free_results(&results);
    free_thumbnail_queue(&thumbnail_queue);
    free_cached_textures(&cached_thumbnails);
    
    // ssl stuff
    if (ctx) SSL_CTX_free(ctx);
    free_connection_pool(&youtube_pool);
    free_connection_pool(&video_thumbnail_pool);
    free_connection_pool(&channel_thumbnail_pool);
    
    CloseWindow();
    
    return 0;
}

// searching feature
    // clean loading thumbnail functionality in main
    // replace cjson walk-down with recursive search in parse function 
    // reccomendations
    // clean everything

// video playing function
    // show video information when double clicking video
    // play video when pressing button

// video management function
    // subscribe to different channels
    // have a liked videos playist
    // able to add videos to playlist

// after everythings done:
    // fonts for L.O.T.E.
    // handle connecticity issues (no wifi on startup, changing connections, etc.)
    // handle cleanup when prematurley deleting
        // thumbnail data list
        // search arguements
        // cached thumbnails