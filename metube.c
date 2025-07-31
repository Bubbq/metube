#include <math.h>
#include <openssl/asn1.h>
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
    SHORT,
    UNDF,
} MediaType; 

#define N_MEDIA_TYPES 5

char* media_type_to_url(const MediaType media_type)
{
    switch (media_type) {
        case SHORT:
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
        case SHORT:
        case LIVE:
        case VIDEO: return "i.ytimg.com";
        case CHANNEL: return "yt3.ggpht.com";
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
    char authorId[64];          // id of content creator
    char title[256];            // name of the content           
    char subscriber_count[32];  // X.XX k/M/B formatted   
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
    memset(search_result->authorId, 0, sizeof(search_result->authorId));
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
            search_result->id, search_result->title, search_result->authorId, search_result->subscriber_count, search_result->view_count, search_result->date_published, search_result->duration, search_result->video_count, search_result->media_type, search_result->thumbnail_path);
}

// linked list of search results returned from a query
typedef struct
{
    size_t count;           
    SearchResult* head;    
    SearchResult* tail;
    pthread_mutex_t mutex;     
} Results;

Results init_results() 
{
    Results search_results;
    search_results.head = search_results.tail = NULL;
    search_results.count = 0;
    pthread_mutex_init(&search_results.mutex, NULL);
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

    pthread_mutex_destroy(&results->mutex);
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
    SEARCH_TYPE_QUERIED,  
    SEARCH_TYPE_RELATED,  
    SEARCH_TYPE_TRENDING, 
    SEARCH_TYPE_VIDEO_FOCUS,
    SEARCH_TYPE_VIEW_PLAYLIST,
    SEARCH_TYPE_VIEW_CHANNEL,
} SearchType;

typedef enum
{
    SEARCH_ATTR_REPLACE,
    SEARCH_ATTR_APPENDING,
} SearchAttribute;

const char* search_type_to_endpoint(const SearchType search_type)
{
    switch (search_type) {
        case SEARCH_TYPE_QUERIED: return "search";
        case SEARCH_TYPE_VIEW_CHANNEL:
        case SEARCH_TYPE_VIEW_PLAYLIST:
        case SEARCH_TYPE_TRENDING: return "browse";
        case SEARCH_TYPE_VIDEO_FOCUS: return "player";
        case SEARCH_TYPE_RELATED: return "next";
        default:    
            printf("search_type_to_endpoint: invalid type passed\n");
            return NULL;
    }
}

bool resolve_youtube_api_path(const size_t n, char path[n], SearchType search_type, const char* key)
{
    const char* endpoint = search_type_to_endpoint(search_type);

    if ((endpoint == NULL) || (key == NULL)) return false;

    return (snprintf(path, n, "/youtubei/v1/%s?key=%s", endpoint, key) < n);
}

// represents user-defined parameters for a YouTube search request
typedef struct
{
    bool allow_youtube_shorts; 
    char* continuation_token;
    char focused_id[64];     
    char string[256];        
    MediaType media;          
    SortType sort;
    SearchType search_type;
    SearchAttribute search_attr;    
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

static RawThumbnailQueue thumbnail_queue;

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
        size_t len;
        if ((len = ssl_read_line(ssl, line, sizeof(line))) < 0) {
            printf("ssl_read_header: failed\n");
            
            if (buffer_ready(&header)) {
                free_buffer(&header);
            }

            return init_buffer();
        }

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

typedef struct
{
    char path[256];
    char* body;
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

static ConnectionPool youtube_pool;
static ConnectionPool video_thumbnail_pool;
static ConnectionPool channel_thumbnail_pool;

ConnectionPool* media_type_to_pool(const MediaType media_type)
{   
    switch (media_type) {
        case LIVE:
        case SHORT:
        case VIDEO:
        case PLAYLIST: return &video_thumbnail_pool;
        case CHANNEL: return &channel_thumbnail_pool;
        default:
            printf("media_type_to_pool: invalid type passed %d\n", media_type);
            return NULL;
    }
}

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

    if (request.body && (request.body[0] != '\0')) {
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
        create_file_from_memory("error_header.txt", response.header);
        connection->connected = false;
        pthread_mutex_unlock(&connection->mutex);
        return (HTTPS_Response){0};
    }

    if (header_contains_tag(response.header.data, "Content-Length:")) {
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

    else if (header_contains_tag(response.header.data, "Transfer-Encoding: chunked")) {
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

bool configure_get_header(const size_t n, char get_header[n], const char *host, const char *path)
{
    const size_t len =  snprintf(get_header, n,
                "GET %s HTTP/1.1\r\n"
                        "Host: %s\r\n"
                        "User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:125.0) Gecko/20100101 Firefox/125.0\r\n"
                        "Connection: keep-alive\r\n"
                        "\r\n",
                        path, host);
    return len < n;
}

size_t trim_whitespace(char* string)
{
    if ((string == NULL) || (string[0] == '\0')) return 0;
    
    char* start = string;
    while (isspace((unsigned char) *start)) {
        start++;
    }

    char* end = string + strlen(string) - 1;
    while (isspace((unsigned char) *end)) {
        end--;
    }

    size_t len = end < start ? 0 : end - start + 1;
    memmove(string, start, len);
    
    string[len] = '\0';

    return len;
}

int filter_non_numeric_chars(char* string, const size_t string_size)
{
    if (string == NULL) return -1;

    int i = 0;
    for (int j = 0; j < string_size; j++) {
        const char c = string[j];
        if (isdigit(c)) {
            string[i++] = string[j];
        }
    }

    string[i] = '\0';   

    return i;
}

void format_view_count(char* dest, const size_t dest_size)
{
    if (dest == NULL) {
        printf("format_view_count: 'dest' is NULL\n");
        return;
    }

    if (filter_non_numeric_chars(dest, dest_size) <= 0) {
        printf("format_view_count: invalid view format passed\n");
        return;
    } 

    const float view_count = strtof(dest, NULL);

    int written;
    if      (view_count < 1e3)  written = snprintf(dest, dest_size, "%.0f  views", view_count);         // 0      - 999
    else if (view_count < 1e4)  written = snprintf(dest, dest_size, "%.2fk views", (view_count / 1e3)); // 1,000  - 9,999
    else if (view_count < 1e5)  written = snprintf(dest, dest_size, "%.1fk views", (view_count / 1e3)); // 10,000 - 99,999
    else if (view_count < 1e6)  written = snprintf(dest, dest_size, "%.0fk views", (view_count / 1e3)); // 10,000 - 99,999
    else if (view_count < 1e7)  written = snprintf(dest, dest_size, "%.2fM views", (view_count / 1e6)); // 10,000 - 99,999
    else if (view_count < 1e8)  written = snprintf(dest, dest_size, "%.1fM views", (view_count / 1e6)); // 10,000 - 99,999
    else if (view_count < 1e9)  written = snprintf(dest, dest_size, "%.0fM views", (view_count / 1e6)); // 10,000 - 99,999
    else if (view_count < 1e10) written = snprintf(dest, dest_size, "%.2fB views", (view_count / 1e9)); // 10,000 - 99,999
    else if (view_count < 1e11) written = snprintf(dest, dest_size, "%.1fB views", (view_count / 1e9)); // 10,000 - 99,999
    else if (view_count < 1e12) written = snprintf(dest, dest_size, "%.0fB views", (view_count / 1e9)); // 10,000 - 99,999
    
    if (written >= dest_size) {
        printf("format_view_count: string truncated\n");
        return;
    }
}

typedef struct 
{
    char search_result_id[64];
    PreparedRequest request;
    PersistentConnection *connection;
    RawThumbnailQueue *thumbnail_queue;
} LoadThumbnailArgs;

void* load_thumbnail(void *args)
{
    LoadThumbnailArgs *targs = (LoadThumbnailArgs*) args;
    
    HTTPS_Response thumbnail_response = send_https_request(targs->request, targs->connection);
    if (https_response_ready(&thumbnail_response) == false) {
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

    thumbnail_data->data = thumbnail_response.body;
    strcpy(thumbnail_data->search_result_id, targs->search_result_id);

    pthread_mutex_lock(&targs->thumbnail_queue->mutex);
    enqueue_thumbnail(targs->thumbnail_queue, thumbnail_data);
    pthread_mutex_unlock(&targs->thumbnail_queue->mutex);

    free_buffer(&thumbnail_response.header);
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
static TaskQueue task_queue;

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
    Query* query;
    PreparedRequest request;
    Results *search_results;
    RawThumbnailQueue *thumbnail_queue;
    PersistentConnection *youtube_connection;
} SearchThreadArgs;

SearchThreadArgs* init_search_thread_args(Query* query, PreparedRequest request, Results* search_results, RawThumbnailQueue* thumbnail_queue, PersistentConnection* youtube_connection)
{
    SearchThreadArgs* targs = (SearchThreadArgs*) malloc(sizeof(SearchThreadArgs));
    if (targs == NULL) {
        printf("create_search_thread_args: malloc returned NULL for 'search_thread_args'\n");
        return NULL;
    }

    targs->query = query;
    targs->request = request;
    targs->search_results = search_results;
    targs->thumbnail_queue = thumbnail_queue;
    targs->youtube_connection = youtube_connection;

    return targs;
}

bool valid_cjson_string(const cJSON* json_str)
{
    return (json_str) && (cJSON_IsString(json_str)) && (json_str->valuestring) && (json_str->valuestring[0] != '\0');
}

bool valid_cjson_array(const cJSON* json_arr)
{
    return json_arr && cJSON_IsArray(json_arr);
}

bool valid_cjson_number(const cJSON* json_num)
{
    return json_num && cJSON_IsNumber(json_num);
}

bool string_is_integer(const char *s)
{
    if (s == NULL || *s == '\0') return false;

    if (*s == '-' || *s == '+') s++;

    if (*s == '\0') return false;

    while (*s) {
        if (!isdigit((unsigned char)*s)) return false;
        s++;
    }

    return true;
}

cJSON* cjson_pointer_get(cJSON* root, const char* path)
{
    if ((root == NULL) || (path == NULL)) return NULL;

    const int last_element_index = -1;

    int n = 0;
    const char** elements = TextSplit(path, '.', &n); 

    cJSON* ret = root;

    for(int i = 0; (i < n); i++) {
        if (elements[i][0] == '\0') {
            continue;
        }

        const char* opening_brace_ptr = strchr(elements[i], '[');
        if (opening_brace_ptr) {
            const size_t name_len = opening_brace_ptr - elements[i] + 1; 

            char array_name[name_len];
            strncpy(array_name, elements[i], name_len - 1);
            array_name[name_len - 1] = '\0';

            ret = cJSON_GetObjectItem(ret, array_name);
            if (ret == NULL) {
                // printf("cjson_pointer_get: failed to add array object \"%s\"\n", elements[i]);
                return NULL;
            }

            if (cJSON_IsArray(ret) == false) {
                printf("cjson_pointer_get: accessing element in non-array object (%s)\n", elements[i]);
                return NULL;
            }

            const char* closing_brace_ptr = strchr(opening_brace_ptr, ']');
            if (closing_brace_ptr == NULL) {
                printf("cjson_pointer_get: \"%s\" is an unbalanced array\n", elements[i]);
                return NULL;
            }

            const size_t len = closing_brace_ptr - opening_brace_ptr; 

            char index_buffer[len];
            strncpy(index_buffer, opening_brace_ptr + 1, len - 1);
            index_buffer[len - 1] = '\0';

            if (string_is_integer(index_buffer) == false) {
                printf("cjson_pointer_get: invalid array element: \"%s\"\n", index_buffer);
                return NULL;
            }

            const size_t arr_size = cJSON_GetArraySize(ret);
            
            const int index_buffer_val = atoi(index_buffer);
            const size_t index = (index_buffer_val == last_element_index) ? (arr_size - 1) : index_buffer_val;
            if (index >= arr_size) {
                printf("cjson_pointer_get: accessing element %zu in %zu size array (%s)\n", index, arr_size, elements[i]);
                return NULL;
            }

            ret = cJSON_GetArrayItem(ret, index);
        } 

        else ret = cJSON_GetObjectItem(ret, elements[i]);
    }

    return ret;
}

bool video_is_youtube_short(cJSON *videoRenderer) 
{
    const char* path = ".navigationEndpoint.commandMetadata.webCommandMetadata.url";
    const cJSON* url = cjson_pointer_get(videoRenderer, path); 
    if (valid_cjson_string(url)) {
        return strstr(url->valuestring, "/shorts");
    }

    return false;
}

bool video_is_live(cJSON* videoRenderer)
{
    const char* path = ".badges[0].metadataBadgeRenderer.label";
    const cJSON* label = cjson_pointer_get(videoRenderer, path);
    if (valid_cjson_string(label)) {
        return (strcmp("LIVE", label->valuestring) == 0);
    }

    return false;
}

bool assign_string_from_path(cJSON* root, const char* path, char* dest, const size_t dest_size)
{
    if ((root == NULL) || (path == NULL) || (dest == NULL)) {
        printf("assign_string_from_path: invalid input\n");
        return false;
    }

    const cJSON* json_str = cjson_pointer_get(root, path);

    if (valid_cjson_string(json_str)) {
        strncpy(dest, json_str->valuestring, dest_size - 1);
        dest[dest_size - 1] = '\0';
        return true;
    }

    else return false;
}

#define MEDIUM_THUMBNAIL_VIDEO_RESOLUTION "mqdefault"

bool assign_video_thumbnail_path(const char* video_id, char* dest, const size_t dest_size)
{
    if ((video_id == NULL) || (dest == NULL)) {
        printf("assign_video_thumbnail_path: invalid input\n");
        return false;
    }

    const size_t written = snprintf(dest, dest_size, "/vi/%s/" MEDIUM_THUMBNAIL_VIDEO_RESOLUTION ".jpg", video_id);
    
    return (0 < written) && (written < dest_size);
}

void parse_video(cJSON* videoRenderer, const char* author_id_override, const bool allow_youtube_shorts, SearchResult* video)
{
    if ((videoRenderer == NULL) || (video == NULL) || (author_id_override == NULL)) return;

    video->media_type = VIDEO;

    if (video_is_youtube_short(videoRenderer)) {
        if (allow_youtube_shorts == false){
            video->media_type = UNDF;
            return;
        }

        else video->media_type = SHORT;
    }

    const char* id_path = ".videoId";

    if (assign_string_from_path(videoRenderer, id_path, video->id, sizeof(video->id)) == false) {
        printf("parse_video: id assign fail (json path: \"%s\")\n", id_path);
        video->media_type = UNDF;
        return;
    }

    if (assign_video_thumbnail_path(video->id, video->thumbnail_path, sizeof(video->thumbnail_path)) == false) {
        printf("parse_video: thumbnail path fail\n");
    }
    
    const char* title_path = ".title.runs[0].text";
    
    if (assign_string_from_path(videoRenderer, title_path, video->title, sizeof(video->title)) == false) {
        printf("parse_video: title assign fail (json path: \"%s\")\n", title_path);
    }

    if (author_id_override[0] != '\0') {
        strncpy(video->authorId, author_id_override, sizeof(video->authorId) - 1);
        video->authorId[sizeof(video->authorId) - 1] = '\0';
    }

    else {
        const char* author_id_path = author_id_override[0] != '\0' ? author_id_override : ".longBylineText.runs[0].navigationEndpoint.browseEndpoint.browseId";
        
        if (assign_string_from_path(videoRenderer, author_id_path, video->authorId, sizeof(video->authorId)) == false) {
            printf("parse_video: author id assign fail (json path: %s)\n", author_id_path);
        }
    }

    if (video_is_live(videoRenderer)) {
        video->media_type = LIVE;

        const char* live_viewers_path = ".viewCountText.runs[0].navigationEndpoint.browseEndpoint.browseId";

        if (assign_string_from_path(videoRenderer, live_viewers_path, video->view_count, sizeof(video->view_count)) == false) {
            video->view_count[0] = '0';
        }

        return;
    }

    const char* view_count_path = ".viewCountText.simpleText";

    if (assign_string_from_path(videoRenderer, view_count_path, video->view_count, sizeof(video->view_count))) {
        format_view_count(video->view_count, sizeof(video->view_count));
    }
    
    else snprintf(video->view_count, sizeof(video->view_count), "no views");

    const char* video_age_path = ".publishedTimeText.simpleText";

    if (assign_string_from_path(videoRenderer, video_age_path, video->date_published, sizeof(video->date_published)) == false) {
        printf("parse_video: date published assign fail (json path: \"%s\")\n", video_age_path);
    }

    const char* length_path = ".lengthText.simpleText";

    if (assign_string_from_path(videoRenderer, length_path, video->duration, sizeof(video->duration)) == false) {
        printf("parse_video: length assign fail (json path: \"%s\")\n", length_path);
    }
}

void parse_related_video(cJSON* lockupViewModel, SearchResult* related_vid)
{
    if ((lockupViewModel == NULL) || (related_vid == NULL)) return;

    const char* id_path = ".contentId";

    if (!assign_string_from_path(lockupViewModel, id_path, related_vid->id, sizeof(related_vid->id))) {
        printf("parse_related_video: id assign fail (json path: \"%s\")\n", id_path);
        related_vid->media_type = UNDF;
        return;
    }

    related_vid->media_type = VIDEO;

    if (!assign_video_thumbnail_path(related_vid->id, related_vid->thumbnail_path, sizeof(related_vid->thumbnail_path))) {
        printf("parse_related_video: thumbnail path fail\n");
    } 

    const char* title_path = ".metadata.lockupMetadataViewModel.title.content";

    if (!assign_string_from_path(lockupViewModel, title_path, related_vid->title, sizeof(related_vid->title))) {
        printf("parse_related_video: title assign fail (json path: \"%s\")\n", title_path);
    }

    const char* author_id_path = ".metadata.lockupMetadataViewModel.image.decoratedAvatarViewModel.rendererContext.commandContext.onTap.innertubeCommand.browseEndpoint.browseId";

    if (assign_string_from_path(lockupViewModel, author_id_path, related_vid->authorId, sizeof(related_vid->authorId)) == false) {
        printf("parse_related_video: author id assign fail\n");
    }

    const char* duration_path = ".contentImage.thumbnailViewModel.overlays[0].thumbnailOverlayBadgeViewModel.thumbnailBadges[0].thumbnailBadgeViewModel.text";

    if (!assign_string_from_path(lockupViewModel, duration_path, related_vid->duration, sizeof(related_vid->duration))) {
        printf("parse_related_video: duration assign fail (json path: \"%s\")\n", duration_path);
    }

    const char* view_count_path = ".metadata.lockupMetadataViewModel.metadata.contentMetadataViewModel.metadataRows[1].metadataParts[0].text.content";

    if (!assign_string_from_path(lockupViewModel, view_count_path, related_vid->view_count, sizeof(related_vid->view_count))) {
        printf("parse_related_video: view count assign fail (json path: \"%s\")\n", view_count_path);
    }

    const char* date_published_path = ".metadata.lockupMetadataViewModel.metadata.contentMetadataViewModel.metadataRows[1].metadataParts[1].text.content";

    if (!assign_string_from_path(lockupViewModel, date_published_path, related_vid->date_published, sizeof(related_vid->date_published))) {
        printf("parse_related_video: duration assign fail (json path: \"%s\")\n", date_published_path);
    }
}

void parse_playlist_video(cJSON* playlistVideoRenderer, SearchResult* playlist_vid)
{
    if ((playlistVideoRenderer == NULL) || (playlist_vid == NULL)) return;

    const char* id_path = ".videoId";

    if (!assign_string_from_path(playlistVideoRenderer, id_path, playlist_vid->id, sizeof(playlist_vid->id))) {
        printf("parse_playlist_video: id assign fail (json path: \"%s\")\n", id_path);
        playlist_vid->media_type = UNDF;
        return;
    }

    playlist_vid->media_type = VIDEO;

    if (!assign_video_thumbnail_path(playlist_vid->id, playlist_vid->thumbnail_path, sizeof(playlist_vid->thumbnail_path))) {
        printf("parse_playlist_video: thumbnail path fail\n");
    }

    const char* title_path = ".title.runs[0].text";

    if (!assign_string_from_path(playlistVideoRenderer, title_path, playlist_vid->title, sizeof(playlist_vid->title))) {
        printf("parse_playlist_video: title assign fail (json path: \"%s\")\n", title_path);
    }

    const char* author_id_path = ".shortBylineText.runs[0].navigationEndpoint.browseEndpoint.browseId";

    if (assign_string_from_path(playlistVideoRenderer, author_id_path, playlist_vid->authorId, sizeof(playlist_vid->authorId)) == false) {
        printf("parse_playlist_video: author id assign fail (json path: %s)\n", author_id_path);
    }

    const char* length_path = ".lengthText.simpleText";

    if (!assign_string_from_path(playlistVideoRenderer, length_path, playlist_vid->duration, sizeof(playlist_vid->duration))) {
        printf("parse_playlist_video: duration assign fail (json path: \"%s\")\n", length_path);
    }

    const char* views_path = ".videoInfo.runs[0].text";

    if (assign_string_from_path(playlistVideoRenderer, views_path, playlist_vid->view_count, sizeof(playlist_vid->view_count))) {
        char* end = strstr(playlist_vid->view_count, " views");
        if (end == NULL) strncat(playlist_vid->view_count, " views", sizeof(playlist_vid->view_count) - strlen(playlist_vid->view_count) - 1);
    }

    else printf("parse_playlist_video: views assign fail (json path: \"%s\")\n", views_path);

    const char* publish_date_path = ".videoInfo.runs[2].text";

    if (!assign_string_from_path(playlistVideoRenderer, publish_date_path, playlist_vid->date_published, sizeof(playlist_vid->date_published))) {
        printf("parse_playlist_video: date published assign fail (json path: \"%s\")\n", publish_date_path);
    }
}

void parse_channel(cJSON* channelRenderer, SearchResult* channel)
{
    if ((channelRenderer == NULL) || (channel == NULL)) return;

    const char* id_path = ".channelId";

    if (!assign_string_from_path(channelRenderer, id_path, channel->id, sizeof(channel->id))) {
        printf("parse_channel: id assign fail (json path: \"%s\")\n", id_path);
        channel->media_type = UNDF;
        return;
    }

    channel->media_type = CHANNEL;

    const char* title_path = ".title.simpleText";

    if (!assign_string_from_path(channelRenderer, title_path, channel->title, sizeof(channel->title))) {
        printf("parse_channel: title assign fail (json path: \"%s\")\n", title_path);
    }

    const char* sub_count_path = ".videoCountText.simpleText";

    if (!assign_string_from_path(channelRenderer, sub_count_path, channel->subscriber_count, sizeof(channel->subscriber_count))) {
        printf("parse_channel: subscriber count assign fail (json path: \"%s\")\n", sub_count_path);
    }

    const cJSON* channelThumbnailLink = cjson_pointer_get(channelRenderer, ".thumbnail.thumbnails[0].url");
    if (valid_cjson_string(channelThumbnailLink)) {
        // the path either starts with '/ytc', or just '/'
        const char* path1 = strstr(channelThumbnailLink->valuestring, "/ytc");
        const char* path2 = strrchr(channelThumbnailLink->valuestring, '/');
        snprintf(channel->thumbnail_path, sizeof(channel->thumbnail_path), "%s", (path1 ? path1 : path2));
    }
}

void parse_playlist(cJSON *lockupViewModel, SearchResult *playlist)
{
    if ((lockupViewModel == NULL) || (playlist == NULL)) return;

    const char* id_path = ".contentId";

    if (!assign_string_from_path(lockupViewModel, id_path, playlist->id, sizeof(playlist->id))) {
        printf("parse_playlist: id assign fail (json path: \"%s\")\n", id_path);
        playlist->media_type = UNDF;
        return;
    }
    
    playlist->media_type = PLAYLIST;

    const char* title_path = ".metadata.lockupMetadataViewModel.title.content";

    if (!assign_string_from_path(lockupViewModel, title_path, playlist->title, sizeof(playlist->title))) {
        printf("parse_playlist: title assign fail (json path: \"%s\")\n", title_path);
    }

    const char* first_video_id_path = ".rendererContext.commandContext.onTap.innertubeCommand.watchEndpoint.videoId";

    char video_id[16];
    if (assign_string_from_path(lockupViewModel, first_video_id_path, video_id, sizeof(video_id))) {
        if (!assign_video_thumbnail_path(video_id, playlist->thumbnail_path, sizeof(playlist->thumbnail_path))) {
            printf("parse_playlist: thumbnail path assign fail\n");
        }
    }
    
    else printf("parse_playlist: video id assign fail (json path: \"%s\")\n", first_video_id_path);

    const char* video_count_path = ".contentImage.collectionThumbnailViewModel.primaryThumbnail.thumbnailViewModel.overlays[0].thumbnailOverlayBadgeViewModel.thumbnailBadges[0].thumbnailBadgeViewModel.text";

    if (!assign_string_from_path(lockupViewModel, video_count_path, playlist->video_count, sizeof(playlist->video_count))) {
        printf("parse_playlist: video count assign fail (json path: \"%s\")\n", video_count_path);
    }
}

const char* get_results_list_path(const SearchType search_type, const SearchAttribute search_attr)
{
    switch (search_type) {
        case SEARCH_TYPE_QUERIED: 
            if (search_attr == SEARCH_ATTR_REPLACE) 
                return ".contents.twoColumnSearchResultsRenderer.primaryContents.sectionListRenderer.contents[0].itemSectionRenderer.contents";
            if (search_attr == SEARCH_ATTR_APPENDING)
                return ".onResponseReceivedCommands[0].appendContinuationItemsAction.continuationItems[0].itemSectionRenderer.contents";
        case SEARCH_TYPE_RELATED: 
            if (search_attr == SEARCH_ATTR_REPLACE)
                return "contents.twoColumnWatchNextResults.secondaryResults.secondaryResults.results";
            if (search_attr == SEARCH_ATTR_APPENDING)
                return ".onResponseReceivedEndpoints[0].appendContinuationItemsAction.continuationItems";
        case SEARCH_TYPE_VIEW_PLAYLIST: 
            if (search_attr == SEARCH_ATTR_REPLACE)
                return ".contents.twoColumnBrowseResultsRenderer.tabs[0].tabRenderer.content.sectionListRenderer.contents[0].itemSectionRenderer.contents[0].playlistVideoListRenderer.contents";
            if (search_attr == SEARCH_ATTR_APPENDING)
                return ".onResponseReceivedActions[0].appendContinuationItemsAction.continuationItems";
        case SEARCH_TYPE_VIEW_CHANNEL: 
            if (search_attr == SEARCH_ATTR_REPLACE) 
                return ".contents.twoColumnBrowseResultsRenderer.tabs[1].tabRenderer.content.richGridRenderer.contents";
            if (search_attr == SEARCH_ATTR_APPENDING)
                return ".onResponseReceivedActions[0].appendContinuationItemsAction.continuationItems";
        case SEARCH_TYPE_TRENDING: 
            return ".contents.twoColumnBrowseResultsRenderer.tabs[0].tabRenderer.content.sectionListRenderer.contents[2].itemSectionRenderer.contents[0].shelfRenderer.content.expandedShelfContentsRenderer.items";
        // should never happen...
        case SEARCH_TYPE_VIDEO_FOCUS: 
            break;
    }

    return NULL;
}

int create_results_from_json(cJSON* json, Results *results, const SearchType search_type, const SearchAttribute search_attr, const bool allow_youtube_shorts)
{
    const char* path = get_results_list_path(search_type, search_attr);

    cJSON* results_array = cjson_pointer_get(json, path);
    if ((results_array == NULL) || (cJSON_IsArray(results_array) == false)) {
        printf("create_results_from_json: invalid results array from path %s\n", path);
        return -1;
    }

    char author_id[64] = {0};
    if (search_type == SEARCH_TYPE_VIEW_CHANNEL) {
        const char* author_id_path = (search_attr == SEARCH_ATTR_REPLACE) 
                                     ? ".contents.twoColumnBrowseResultsRenderer.tabs[0].tabRenderer.endpoint.browseEndpoint.browseId"
                                     : ".responseContext.serviceTrackingParams[0].params[3].value";

        if (assign_string_from_path(json, author_id_path, author_id, sizeof(author_id)) == false) {
            printf("funct: failed to parse author id from the path %s\n", author_id_path);
        }
    }

    int elements_added = 0;

    cJSON *item;
    cJSON_ArrayForEach (item, results_array) {
        SearchResult *search_result = init_search_result();
        if (search_result == NULL) {
            printf("create_results_from_json: init_search_result returned NULL\n");
            return 0;
        }
        
        cJSON* videoRenderer   =       cjson_pointer_get(item, ".videoRenderer");     // video
        cJSON* lockupViewModel =       cjson_pointer_get(item, ".lockupViewModel");   // playlist or related video container
        cJSON* playlistVideoRenderer = cjson_pointer_get(item, ".playlistVideoRenderer"); // video object in playlist container
        cJSON* channelRenderer =       cjson_pointer_get(item, ".channelRenderer");   // channel
        cJSON* richItemRenderer =      cjson_pointer_get(item, ".richItemRenderer.content.videoRenderer"); // videos in channel window
        
        if      (videoRenderer)         parse_video(videoRenderer, author_id, allow_youtube_shorts, search_result);
        else if (richItemRenderer)      parse_video(richItemRenderer, author_id, allow_youtube_shorts,search_result);
        else if (playlistVideoRenderer) parse_playlist_video(playlistVideoRenderer, search_result);
        else if (channelRenderer)       parse_channel(channelRenderer, search_result);
        else if (lockupViewModel) {
            if (search_type == SEARCH_TYPE_RELATED) parse_related_video(lockupViewModel, search_result);
            else parse_playlist(lockupViewModel, search_result);
        }

        if (search_result->media_type != UNDF) {
            elements_added++;
            add_search_result(results, search_result);
        }

        else free_search_result(search_result);
    }

    return elements_added;
}

const char* search_type_to_text(const SearchType search_type)
{
    switch (search_type) {
        case SEARCH_TYPE_QUERIED: return "QUERIED";
        case SEARCH_TYPE_RELATED: return "RELATED";
        case SEARCH_TYPE_TRENDING: return "TRENDING";
        case SEARCH_TYPE_VIDEO_FOCUS: return "VIDEO FOCUS";
        case SEARCH_TYPE_VIEW_PLAYLIST: return "VIEW PLAYLIST";
        case SEARCH_TYPE_VIEW_CHANNEL: return "VIEW CHANNEL";
        default:
            return NULL;
    }
}

const char* search_attr_to_text(const SearchAttribute search_attr)
{
    switch (search_attr) {
        case SEARCH_ATTR_REPLACE: return "NEW";
        case SEARCH_ATTR_APPENDING: return "APPENDING";
        default:
            return NULL;
    }
}

const char* get_continuation_token_path(const SearchType search_type, const SearchAttribute search_attr)
{
    switch (search_type) {
        case SEARCH_TYPE_QUERIED:
            if (search_attr == SEARCH_ATTR_REPLACE)
                return ".contents.twoColumnSearchResultsRenderer.primaryContents.sectionListRenderer.contents[1].continuationItemRenderer.continuationEndpoint.continuationCommand.token";
            if (search_attr == SEARCH_ATTR_APPENDING)
                return ".onResponseReceivedCommands[0].appendContinuationItemsAction.continuationItems[1].continuationItemRenderer.continuationEndpoint.continuationCommand.token";
        case SEARCH_TYPE_RELATED:
            if (search_attr == SEARCH_ATTR_REPLACE)
                return ".contents.twoColumnWatchNextResults.secondaryResults.secondaryResults.results[-1].continuationItemRenderer.continuationEndpoint.continuationCommand.token";
            if (search_attr == SEARCH_ATTR_APPENDING)
                return ".onResponseReceivedEndpoints[0].appendContinuationItemsAction.continuationItems[-1].continuationItemRenderer.continuationEndpoint.continuationCommand.token";
        case SEARCH_TYPE_VIEW_PLAYLIST:
            if (search_attr == SEARCH_ATTR_REPLACE)
                return ".contents.twoColumnBrowseResultsRenderer.tabs[0].tabRenderer.content.sectionListRenderer.contents[0].itemSectionRenderer.contents[0].playlistVideoListRenderer.contents[-1].continuationItemRenderer.continuationEndpoint.commandExecutorCommand.commands[1].continuationCommand.token";
            if (search_attr == SEARCH_ATTR_APPENDING)
                return ".onResponseReceivedActions[0].appendContinuationItemsAction.continuationItems[-1].continuationItemRenderer.continuationEndpoint.continuationCommand.token";
        case SEARCH_TYPE_VIEW_CHANNEL:
            if (search_attr == SEARCH_ATTR_REPLACE)
                return ".contents.twoColumnBrowseResultsRenderer.tabs[1].tabRenderer.content.richGridRenderer.contents[-1].continuationItemRenderer.continuationEndpoint.continuationCommand.token";
            if (search_attr == SEARCH_ATTR_APPENDING)
                return ".onResponseReceivedActions[0].appendContinuationItemsAction.continuationItems[-1].continuationItemRenderer.continuationEndpoint.continuationCommand.token";
        case SEARCH_TYPE_TRENDING:
        case SEARCH_TYPE_VIDEO_FOCUS:
            return NULL;
    }
}


void delete_n_results(Results* results, const size_t n)
{
    if (results == NULL || (n > results->count)) return;

    for (int i = 0; results->head && i < n; i++) {
        SearchResult* to_del = results->head;
        results->count--;
        results->head = results->head->next;
        free_search_result(to_del);
    }
}

static bool search_finished = true;

void* get_results_from_query(void* args)
{
    float start_time = GetTime(); // preformance check
    
    cJSON* json = NULL;
    HTTPS_Response response = create_https_response();
    SearchThreadArgs* targs = (SearchThreadArgs*) args;
    if (targs == NULL) {
        printf("get_results_from_query: 'targs' is NULL\n");
        SetWindowTitle("[failed] - metube");
        goto cleanup;
    }

    response = send_https_request(targs->request, targs->youtube_connection);
    if (https_response_ready(&response) == false) {
        printf("get_results_from_query: invalid https response\n");
        SetWindowTitle("[failed] - metube");
        goto cleanup;
    }

    create_file_from_memory("body.json", response.body);

    json = cJSON_Parse(response.body.data);
    if (json == NULL) {
        printf("get_results_from_query: cJSON_Parse returned NULL\n");
        SetWindowTitle("[failed] - metube");
        goto cleanup;
    }

    const SearchType search_type = targs->query->search_type;
    const SearchAttribute search_attr = targs->query->search_attr;
    
    pthread_mutex_lock(&targs->search_results->mutex);
    const int old_size = targs->search_results->count;
    const int elements_added = create_results_from_json(json, targs->search_results, search_type, search_attr, targs->query->allow_youtube_shorts);
    pthread_mutex_unlock(&targs->search_results->mutex);

    if (elements_added < 0) {
        printf("get_results_from_query: invalid elements added\n");
        goto cleanup;
    }

    char** dest = &targs->query->continuation_token;
    if (*dest) {
        free((*dest)); (*dest) = NULL;
    }

    const char* continuation_path = get_continuation_token_path(search_type, search_attr);

    const cJSON* token_obj = cjson_pointer_get(json, continuation_path);
    if (valid_cjson_string(token_obj)) {
        (*dest) = strdup(token_obj->valuestring);
    }

    else printf("get_results_from_query: failed to parse continuation token (path: %s)\n", continuation_path);

    const float search_time = GetTime() - start_time;
    const char* type_text = search_type_to_text(search_type);
    const char* attr_text = search_attr_to_text(search_attr);
    printf("%s (%s) took %f seconds, %d items found\n", type_text, attr_text, search_time, elements_added);
    
    if (search_attr == SEARCH_ATTR_REPLACE) {
        pthread_mutex_lock(&targs->search_results->mutex);
        delete_n_results(targs->search_results, old_size);
        pthread_mutex_unlock(&targs->search_results->mutex);
    }

    SetWindowTitle(TextFormat("[search results(%zu)] - metube", targs->search_results->count));

    cleanup:
        search_finished = true;
        if (targs->request.body) free(targs->request.body);
        if (https_response_ready(&response)) free_https_response(&response);
        if (json) cJSON_Delete(json);
        if (targs) free(targs);
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
    return (Rectangle) { rect.x + padding, rect.y + padding, rect.width - (padding * 2), rect.height - (padding * 2) };
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

void get_internal_api_key(const char* response_body, const size_t n, char* internal_api_key)
{
    if (response_body == NULL) return;

    const char* tag = "\"INNERTUBE_API_KEY\"";
    const size_t tag_len = strlen(tag);

    char* location = strstr(response_body, tag);
    if (location == NULL) {
        printf("get_internal_api_key: \"%s\" not found\n", tag);
        return;
    }

    int i = 0;
    bool in_quotes = false;

    for (char* current = location + tag_len; (current && (i < n - 1)); current++) {
        const char c = *current;

        if (!in_quotes) {
            if (c == '\"') 
                in_quotes = true;
        }
        
        else if (in_quotes) {
            if (c == '\"') {
                break;
            }

            internal_api_key[i++] = c;
        }
    }

    internal_api_key[i] = '\0';
}

void parse_youtube_page(PersistentConnection *youtube_connection, const size_t n, char* internal_api_key)
{
    PreparedRequest request = {
        .path = "/",
        .header = "",
        .body = NULL,
    };

    configure_get_header(sizeof(request.header), request.header, youtube_connection->host, request.path);

    HTTPS_Response youtube_page_response = send_https_request(request, youtube_connection);
    if (https_response_ready(&youtube_page_response) == false) {
        memset(internal_api_key, 0, n);
        printf("parse_youtube_page: page response is invalid\n");
        return;
    }

    get_internal_api_key(youtube_page_response.body.data, n, internal_api_key);

    free_https_response(&youtube_page_response);
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
        case SHORT:
        case VIDEO:
            DrawTextBoxed(TextFormat("%s   %s", search_result->date_published, search_result->view_count), padded_rectangle(ui.padding, subtext_area), ui, 11.5, BLACK);
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

bool cached_texture_is_ready(TextureCacheEntry* cached_texture)
{
    return cached_texture && IsTextureReady(cached_texture->thumbnail);
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

int anticipate_lines_wordwrap(Font font, const char* text, float fontSize, float spacing, float maxWidth)
{
    if (!text) return 0;

    int lines = 1;
    float line_width = 0.0f;

    const char* word_start = text;
    while (*word_start) {
        if (*word_start == '\n') {
            lines++;
            line_width = 0;
            word_start++;
            continue;
        }

        const char* word_end = word_start;
        while (*word_end && *word_end != ' ' && *word_end != '\n') word_end++;

        int word_len = word_end - word_start;
        char word_buf[256];
        strncpy(word_buf, word_start, word_len);
        word_buf[word_len] = '\0';

        Vector2 size = MeasureTextEx(font, word_buf, fontSize, spacing);

        if (line_width + size.x > maxWidth) {
            lines++;
            line_width = 0;
        }

        line_width += size.x;

        if (*word_end == ' ') {
            Vector2 space_size = MeasureTextEx(font, " ", fontSize, spacing);
            line_width += space_size.x;
            word_end++;
        }

        word_start = word_end;
    }

    return lines;
}

int get_level_string(const int level, const char* spec_link, const size_t n, char level_parameters[n])
{
    if (spec_link == NULL) return 0;

    const char LEVEL_SEPERATOR = '|';

    bool in_correct_level = false;
    int current_level = 0;

    int i = 0;
    for (const char* ptr = spec_link; *ptr; ptr++) {
        if (*ptr == LEVEL_SEPERATOR) {
            current_level++;
            if (in_correct_level) break;
            else if (current_level == level) in_correct_level = true;
        }

        else if (in_correct_level && i < n) {
            level_parameters[i++] = *ptr;
        }
    }

    level_parameters[i] = '\0';
    
    return i;
}

const float seconds_to_microseconds(const float seconds)
{
    return (seconds * 1e3);
}

bool queue_thumbnail_load(const char* search_result_id, const char* thumbnail_path, PersistentConnection* conn)
{
    if ((search_result_id == NULL) || (thumbnail_path == NULL) || (conn == NULL)) return false;

    PreparedRequest req = {0};
    if (configure_get_header(sizeof(req.header), req.header, conn->host, thumbnail_path) == false) {
        printf("queue_thumbnail_load: rew header truncated\n");
        return false;
    }

    LoadThumbnailArgs* targs = malloc(sizeof(LoadThumbnailArgs));
    if (targs == NULL) {
        printf("queue_thumbnail_load: malloc returned NULL for 'targs'\n");
        return false;
    }

    targs->request = req;
    targs->connection = conn;
    targs->thumbnail_queue = &thumbnail_queue;
    strncpy(targs->search_result_id, search_result_id, sizeof(targs->search_result_id) - 1);

    return launch_task(&task_queue, targs, load_thumbnail);
}

#define CLIENT_NAME "WEB"
#define CLIENT_VER "2.20250730"
#define YT_API_PLAYLIST_BROWSE_ID_PREFIX "VL"    // "video list" (playlist)
#define YT_API_BROWSE_ID_TRENDING "FEtrending"   // "frontend trending"
#define YT_API_CHANNEL_VIDEOS_PARAMS "EgZ2aWRlb3PyBgQKAjoA"  // filters to "Videos" tab in a channel's homepage
#define USER_AGENT "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/130.0.0.0 Safari/537.36"

bool valid_post_request(const PreparedRequest post)
{
    return (post.path[0] != '\0') && (post.header[0] != '\0') && (post.body) && (post.body[0] != '\0');
}

bool init_post_header(const size_t n, char post_header[n], const char *host, const char *path, const size_t post_body_length)
{
    const size_t len = snprintf(post_header, n,
                        "POST %s HTTP/1.1\r\n"
                        "Host: %s\r\n"
                        "User-Agent: " USER_AGENT "\r\n"
                        "Content-Type: application/json\r\n"
                        "Accept: application/json\r\n"
                        "Content-Length: %zu\r\n"
                        "Connection: keep-alive\r\n"
                        "\r\n",
                        path, host, post_body_length);
    
    return (len > 0) && (len < n);
}

bool add_queried_search_payload(cJSON* root, const Query* q)
{
    if ((root == NULL) || (q == NULL) || (q->string[0] == '\0')) {
        printf("add_queried_search_payload: invalid input\n");
        return false;
    }

    const char* sort_url = sort_type_to_url(q->sort);
    const char* media_url = media_type_to_url(q->media);

    char params[16];
    const int len = snprintf(params, sizeof(params), "%s%s",  sort_url, media_url);
    if (len < 0 || len >= sizeof(params)) {
        printf("add_queried_search_payload: snprintf returned %d\n", len);
        return false;
    }

    if (cJSON_AddStringToObject(root, "query", q->string) == NULL) {
        printf("add_queried_search_payload: failed to add 'query'\n");
        return false;
    }

    if (cJSON_AddStringToObject(root, "params", params) == NULL) {
        printf("add_queried_search_payload: failed to add 'params'\n");
        return false;
    }

    return true;
}

bool add_view_channel_videos_payload(cJSON* root, const Query* q)
{
    if ((root == NULL) || (q == NULL) || (q->focused_id[0] == '\0')) {
        printf("add_view_channel_videos_payload: invalid input\n");
        return false;
    }

    if (cJSON_AddStringToObject(root, "browseId", q->focused_id) == NULL) {
        printf("add_view_channel_videos_payload: failed to add 'browseId\n");
        return false;
    }

    if (cJSON_AddStringToObject(root, "params", YT_API_CHANNEL_VIDEOS_PARAMS) == NULL) {
        printf("add_view_channel_videos_payload: failed to add 'browseId\n");
        return false;
    }

    return true;
}

bool add_view_playlist_videos_payload(cJSON* root, const Query* q)
{
    if ((root == NULL) || (q == NULL) || (q->focused_id[0] == '\0')) {
        printf("add_view_playlist_videos_payload: invalid input\n");
        return false;
    }

    char playlist_browse_id[64];
    const int len = snprintf(playlist_browse_id, sizeof(playlist_browse_id), YT_API_PLAYLIST_BROWSE_ID_PREFIX "%s", q->focused_id);
    if ((len < 0) || (len >= sizeof(playlist_browse_id))) {
        printf("add_view_playlist_videos_payload: snprintf returned %d\n", len);
        return false;
    }

    if (cJSON_AddStringToObject(root, "browseId", playlist_browse_id) == NULL) {
        printf("add_view_playlist_videos_payload: failed to add 'browseId'\n");
        return false;
    }

    return true;
}

bool add_view_trending_videos_payload(cJSON* root, const Query* q)
{
    if ((root == NULL) || (q == NULL)) {
        printf("add_view_trending_videos_payload: invalid input\n");
        return false;
    }

    if (cJSON_AddStringToObject(root, "browseId", YT_API_BROWSE_ID_TRENDING) == NULL) {
        printf("add_view_trending_videos_payload: failed to add 'browseId'\n");
        return false;
    }

    return true;
}

bool add_view_related_videos_payload(cJSON* root, const Query* q)
{
    if ((root == NULL) || (q == NULL) || (q->focused_id[0] == '\0')) {
        printf("add_view_related_videos_payload: invalid input\n");
        return false;
    }

    if (cJSON_AddStringToObject(root, "videoId", q->focused_id) == NULL) {
        printf("add_view_related_videos_payload: failed to add 'videoId'\n");
        return false;
    }

    return true;
}

bool add_continuation_payload(cJSON* root, const char* continuation_token)
{
    if ((root == NULL) || (continuation_token == NULL)) {
        printf("add_continuation_payload: invalid input\n");
        return false;
    }

    return cJSON_AddStringToObject(root, "continuation", continuation_token);
}

cJSON* init_payload_root()
{
    cJSON* client = cJSON_CreateObject();
    cJSON* context = cJSON_CreateObject();
    cJSON* root = cJSON_CreateObject();

    if ((client == NULL) || (context == NULL) || (root == NULL)) {
        cJSON_Delete(client);  
        cJSON_Delete(context); 
        cJSON_Delete(root);    
        return NULL;
    }

    cJSON_AddStringToObject(client, "clientName", CLIENT_NAME);
    cJSON_AddStringToObject(client, "clientVersion", CLIENT_VER);
    cJSON_AddItemToObject(context, "client", client);  
    cJSON_AddItemToObject(root, "context", context);  

    return root; 
}

cJSON* init_payload(const Query* q)
{   
    if (!q) {
        printf("init_payload: invalid input\n");
        return NULL;
    }

    cJSON* root = init_payload_root();
    if (!root) {
        printf("init_payload: failed to init root\n");
        return NULL;
    }

    if ((q->search_attr == SEARCH_ATTR_APPENDING) && (q->continuation_token) && (q->continuation_token[0] != '\0')) {
        if (add_continuation_payload(root, q->continuation_token) == false) cJSON_Delete(root);
        return root;
    }

    switch (q->search_type) {
        case SEARCH_TYPE_RELATED:
        case SEARCH_TYPE_VIDEO_FOCUS: 
            if (add_view_related_videos_payload(root, q) == false) {
                cJSON_Delete(root); root = NULL;
            }
            break;
        case SEARCH_TYPE_QUERIED: 
            if (add_queried_search_payload(root, q) == false) {
                cJSON_Delete(root); root = NULL;
            }
            break;
        case SEARCH_TYPE_VIEW_CHANNEL: 
            if (add_view_channel_videos_payload(root, q) == false) {
                cJSON_Delete(root); root = NULL;
            }
            break;
        case SEARCH_TYPE_VIEW_PLAYLIST: 
            if (add_view_playlist_videos_payload(root, q) == false) {
                cJSON_Delete(root); root = NULL;
            } 
            break;
        case SEARCH_TYPE_TRENDING: 
            if (add_view_trending_videos_payload(root, q) == false) {
                cJSON_Delete(root); root = NULL;
            }
            break;
    }

    return root;
}

PreparedRequest init_post_request(const Query query, const char* internal_api_key, const char* host)
{
    PreparedRequest req = (PreparedRequest) {0};

    if ((internal_api_key == NULL) || (host == NULL)) {
        printf("init_post_request: invalid input\n");
        return (PreparedRequest) {0};
    }

    if (resolve_youtube_api_path(sizeof(req.path), req.path, query.search_type, internal_api_key) == false) {
        printf("init_post_request: failed to resolve path\n");
        return (PreparedRequest) {0};
    }

    cJSON* payload = init_payload(&query);

    if (payload == NULL) {
        printf("init_post_request: 'payload' is NULL'\n");
        return (PreparedRequest) {0};
    }

    req.body = cJSON_Print(payload);

    if (init_post_header(sizeof(req.header), req.header, host, req.path, strlen(req.body)) == false) {
        printf("init_post_request: failed to configure header\n");
        free(req.body); req.body = NULL;
        cJSON_Delete(payload); payload = NULL;
        return (PreparedRequest) {0};
    }
    
    cJSON_Delete(payload); payload = NULL;
    
    return req;
}

bool queue_search_task(Query* query, PersistentConnection* conn, Results* results, const char* internal_api_key)
{
    if ((conn == NULL) || (results == NULL) || (internal_api_key == NULL)) {
        printf("queue_search_task: invalid input\n");
        return false;
    }

    PreparedRequest post = init_post_request((*query), internal_api_key, conn->host);
    if (valid_post_request(post) == false) {
        printf("queue_search_task: invalid post req\n");
        return false;
    }

    SearchThreadArgs* targs = init_search_thread_args(query, post, results, &thumbnail_queue, conn);
    if (!targs) {
        printf("queue_search_task: 'targs' is NULL\n");
        return false;
    }

    return launch_task(&task_queue, targs, get_results_from_query);
}

typedef struct
{
    char id[64];
    char author_id[64];
    char title[512];
    char description[2048];
} HighlightedVideo;

void draw_highlighted_video(const Rectangle container, Ui ui, Vector2* scrollbar_position, HighlightedVideo* highlighted_video)
{
    const Color text_color = BLACK;
    const int title_font_size = 17;
    const int video_desc_font_size = 12;
    const int spacing = 2;

    const Rectangle scroll_window_area = {
        .x = container.x,
        .y = container.y + (container.height * 0.25f),
        .width = container.width - ui.padding,
        .height = container.height * 0.75f,
    };

    const float padded_width = container.width - ui.padding - ui.padding;

    const float title_line_height = title_font_size + spacing;
    const int n_title_lines = anticipate_lines_wordwrap(ui.font, highlighted_video->title, title_font_size, spacing, padded_width);
    const float title_text_height = title_line_height * (n_title_lines + 1);

    const float video_desc_line_height = video_desc_font_size + spacing;
    const int n_desc_lines = anticipate_lines_wordwrap(ui.font, highlighted_video->description, video_desc_font_size, spacing, padded_width);
    const float video_desc_text_height = video_desc_line_height * (n_desc_lines);
    
    const Rectangle scroll_content_area = {
        .x = scroll_window_area.x,
        .y = scroll_window_area.y,
        .width = scroll_window_area.width,
        .height = title_text_height + video_desc_text_height,
    };

    GuiScrollPanel(scroll_window_area, NULL, scroll_content_area, scrollbar_position, NULL, false);

    const Rectangle title_bounds = {
        .x = scroll_window_area.x,
        .y = scroll_window_area.y + scrollbar_position->y,
        .height = title_text_height,
        .width = scroll_window_area.width,
    };
    
    const Rectangle video_desc_bounds = {
        .x = scroll_content_area.x,
        .y = title_bounds.y + title_bounds.height + scrollbar_position->y,
        .height = video_desc_text_height,
        .width = scroll_content_area.width,
    };

    BeginScissorMode(scroll_window_area.x, scroll_window_area.y, scroll_window_area.width, scroll_window_area.height);

    const Rectangle padded_title_bounds = padded_rectangle(ui.padding, title_bounds);
    DrawTextBoxed(highlighted_video->title, padded_title_bounds, ui, title_font_size, text_color);
    
    const Rectangle padded_video_desc_bounds = padded_rectangle(ui.padding, video_desc_bounds);
    DrawTextBoxed(highlighted_video->description, padded_video_desc_bounds, ui, video_desc_font_size, text_color);

    EndScissorMode();
}

typedef struct
{
    PreparedRequest req;
    PersistentConnection* conn;
    HighlightedVideo* highlighted_video;
} FocusedInfoArgs;

FocusedInfoArgs* init_focused_info_args(PreparedRequest req, PersistentConnection* conn, HighlightedVideo* highlighted_video)
{
    if ((conn == NULL) || (highlighted_video == NULL)) {
        printf("init_focused_info_args: invalid input\n");
        return NULL;
    }

    FocusedInfoArgs* targs = malloc(sizeof(FocusedInfoArgs));
    if (targs == NULL) {
        printf("init_focused_info_args: malloc returned NULL for 'targs'\n");
        return NULL;
    }

    targs->req = req;
    targs->conn = conn;
    targs->highlighted_video = highlighted_video;

    return targs;
}

void* get_focused_video_information(void* args)
{
    cJSON* json = NULL;
    HTTPS_Response response = create_https_response();
    FocusedInfoArgs* targs = (FocusedInfoArgs*) args;
    if (targs == NULL) {
        printf("get_focused_video_information: 'targs' is NULL\n");
        goto cleanup;
    }

    response = send_https_request(targs->req, targs->conn); 
    if (https_response_ready(&response) == false) {
        printf("get_focused_video_information: invaild https response\n");
        goto cleanup;
    }

    json = cJSON_Parse(response.body.data);
    if (json == NULL) {
        printf("get_focused_video_information: cJSON_Parse returned NULL\n");
        goto cleanup;
    }

    create_file_from_memory("focused_body.json", response.body);

    const char* desc_path = ".videoDetails.shortDescription";

    if (assign_string_from_path(json, desc_path, targs->highlighted_video->description, sizeof(targs->highlighted_video->description)) == false) {
        printf("get_focused_video_information: assign video desc fail (json path: %s)\n", desc_path);
        targs->highlighted_video->description[0] = '\0';
    }

    const char* title_path = ".videoDetails.title";

    if (assign_string_from_path(json, title_path, targs->highlighted_video->title, sizeof(targs->highlighted_video->title)) == false) {
        printf("get_focused_video_information: assign title fail (json path: %s)\n", title_path);
        targs->highlighted_video->description[0] = '\0';
    }

    cleanup:
        search_finished = true;
        if (targs->req.body) free(targs->req.body);
        if (https_response_ready(&response)) free_https_response(&response);
        if (json) cJSON_Delete(json);
        if (targs) free(targs);
        return NULL;
}

bool queue_focused_video_task(const Query query, PersistentConnection* conn, HighlightedVideo* highlighted_video, const char* internal_api_key)
{
    if ((conn == NULL) || (highlighted_video == NULL) || (internal_api_key == NULL)) {
        printf("queue_focused_video_task: invalid input\n");
        return false;
    }

    PreparedRequest post = init_post_request(query, internal_api_key, conn->host);
    if (valid_post_request(post) == false) {
        printf("queue_focused_video_task: 'post' is invalid\n");
        return false;
    }

    FocusedInfoArgs* targs = init_focused_info_args(post, conn, highlighted_video);

    return launch_task(&task_queue, targs, get_focused_video_information);
}

// bug bountys
    // related videos arent always videos

    // watch history
        // every time the user presses a video
            // caveats :
                // only want to keep n videos in history? (100)
        // make button to load watch history
            // fill results with the content of those videos

bool file_exists(const char* filename)
{
    FILE* fp = fopen(filename, "r+");
    if (fp) {
        fclose(fp);
        return true;
    }

    else return false;
}

size_t get_file_length(FILE* fp)
{
    const size_t original_position = ftell(fp);

    fseek(fp, 0, SEEK_END);

    const size_t file_len = ftell(fp) - original_position;    

    fseek(fp, original_position, SEEK_SET);

    return file_len;
}

char* get_file_content(const char* filepath)
{
    if (filepath == NULL) return NULL;

    if (file_exists(filepath) == false) {
        printf("get_file_content: %s does not exist\n", filepath);
        return NULL;
    }

    FILE* fp = fopen(filepath, "r");

    const long len = get_file_length(fp);
    if (len == 0) {
        printf("get_file_content: get_file_length returned 0\n");
        fclose(fp); fp = NULL;
        return NULL;
    }

    char* buffer = malloc((sizeof(char) * (len + 1)));
    if (buffer == NULL) {
        printf("get_file_content: malloc returned NULL\n");
        fclose(fp); fp = NULL;
        return NULL;
    }

    const unsigned long chars_read = fread(buffer, sizeof(char), len, fp);
    
    buffer[chars_read] = '\0';
    
    fclose(fp); fp = NULL;

    return buffer;
}

#define WATCH_HISTORY_FILE "watch_history.json"
#define MAX_HISTORY_LEN 20

bool create_watch_history_file()
{
    if (file_exists(WATCH_HISTORY_FILE)) {
        return false;
    }

    FILE* fp = fopen(WATCH_HISTORY_FILE, "w");
    if (fp == NULL) {
        printf("create_watch_history_file: 'fp' is NULL\n");
        return false;
    }
    
    const char* buffer = "[]";
    const size_t size = strlen(buffer); 

    const bool write_sucess = fwrite(buffer, size, sizeof(char), fp) == sizeof(char);

    fclose(fp);

    return write_sucess;
}

int get_watched_video_index(const char* id, const cJSON* history)
{
    if ((id == NULL) || (history == NULL)) return -1;

    const char* id_path = ".id";

    int i = 0;
    cJSON* item;
    cJSON_ArrayForEach(item, history) {
        const cJSON* id_item = cjson_pointer_get(item, id_path);
        if (valid_cjson_string(id_item) && (strcmp(id, id_item->valuestring) == 0)) {
            return i;
        }

        i++;
    }

    return -1;
}

cJSON* init_video_json_object(const SearchResult* video)
{
    if ((video == NULL) || (video->media_type != VIDEO)) return NULL;

    cJSON* video_obj = cJSON_CreateObject();
    if (video_obj == NULL) {
        printf("init_video_json_object: cJSON_CreateObject returned NULL\n");
        return NULL;
    }

    cJSON_AddStringToObject(video_obj, "id", video->id);
    cJSON_AddNumberToObject(video_obj, "media_type", VIDEO);
    cJSON_AddStringToObject(video_obj, "title", video->title);
    cJSON_AddStringToObject(video_obj, "authorId", video->authorId);
    cJSON_AddStringToObject(video_obj, "duration", video->duration);
    cJSON_AddStringToObject(video_obj, "view_count", video->view_count);
    cJSON_AddNumberToObject(video_obj, "time_added", (double)time(NULL));
    cJSON_AddStringToObject(video_obj, "thumbnail_path", video->thumbnail_path);
    cJSON_AddStringToObject(video_obj, "date_published", video->date_published);

    return video_obj;
}

int get_oldest_watched_video_index(const cJSON* history)
{
    if ((history == NULL) || (cJSON_IsArray(history) == false)) NULL;

    const char* time_added_path = ".time_added";

    int oldest_watched_index = -1;
    double min_time_added = time(NULL);

    int i = 0;
    cJSON* item;
    cJSON_ArrayForEach(item, history) {
        const cJSON* time_added_obj = cjson_pointer_get(item, time_added_path); 
        if (valid_cjson_number(time_added_obj) && (time_added_obj->valuedouble < min_time_added)) {
            oldest_watched_index = i;
            min_time_added = time_added_obj->valuedouble;
        }

        i++;
    }

    return oldest_watched_index;
}

void remove_oldest_watched_video(cJSON* history_array)
{
    if (history_array == NULL) return;

    const int oldest_index = get_oldest_watched_video_index(history_array);
    if (oldest_index >= 0) {
        cJSON_Delete(cJSON_DetachItemFromArray(history_array, oldest_index));
    }
}

void update_watch_history(const SearchResult* watched_video)
{
    if (watched_video == NULL) return;

    char* history_buffer = get_file_content(WATCH_HISTORY_FILE);
    if (history_buffer == NULL) {
        printf("update_watch_history: 'history_buffer' is NULL\n");
        return;
    }

    cJSON* history_array = cJSON_Parse(history_buffer);
    if (history_array == NULL) {
        printf("update_watch_history: cJSON_Parse returned NULL\n");
        goto cleanup_buffer;
    } 

    // if you hit max size and you need to increase the size
        // need to find the oldest video and remove it

    const int watched_video_index = get_watched_video_index(watched_video->id, history_array);

    const bool found = (watched_video_index >= 0);
    cJSON* to_add = (found)
                    ? cJSON_DetachItemFromArray(history_array, watched_video_index)
                    : init_video_json_object(watched_video);

    if (to_add == NULL) {
        printf("update_watch_history: 'to_add' is NULL\n");
        goto cleanup_array;
    }

    if (found == false) {
        if (cJSON_GetArraySize(history_array) == MAX_HISTORY_LEN) {
            remove_oldest_watched_video(history_array);
        }
    }

    else cJSON_ReplaceItemInObject(to_add, "time_added", cJSON_CreateNumber((double)time(NULL)));
    
    cJSON_InsertItemInArray(history_array, 0, to_add);
    
    FILE* fp = fopen(WATCH_HISTORY_FILE, "w");
    if (fp == NULL) {
        printf("update_watch_history: 'fopen' returned NULL\n");
        goto cleanup_array;
    }

    char* new_history_buffer = cJSON_Print(history_array);
    if (new_history_buffer == NULL) {
        printf("update_watch_history: 'cJSON_Print' returned NULL\n");
        goto cleanup_fp;
    }

    const size_t size = strlen(new_history_buffer);

    if (fwrite(new_history_buffer, size, sizeof(char), fp) == 0) {
        printf("update_watch_history: 'fwrite' returned 0\n");
        goto cleanup_fwrite;
    }
    
    cleanup_fwrite:
        free(new_history_buffer);
    cleanup_fp:
        fclose(fp);
    cleanup_array:
        cJSON_Delete(history_array);
    cleanup_buffer:
        free(history_buffer);
}

// make button for loading watch history
// parse the content from the cjson array
// store into results array

int main()
{
    TextureCacheEntry *cached_thumbnails = NULL;
    thumbnail_queue = init_thumbnail_queue();

    Results results = init_results();

    task_queue = init_task_queue();
    pthread_t thread_pool[MAX_THREADS];
    init_thread_pool(MAX_THREADS, thread_pool, worker_thread_funct, &task_queue);
    
    ctx = SSL_CTX_new(TLS_client_method());
    if (!ctx) {
        printf("error initalizing SSL_CTX object\n");
        return 1;
    } 

    youtube_pool = init_connection_pool("www.youtube.com");
    video_thumbnail_pool = init_connection_pool(media_type_to_host(VIDEO)); // playlists, videos, shorts, and live videos all share the same host
    channel_thumbnail_pool = init_connection_pool(media_type_to_host(CHANNEL));

    PersistentConnection* conn = &youtube_pool.connections[youtube_pool.current_conn];

    char internal_api_key[64];
    parse_youtube_page(conn, sizeof(internal_api_key), internal_api_key);
    printf("INTERNAL KEY: \"%s\"\n", internal_api_key);
    
    create_watch_history_file();
    if (file_exists(WATCH_HISTORY_FILE) == false) {
        printf("failed to create \"%s\"\n", WATCH_HISTORY_FILE);
        return 1;
    }

    // when true, the application starts the search process
    bool search = false;
    bool edit_mode = false;
    SearchType last_search_type = -1;
    char last_search_query[512] = {0};

    Vector2 search_result_scrollbar_pos = { 10, 10 };

    // the current_query that the user has constructed
    Query query = {
        .allow_youtube_shorts = true,
        .string = "",
        .media = ANY,
        .sort = BY_RELEVANCE,
        .search_type = SEARCH_TYPE_QUERIED,
        .search_attr = SEARCH_ATTR_REPLACE,
        .continuation_token = NULL,
    };

    init_app();

    Ui ui;
    ui.font = GetFontDefault();
    ui.padding = 5;
    ui.spacing = 2;
    ui.word_wrap = true;

    bool clicked_video = false;
    HighlightedVideo highlighted_video = {0};
    Vector2 video_desc_scrollbar_pos = { 10, 10 };

    bool load_watch_history = false;

    while (!WindowShouldClose())
    {
        if (HASH_COUNT(cached_thumbnails) > 0) {
            remove_expired_thumbnails(&cached_thumbnails);
        }

        if (thumbnail_queue.count > 0) {
            process_thumbnail_queue(&thumbnail_queue, &cached_thumbnails);
        }

        if (clicked_video) {
            clicked_video = false;

            PersistentConnection* conn = &youtube_pool.connections[youtube_pool.current_conn];

            if (queue_focused_video_task(query, conn, &highlighted_video, internal_api_key) == false) {
                printf("failed to queue focused video task\n");
            }
        }

        if (search) {
            search = search_finished = false;

            free_thumbnail_queue(&thumbnail_queue); thumbnail_queue = init_thumbnail_queue();

            // evade bot detection
            if (strcmp(last_search_query, query.string) == 0) {
                cycle_connection(&youtube_pool);
            }

            last_search_type = query.search_type;
            strncpy(last_search_query, query.string, sizeof(last_search_query) - 1);

            PersistentConnection* conn = &youtube_pool.connections[youtube_pool.current_conn];

            if (queue_search_task(&query, conn, &results, internal_api_key) == false) {
                printf("failed to queue search task\n");
            }
        }

        if (load_watch_history) {
            load_watch_history = false;

            free(query.continuation_token); query.continuation_token = NULL;

            pthread_mutex_lock(&results.mutex);
            
            const size_t old_size = results.count;
            
            char* buffer = get_file_content(WATCH_HISTORY_FILE);
            if (buffer) {
                cJSON* history_array = cJSON_Parse(buffer);
                if (valid_cjson_array(history_array)) {
                    const size_t array_size = cJSON_GetArraySize(history_array);
                    cJSON* item;
                    cJSON_ArrayForEach(item, history_array) {
                        SearchResult* watched_video = init_search_result();
                        if (watched_video) {
                            if (assign_string_from_path(item, ".id", watched_video->id, sizeof(watched_video->id)) == false) {
                                printf("assign id fail\n");
                            }

                            watched_video->media_type = VIDEO;

                            if (assign_string_from_path(item, ".title", watched_video->title, sizeof(watched_video->title)) == false) {
                                printf("assign title fail\n");
                            } 

                            if (assign_string_from_path(item, ".authorId", watched_video->authorId, sizeof(watched_video->authorId)) == false) {
                                printf("assign authorId fail\n");
                            } 

                            if (assign_string_from_path(item, ".duration", watched_video->duration, sizeof(watched_video->duration)) == false) {
                                printf("assign duration fail\n");
                            } 

                            if (assign_string_from_path(item, ".view_count", watched_video->view_count, sizeof(watched_video->view_count)) == false) {
                                printf("assign view_count fail\n");
                            } 

                            if (assign_string_from_path(item, ".thumbnail_path", watched_video->thumbnail_path, sizeof(watched_video->thumbnail_path)) == false) {
                                printf("assign thumbnail_path fail\n");
                            } 

                            if (assign_string_from_path(item, ".date_published", watched_video->date_published, sizeof(watched_video->date_published)) == false) {
                                printf("assign date_published fail\n");
                            } 

                            add_search_result(&results, watched_video);                        
                        }
                    }

                    delete_n_results(&results, old_size);
                    cJSON_Delete(history_array);
                    free(buffer);
                }
            }

            pthread_mutex_unlock(&results.mutex);
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

            // edit_mode toggles when search box is focused (T) or not (F)
            int text_box_status;
            if ((text_box_status = GuiTextBox(search_bar_bounds, query.string, sizeof(query.string), edit_mode))) {
                edit_mode = !edit_mode;
            }

            bool enter_key_pressed = text_box_status == 2;

            if (GuiButton(search_button_bounds, "Search") || enter_key_pressed) {
                if (trim_whitespace(query.string) > 0) {
                    search = true;
                    query.search_attr = SEARCH_ATTR_REPLACE;
                    query.search_type = SEARCH_TYPE_QUERIED;
                    SetWindowTitle(TextFormat("[%s(loading)] - metube", query.string));
                }
            }

            const Rectangle trending_button_bounds = {
                .x = search_button_bounds.x + search_button_bounds.width + ui.padding,
                .y = ui.padding,
                .width = 50,
                .height = 25,
            };

            if (GuiButton(trending_button_bounds, "Trending")) {
                search = true;
                query.search_attr = SEARCH_ATTR_REPLACE;
                query.search_type = SEARCH_TYPE_TRENDING;
                SetWindowTitle("[Trending(loading)] - metube");
            }

            const Rectangle related_videos_button_bounds = {
                .x = trending_button_bounds.x + trending_button_bounds.width + ui.padding,
                .y = ui.padding,
                .width = 85,
                .height = 25
            };

            if (highlighted_video.id[0] == '\0') {
                GuiSetState(STATE_DISABLED);
            }
            
            if (GuiButton(related_videos_button_bounds, "Related Videos")) {
                search = true;
                query.search_attr = SEARCH_ATTR_REPLACE;
                query.search_type = SEARCH_TYPE_RELATED;
                strncpy(query.focused_id, highlighted_video.id, sizeof(query.focused_id) - 1);
                query.focused_id[sizeof(query.focused_id) - 1] = '\0';
                SetWindowTitle(TextFormat("[Related:%s(loading)] - metube", query.focused_id));                
            }

            GuiSetState(STATE_NORMAL);

            if (highlighted_video.author_id[0] == '\0') {
                GuiSetState(STATE_DISABLED);
            }

            const Rectangle users_videos_button_bounds = {
                .x = related_videos_button_bounds.x + related_videos_button_bounds.width + ui.padding,
                .y = ui.padding,
                .width = 85,
                .height = 25,
            };

            if (GuiButton(users_videos_button_bounds, "User Videos")) {
                search = true;
                query.search_attr = SEARCH_ATTR_REPLACE;
                query.search_type = SEARCH_TYPE_VIEW_CHANNEL;
                strncpy(query.focused_id, highlighted_video.author_id, sizeof(query.focused_id) - 1);
                query.focused_id[sizeof(query.focused_id) - 1] = '\0';
                SetWindowTitle(TextFormat("[User %s Videos(loading)] - metube", query.focused_id));
            }

            GuiSetState(STATE_NORMAL);

            const Rectangle watch_history_button = {
                .x = users_videos_button_bounds.x + users_videos_button_bounds.width + ui.padding,
                .y = ui.padding,
                .width = 80,
                .height = 25,
            };

            if (GuiButton(watch_history_button, "Watch History")) {
                load_watch_history = true;
            }

            const Rectangle filter_window_bounds = {
                .x = ui.padding, 
                .y = search_button_bounds.y + search_button_bounds.height + ui.padding, 
                .width = search_bar_bounds.width, 
                .height = 75
            };

            draw_filter_window(&query, filter_window_bounds, ui.font, ui.padding);

            const Rectangle scroll_window_bounds = { 
                .x = ui.padding, 
                .y = search_bar_bounds.y + search_bar_bounds.height + filter_window_bounds.height + (ui.padding * 2), 
                .width = search_bar_bounds.width, 
                .height = GetScreenHeight() - scroll_window_bounds.y - (ui.padding * 2), 
            };

            pthread_mutex_lock(&results.mutex); 

            const bool load_more_button_visible = (query.continuation_token != NULL) && (query.continuation_token[0] != '\0');
            const size_t results_len = results.count + load_more_button_visible;

            pthread_mutex_unlock(&results.mutex); 

            const int container_height = 80;

            const Rectangle content_area = {
                .x = scroll_window_bounds.x,
                .y = scroll_window_bounds.y,
                .width = scroll_window_bounds.width,
                .height = container_height * results_len,
            };

            const int SCROLLBAR_WIDTH = 13;
            const bool vertical_scrollbar_visible = content_area.height > scroll_window_bounds.height;

            GuiScrollPanel(scroll_window_bounds, NULL, content_area, &search_result_scrollbar_pos, NULL, true);

            const Rectangle scissor_rect = padded_rectangle(1, scroll_window_bounds);

            BeginScissorMode(scissor_rect.x, scissor_rect.y, scissor_rect.width, scissor_rect.height);

            int i = 0;
            float container_y = scroll_window_bounds.y;
            Rectangle container = { 
                .x = ui.padding, 
                .y = container_y, 
                .width = scroll_window_bounds.width - (vertical_scrollbar_visible ? SCROLLBAR_WIDTH : 0),
                .height = container_height 
            };

            pthread_mutex_lock(&results.mutex);

            for (SearchResult* search_result = results.head; search_result; search_result = search_result->next, i++, container_y += container_height) {
                container.y = container_y + search_result_scrollbar_pos.y;

                if (CheckCollisionRecs(scissor_rect, container) == false) {
                    continue;
                }

                const bool result_is_highlighted = strcmp(search_result->id, highlighted_video.id) == 0;

                const Color container_color = result_is_highlighted ? 
                                              BLUE :
                                              ((i % 2) ? WHITE : RAYWHITE);

                DrawRectangleRec(container, container_color);

                Texture2D thumbnail = (Texture2D){0};
                TextureCacheEntry *cached = find_cached_thumbnail(search_result->id, &cached_thumbnails);
                if (cached_texture_is_ready(cached)) {
                    thumbnail = cached->thumbnail;
                    start_timer(&cached->timer, THUMBNAIL_LIFETIME); // refresh lifetime
                }

                else if ((search_result->thumbnail_loaded == false) && (search_result->thumbnail_path[0] != '\0')) {
                    search_result->thumbnail_loaded = true;
                    
                    ConnectionPool* pool = media_type_to_pool(search_result->media_type);
                    if (pool) {
                        PersistentConnection* conn = &pool->connections[pool->current_conn];
                        if (queue_thumbnail_load(search_result->id, search_result->thumbnail_path, conn)) {
                            cycle_connection(pool);
                        }
                    }
                }

                draw_search_result(search_result, thumbnail, container, container_color, ui);

                if ((CheckCollisionPointRec(GetMousePosition(), container)) && 
                    (CheckCollisionPointRec(GetMousePosition(), scissor_rect)) &&
                    (IsMouseButtonPressed(MOUSE_BUTTON_LEFT))) {
                    query.search_attr = SEARCH_ATTR_REPLACE;

                    strncpy(query.focused_id, search_result->id, sizeof(query.focused_id) - 1);
                    query.focused_id[sizeof(query.focused_id) - 1] = '\0';

                    switch (search_result->media_type) {
                        case LIVE:
                        case SHORT:
                        case VIDEO:
                            if (result_is_highlighted == false) {
                                clicked_video = true;
                                query.search_type = SEARCH_TYPE_VIDEO_FOCUS;
                                
                                strncpy(highlighted_video.id, search_result->id, sizeof(highlighted_video.id) - 1);
                                highlighted_video.id[sizeof(highlighted_video.id) - 1] = '\0';

                                strncpy(highlighted_video.author_id, search_result->authorId, sizeof(highlighted_video.author_id));
                                highlighted_video.author_id[sizeof(highlighted_video.author_id) - 1] = '\0';

                                update_watch_history(search_result);
                            }
                            break;
                        case PLAYLIST:
                            search = true;
                            query.search_type = SEARCH_TYPE_VIEW_PLAYLIST;
                            SetWindowTitle(TextFormat("[Playlist:%s(loading)] - metube", query.focused_id));
                            break;
                        case CHANNEL:
                            search = true;
                            query.search_type = SEARCH_TYPE_VIEW_CHANNEL;
                            SetWindowTitle(TextFormat("[Channel:%s(loading)] - metube", query.focused_id));
                            break;
                        case ANY:
                        case UNDF:
                            break;
                    }
                }
            }

            const Rectangle load_more_button_bounds = {
                .x = container.x,
                .y = container.y + container_height,
                .width = container.width,
                .height = container_height,
            };

            if (load_more_button_visible && GuiButton(load_more_button_bounds, "LOAD MORE")) {
                search = true;
                query.search_type = last_search_type;
                query.search_attr = SEARCH_ATTR_APPENDING;
            }

            pthread_mutex_unlock(&results.mutex);
            
            EndScissorMode();

            const Rectangle focused_video_bounds = {
                .x = scroll_window_bounds.x + scroll_window_bounds.width + ui.padding,
                .y = filter_window_bounds.y,
                .width = GetScreenWidth() - focused_video_bounds.x,
                .height = GetScreenHeight() - focused_video_bounds.y - ui.padding,
            };
            
            draw_highlighted_video(focused_video_bounds, ui, &video_desc_scrollbar_pos, &highlighted_video);

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
    if (query.continuation_token) free(query.continuation_token);
    
    // ssl stuff
    if (ctx) SSL_CTX_free(ctx);
    free_connection_pool(&youtube_pool);
    free_connection_pool(&video_thumbnail_pool);
    free_connection_pool(&channel_thumbnail_pool);
    
    CloseWindow();
    
    return 0;
}

// searching feature
    // watch history
    // subscribe to different channels
    // like/fav video list

// after everythings done:
    // able to add videos to created playlist
    // fonts for L.O.T.E.
    // handle connecticity issues (no wifi on startup, changing connections, etc.)
    // reccomendations using cookies
    // goto's for redundant cleanups
    // set all ptrs to NULL after freeing them
    // thumbnail frames from video click
    // limit the amout of results to 20 (specifically loading more playlist videos)
    // channel header and desc on channel view
    // mem leak on queue_focused_video_task on premature exit
    // ssl read read -1 error