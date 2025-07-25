#include <math.h>
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
    char title[256];            // name of the content           
    char author[128];           // creator of video, livestream or playlist         
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
    SEARCH_TYPE_QUERIED,  
    SEARCH_TYPE_RELATED,  
    SEARCH_TYPE_TRENDING, 
    SEARCH_TYPE_LOAD_MORE,
    SEARCH_TYPE_VIDEO_FOCUS,
} SearchType;

typedef enum
{
    SEARCH_ATTR_NEW,
    SEARCH_ATTR_APPENDING,
} SearchAttribute;

const char* search_type_to_endpoint(const SearchType search_type)
{
    switch (search_type) {
        case SEARCH_TYPE_QUERIED:
        case SEARCH_TYPE_LOAD_MORE: return "search";
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
            printf("media_type_to_pool: invalid type passed\n");
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

bool configure_post_header(const size_t n, char post_header[n], const char *host, const char *path, const size_t post_body_length)
{
    const size_t len = snprintf(post_header, n,
                        "POST %s HTTP/1.1\r\n"
                        "Host: %s\r\n"
                        "User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:125.0) Gecko/20100101 Firefox/125.0\r\n"
                        "Content-Type: application/json\r\n"
                        "Accept: application/json\r\n"
                        "Content-Length: %zu\r\n"
                        "Connection: keep-alive\r\n"
                        "\r\n",
                        path, host, post_body_length);
    return len < n;
}

bool configure_post_body(const size_t n, char post_body[n], const Query query, const char* continuation_token, const char* video_id)
{
    size_t body_len = snprintf(post_body, n,
                        "{\n"
                        "  \"context\": {\n"
                        "    \"client\": {\n"
                        "      \"clientName\": \"WEB\",\n"
                        "      \"clientVersion\": \"2.20210721.00.00\"\n"
                        "    }\n"
                        "  },\n");

    switch (query.search_type) {
        case SEARCH_TYPE_QUERIED:
            body_len += snprintf(post_body + body_len, n - body_len,
                        "  \"params\": \"%s%s\",\n"
                                "  \"query\": \"%s\",\n"
                                "}", sort_type_to_url(query.sort), media_type_to_url(query.media), query.string);
            break;
        case SEARCH_TYPE_LOAD_MORE:
            body_len += snprintf(post_body + body_len, n - body_len,
                        "  \"continuation\": \"%s\"\n"
                            "}", continuation_token);
            break;
        case SEARCH_TYPE_TRENDING:
            body_len += snprintf(post_body + body_len, n - body_len,
                        "  \"browseId\": \"FEtrending\"\n"
                            "}");
            break;
        case SEARCH_TYPE_RELATED:
        case SEARCH_TYPE_VIDEO_FOCUS:
            body_len += snprintf(post_body + body_len, n - body_len,
                        "  \"videoId\": \"%s\"\n"
                            "}", video_id);
            break;
    }

    return (body_len < n);
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
    bool allow_youtube_shorts;
    SearchType search_type;
    SearchAttribute search_attr;
    PreparedRequest request;
    Results *search_results;
    RawThumbnailQueue *thumbnail_queue;
    PersistentConnection *youtube_connection;
} SearchThreadArgs;

SearchThreadArgs* init_search_thread_args(const Query query, PreparedRequest request, Results* search_results, RawThumbnailQueue* thumbnail_queue, PersistentConnection* youtube_connection)
{
    SearchThreadArgs* targs = (SearchThreadArgs*) malloc(sizeof(SearchThreadArgs));
    if (targs == NULL) {
        printf("create_search_thread_args: malloc returned NULL for 'search_thread_args'\n");
        return NULL;
    }

    targs->allow_youtube_shorts = query.allow_youtube_shorts;
    targs->search_type = query.search_type;
    targs->search_attr = query.search_attr;
    targs->request = request;
    targs->search_results = search_results;
    targs->thumbnail_queue = thumbnail_queue;
    targs->youtube_connection = youtube_connection;

    return targs;
}

bool valid_cjson_string(const cJSON* json_str)
{
    return (json_str) && (cJSON_IsString(json_str)) && (json_str->valuestring[0] != '\0');
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
    if (root == NULL || path == NULL) return NULL;

    int n = 0;
    const char** elements = TextSplit(path, '/', &n); 

    cJSON* ret = root;

    for(int i = 0; (ret && i < n); i++) {
        if (elements[i][0] == '\0') {
            continue;
        }

        if (string_is_integer(elements[i])) {
            if (cJSON_IsArray(ret)) {
                const int index = atoi(elements[i]);
                if ((index >= 0) && (index < cJSON_GetArraySize(ret))) {
                    ret = cJSON_GetArrayItem(ret, index);
                }

                else return NULL;
            }

            else return NULL;
        }

        else ret = cJSON_GetObjectItem(ret, elements[i]);
    }

    return ret;
}

bool get_continuation_token_from_query_type(const SearchType search_type, cJSON* root, const size_t n, char token[n])
{
    if (root == NULL) return false;

    cJSON* parent = NULL;
    const char* token_path = "/continuationItemRenderer/continuationEndpoint/continuationCommand/token";
    switch (search_type) {
        case SEARCH_TYPE_QUERIED:
            parent = cjson_pointer_get(root, "/contents/twoColumnSearchResultsRenderer/primaryContents/sectionListRenderer/contents/1");
            break;
        case SEARCH_TYPE_LOAD_MORE:
            parent = cjson_pointer_get(root, "/onResponseReceivedCommands/0/appendContinuationItemsAction/continuationItems/1");
            break;
        case SEARCH_TYPE_RELATED:
            parent = cjson_pointer_get(root, "/contents/twoColumnWatchNextResults/secondaryResults/secondaryResults/results");
            parent = cJSON_GetArrayItem(parent, cJSON_GetArraySize(parent) - 1);
            break;
        case SEARCH_TYPE_TRENDING: break;
        case SEARCH_TYPE_VIDEO_FOCUS: break;
            break;
    }

    const cJSON* token_tag = cjson_pointer_get(parent, token_path);
    if (valid_cjson_string(token_tag) == false) {
        printf("get_continuation_token_from_query_type: failed\n");
        token[0] = '\0';
        return false;
    }

    return (snprintf(token, n, "%s", token_tag->valuestring) < n);
} 

static int elements_added = 0; 
static bool delete_old_nodes = false;
static bool search_finished = true;

bool video_is_youtube_short(cJSON *videoRenderer) 
{
    const char* path = "/navigationEndpoint/commandMetadata/webCommandMetadata/url";
    const cJSON* url = cjson_pointer_get(videoRenderer, path); 
    if (valid_cjson_string(url)) {
        return strstr(url->valuestring, "/shorts");
    }

    return false;
}

bool video_is_live(cJSON* videoRenderer)
{
    const char* path = "/badges/0/metadataBadgeRenderer/label";
    const cJSON* label = cjson_pointer_get(videoRenderer, path);
    if (valid_cjson_string(label)) {
        return (strcmp("LIVE", label->valuestring) == 0);
    }

    return false;
}

void parse_video(cJSON* videoRenderer, const bool allow_youtube_shorts, SearchResult* video)
{
    if ((videoRenderer == NULL) || (video == NULL)) return;

    video->media_type = VIDEO;

    if (video_is_youtube_short(videoRenderer)) {
        if (allow_youtube_shorts == false) {
            video->media_type = UNDF;
            return;
        }

        video->media_type = SHORT;
    }

    const cJSON* videoId = cjson_pointer_get(videoRenderer, "/videoId");
    if (valid_cjson_string(videoId)) {
        snprintf(video->id, sizeof(video->id), "%s", videoId->valuestring);
        snprintf(video->thumbnail_path, sizeof(video->thumbnail_path), "/vi/%s/mqdefault.jpg", videoId->valuestring);
    } 

    else {
        printf("parse_video: no video id\n");
        video->media_type = UNDF;
        return;
    }

    const cJSON* titleText = cjson_pointer_get(videoRenderer, "/title/runs/0/text");
    if (valid_cjson_string(titleText)) {
        snprintf(video->title, sizeof(video->title), "%s", titleText->valuestring);
    }

    const cJSON* authorText = cjson_pointer_get(videoRenderer, "/ownerText/runs/0/text");
    if (valid_cjson_string(authorText)) {
        snprintf(video->author, sizeof(video->author), "%s", authorText->valuestring);
    }
    
    if (video_is_live(videoRenderer)) {
        video->media_type = LIVE;
        
        const cJSON* liveViewCountText = cjson_pointer_get(videoRenderer, "/viewCountText/runs/0/text");
        if (valid_cjson_string(liveViewCountText)) {
            snprintf(video->view_count, sizeof(video->view_count), "%s", liveViewCountText->valuestring);
        }

        else video->view_count[0] = '0';

        return;
    }

    const cJSON* viewCountText = cjson_pointer_get(videoRenderer, "/viewCountText/simpleText");
    if (valid_cjson_string(viewCountText)) {
        snprintf(video->view_count, sizeof(video->view_count), "%s", viewCountText->valuestring);
        format_view_count(video->view_count);
    }

    else snprintf(video->view_count, sizeof(video->view_count), "no views");

    const cJSON* videoAge = cjson_pointer_get(videoRenderer, "/publishedTimeText/simpleText");
    if (valid_cjson_string(videoAge)) {
        snprintf(video->date_published, sizeof(video->date_published), "%s", videoAge->valuestring);
    }
    
    const cJSON* videoLengthText = cjson_pointer_get(videoRenderer, "/lengthText/simpleText");
    if (valid_cjson_string(videoLengthText)) {
        snprintf(video->duration, sizeof(video->duration), "%s", videoLengthText->valuestring);
    }
}

void parse_related_video(cJSON* lockupViewModel, SearchResult* related_vid)
{
    if ((lockupViewModel == NULL) || (related_vid == NULL)) return;

    const cJSON* contentId = cjson_pointer_get(lockupViewModel, "/contentId");
    if (valid_cjson_string(contentId)) {
        related_vid->media_type = VIDEO;
        strncpy(related_vid->id, contentId->valuestring, sizeof(related_vid->id) - 1);
        snprintf(related_vid->thumbnail_path, sizeof(related_vid->thumbnail_path), "/vi/%s/mqdefault.jpg", contentId->valuestring);
    }

    const cJSON* titleContent = cjson_pointer_get(lockupViewModel, "/metadata/lockupMetadataViewModel/title/content");
    if (valid_cjson_string(titleContent)) {
        strncpy(related_vid->title, titleContent->valuestring, sizeof(related_vid->title) - 1);
    }

    const cJSON* durationText = cjson_pointer_get(lockupViewModel, "/contentImage/thumbnailViewModel/overlays/0/thumbnailOverlayBadgeViewModel/thumbnailBadges/0/thumbnailBadgeViewModel/text");
    if (valid_cjson_string(durationText)) {
        strncpy(related_vid->duration, durationText->valuestring, sizeof(related_vid->duration) - 1);
    }

    cJSON* metadataParts = cjson_pointer_get(lockupViewModel, "/metadata/lockupMetadataViewModel/metadata/contentMetadataViewModel/metadataRows/1/metadataParts");
    if (valid_cjson_array(metadataParts)) {
        const cJSON* viewCountContent = cjson_pointer_get(metadataParts, "/0/text/content");
        if (valid_cjson_string(viewCountContent)) {
            strncpy(related_vid->view_count, viewCountContent->valuestring, sizeof(related_vid->view_count) - 1);
            char* end = strstr(related_vid->view_count, " views");
            if (end) *end = '\0';
        }

        const cJSON* videoAgeContent = cjson_pointer_get(metadataParts, "/1/text/content");
        if (valid_cjson_string(videoAgeContent)) {
            strncpy(related_vid->date_published, videoAgeContent->valuestring, sizeof(related_vid->date_published));
        }
    }
}

void parse_channel(cJSON* channelRenderer, SearchResult* channel)
{
    if ((channelRenderer == NULL) || (channel == NULL)) return;

    channel->media_type = CHANNEL;

    const cJSON* channelId = cjson_pointer_get(channelRenderer, "/channelId");
    if (valid_cjson_string(channelId)) {
        snprintf(channel->id, sizeof(channel->id), "%s", channelId->valuestring);
    }
    
    else {
        printf("parse_channel: no channel id\n");
        channel->media_type = UNDF;
        return;
    }

    const cJSON* channelName = cjson_pointer_get(channelRenderer, "/title/simpleText");
    if (valid_cjson_string(channelName)) {
        snprintf(channel->title, sizeof(channel->title), "%s", channelName->valuestring);
    }

    const cJSON* subCount = cjson_pointer_get(channelRenderer, "/videoCountText/simpleText");
    if (valid_cjson_string(subCount)) {
        snprintf(channel->subscriber_count, sizeof(channel->subscriber_count), "%s", subCount->valuestring);
    }

    const cJSON* channelThumbnailLink = cjson_pointer_get(channelRenderer, "/thumbnail/thumbnails/0/url");
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

    playlist->media_type = PLAYLIST;

    const cJSON* contentId = cjson_pointer_get(lockupViewModel, "/contentId");
    if (valid_cjson_string(contentId)) {
        snprintf(playlist->id, sizeof(playlist->id), "%s", contentId->valuestring);
    }
    
    else {
        printf("parse_playlist: no playlist id\n");
        playlist->media_type = UNDF;
        return;
    }

    const cJSON* playlistTitle = cjson_pointer_get(lockupViewModel, "/metadata/lockupMetadataViewModel/title/content");
    if (valid_cjson_string(playlistTitle)) {
        snprintf(playlist->title, sizeof(playlist->title), "%s", playlistTitle->valuestring);
    }

    const cJSON* firstVideoId = cjson_pointer_get(lockupViewModel, "/rendererContext/commandContext/onTap/innertubeCommand/watchEndpoint/videoId");
    if (valid_cjson_string(firstVideoId)) {
        snprintf(playlist->thumbnail_path, sizeof(playlist->thumbnail_path), "/vi/%s/mqdefault.jpg", firstVideoId->valuestring);
    }

    const cJSON* videoCount = cjson_pointer_get(lockupViewModel, "/contentImage/collectionThumbnailViewModel/primaryThumbnail/thumbnailViewModel/overlays/0/thumbnailOverlayBadgeViewModel/thumbnailBadges/0/thumbnailBadgeViewModel/text");
    if (valid_cjson_string(videoCount)) {
        snprintf(playlist->video_count, sizeof(playlist->video_count), "%s", videoCount->valuestring);
    }
}

int create_results_from_json(cJSON* cjson, Results *results, const SearchType search_type, const bool allow_youtube_shorts)
{
    if (results == NULL) {
        printf("create_results_from_json: 'results' is NULL\n");
        return 0;
    }

    int nelements = 0;

    cJSON *item;
    cJSON_ArrayForEach (item, cjson) {
        SearchResult *search_result = init_search_result();
        if (search_result == NULL) {
            printf("create_results_from_json: init_search_result returned NULL\n");
            return 0;
        }
        
        cJSON *videoRenderer   = cjson_pointer_get(item, "/videoRenderer");     // video
        cJSON *channelRenderer = cjson_pointer_get(item, "/channelRenderer");   // channel
        cJSON *lockupViewModel = cjson_pointer_get(item, "/lockupViewModel");   // playlist or related video container
            
        if (videoRenderer) parse_video(videoRenderer, allow_youtube_shorts, search_result);

        else if (channelRenderer) parse_channel(channelRenderer, search_result);

        else if (lockupViewModel) {
            if (search_type == SEARCH_TYPE_RELATED) parse_related_video(lockupViewModel, search_result);
            else parse_playlist(lockupViewModel, search_result);
        }

        if (search_result->media_type != UNDF) {
            nelements++;
            add_search_result(results, search_result);
        }

        else free_search_result(search_result);
    }

    return nelements;
}

const char* get_json_path_for_query_type(const SearchType search_type)
{
    switch (search_type) {
        case SEARCH_TYPE_QUERIED: return "/contents/twoColumnSearchResultsRenderer/primaryContents/sectionListRenderer/contents/0/itemSectionRenderer/contents";
        case SEARCH_TYPE_LOAD_MORE: return "/onResponseReceivedCommands/0/appendContinuationItemsAction/continuationItems/0/itemSectionRenderer/contents";
        case SEARCH_TYPE_TRENDING: return "/contents/twoColumnBrowseResultsRenderer/tabs/0/tabRenderer/content/sectionListRenderer/contents/2/itemSectionRenderer/contents/0/shelfRenderer/content/expandedShelfContentsRenderer/items";
        case SEARCH_TYPE_RELATED: return "/contents/twoColumnWatchNextResults/secondaryResults/secondaryResults/results";
        case SEARCH_TYPE_VIDEO_FOCUS:
            break;
        }

    return NULL;
}

const char* search_type_to_text(const SearchType search_type)
{
    switch (search_type) {
        case SEARCH_TYPE_QUERIED: return "QUERIED";
        case SEARCH_TYPE_RELATED: return "RELATED";
        case SEARCH_TYPE_TRENDING: return "TRENDING";
        case SEARCH_TYPE_LOAD_MORE: return "LOAD MORE";
        case SEARCH_TYPE_VIDEO_FOCUS: return "VIDEO FOCUS";
        default:
            return NULL;
    }
}

const char* search_attr_to_text(const SearchAttribute search_attr)
{
    switch (search_attr) {
        case SEARCH_ATTR_NEW: return "NEW";
        case SEARCH_ATTR_APPENDING: return "APPENDING";
        default:
            return NULL;
    }
}

void* get_results_from_query(void* args)
{
    float start_time = GetTime(), end_time; // preformance check
    
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

    json = cJSON_Parse(response.body.data);
    if (json == NULL) {
        printf("get_results_from_query: cJSON_Parse returned NULL\n");
        SetWindowTitle("[failed] - metube");
        goto cleanup;
    }

    const char* json_path = get_json_path_for_query_type(targs->search_type);
    cJSON* result_root = cjson_pointer_get(json, json_path); 
    elements_added = create_results_from_json(result_root, targs->search_results, targs->search_type, targs->allow_youtube_shorts);
    
    get_continuation_token_from_query_type(targs->search_type, json, sizeof(targs->search_results->continuation_token), targs->search_results->continuation_token);

    end_time = GetTime();
    printf("%s (%s) took %f seconds, %d items found\n", search_type_to_text(targs->search_type), search_attr_to_text(targs->search_attr), end_time - start_time, elements_added);
    
    const int display_count = (targs->search_attr == SEARCH_ATTR_APPENDING) ? targs->search_results->count : elements_added;
    SetWindowTitle(TextFormat("[search results(%d)] - metube", display_count));

    cleanup:
        search_finished = true;
        delete_old_nodes = (targs->search_attr == SEARCH_ATTR_NEW);
        
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

static char internal_api_key[64];
void get_internal_api_key(const char* response_body)
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

    for (char* current = location + tag_len; (current && (i < sizeof(internal_api_key) - 1)); current++) {
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

void parse_youtube_page(PersistentConnection *youtube_connection)
{
    PreparedRequest request = {
        .body = "",
        .path = "/",
        .header = "",
    };

    configure_get_header(sizeof(request.header), request.header, youtube_connection->host, request.path);

    HTTPS_Response youtube_page_response = send_https_request(request, youtube_connection);
    if (https_response_ready(&youtube_page_response) == false) {
        memset(internal_api_key, 0, sizeof(internal_api_key));
        printf("parse_youtube_page: page response is invalid\n");
        return;
    }

    get_internal_api_key(youtube_page_response.body.data);

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
            DrawTextBoxed(TextFormat("%s %s views", search_result->date_published, search_result->view_count), padded_rectangle(ui.padding, subtext_area), ui, 11.5, BLACK);
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

void draw_video_desc(const Rectangle container, Ui ui, Vector2* scrollbar_position, char* video_desc)
{
    const Color text_color = BLACK;
    const int font_size = 12;
    const int spacing = 2;

    const Rectangle scroll_window_area = {
        .x = container.x,
        .y = container.y + (container.height * 0.25f),
        .width = container.width - ui.padding,
        .height = container.height * 0.75f,
    };

    const float padded_width = container.width - ui.padding - ui.padding;
    
    // HACK: adding random shit makes the content grow faster... who would've thought?
    const float line_height = font_size + spacing + (ui.padding * 1.5);
    const int nlines = anticipate_lines_wordwrap(ui.font, video_desc, font_size, spacing, padded_width);
    
    const float text_height = line_height * nlines;
    
    const Rectangle scroll_content_area = {
        .x = scroll_window_area.x,
        .y = scroll_window_area.y,
        .width = scroll_window_area.width,
        .height = fmaxf(text_height, scroll_window_area.height - 1),
    };

    GuiScrollPanel(scroll_window_area, NULL, scroll_content_area, scrollbar_position, NULL, false);

    const Rectangle video_desc_bounds = {
        .x = scroll_content_area.x,
        .y = scroll_content_area.y + scrollbar_position->y,
        .width = scroll_content_area.width,
        .height = scroll_content_area.height,
    };

    BeginScissorMode(scroll_window_area.x, scroll_window_area.y, scroll_window_area.width, scroll_window_area.height);

    const Rectangle padded = padded_rectangle(ui.padding, video_desc_bounds);
    
    DrawTextBoxed(video_desc, padded, ui, font_size, text_color);

    EndScissorMode();
}

typedef struct
{
    PreparedRequest req;
    PersistentConnection* conn;
    char* desc_ptr;
    size_t desc_size;
} FocusedInfoArgs;

FocusedInfoArgs* init_focused_info_args(PreparedRequest req, PersistentConnection* conn, char* video_desc_ptr, size_t strlen)
{
    FocusedInfoArgs* targs = malloc(sizeof(FocusedInfoArgs));
    if (targs == NULL) {
        printf("init_focused_info_args: malloc returned NULL for 'targs'\n");
        return NULL;
    }

    targs->req = req;
    targs->conn = conn;
    targs->desc_size = strlen;
    targs->desc_ptr = video_desc_ptr;

    return targs;
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

    create_file_from_memory("focused_header.txt", response.header);
    create_file_from_memory("focused_body.json", response.body);

    json = cJSON_Parse(response.body.data);
    if (json == NULL) {
        printf("get_focused_video_information: cJSON_Parse returned NULL\n");
        goto cleanup;
    }

    const cJSON* shortDescription = cjson_pointer_get(json, "/videoDetails/shortDescription");
    if (valid_cjson_string(shortDescription) == false) {
        printf("get_focused_video_information: video description not found\n");
        targs->desc_ptr[0] = '\0';
        goto cleanup;
    }

    snprintf(targs->desc_ptr, targs->desc_size, "%s", shortDescription->valuestring);

    float video_len = -1;
    const cJSON* lengthSeconds = cjson_pointer_get(json, "/videoDetails/lengthSeconds");
    if (valid_cjson_string(lengthSeconds) == false) {
        printf("get_focused_video_information: video length not found\n");
        goto cleanup;
    }

    video_len = atof(lengthSeconds->valuestring);

    char spec_link[512];
    const cJSON* spec = cjson_pointer_get(json, "/storyboards/playerStoryboardSpecRenderer/spec");
    if (valid_cjson_string(spec) == false) {
        printf("get_focused_video_information: spec not found\n");
        goto cleanup;

    }

    strncpy(spec_link, spec->valuestring, sizeof(spec_link) - 1);

    int recommended_level = -1;
    const cJSON* recommendedLevel = cjson_pointer_get(json, "/storyboards/playerStoryboardSpecRenderer/recommendedLevel");
    if (valid_cjson_number(recommendedLevel) == false) {
        printf("get_focused_video_information: recommended level not found\n");
        goto cleanup;
    }

    recommended_level = recommendedLevel->valueint;

    char level_parameters[128];
    if (get_level_string(recommended_level, spec_link, sizeof(level_parameters), level_parameters) == 0) {
        printf("get_focused_video_information: get_level_string failed\n");
        goto cleanup;
    }

    const int nrelavent_params = 6;
    const char PARAM_SEPERATOR = '#';
    
    int n = 0;
    const char** storyboard_params = TextSplit(level_parameters, PARAM_SEPERATOR, &n);

    if (n < nrelavent_params) {
        printf("get_focused_video_information: missing storyboard parameters\n");
        goto cleanup;
    }

    cleanup:
        search_finished = true;

        if (https_response_ready(&response)) free_https_response(&response);
        if (json) cJSON_Delete(json);
        if (targs) free(targs);
        return NULL;
}

bool queue_thumbnail_load(const char* search_result_id, const char* thumbnail_path, PersistentConnection* conn)
{
    if (search_result_id == NULL || thumbnail_path == NULL) return false;

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

// get thumbnail frames from video click

// handle channel click -> should go to homepage
// handle playlist click -> should load videos from that playlist

int main()
{
    SSL_library_init();
    OpenSSL_add_all_algorithms();
    
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

    parse_youtube_page(&youtube_pool.connections[youtube_pool.current_conn]);
    printf("INTERNAL KEY: \"%s\"\n", internal_api_key);
    
    // when true, the application starts the search process
    bool search = false;
    bool edit_mode = false;
    char last_search[512] = {0};
    SearchType last_search_type = -1;
    Vector2 search_result_scrollbar_pos = { 10, 10 };

    // the current_query that the user has constructed
    Query query = {
        .allow_youtube_shorts = true,
        .string = "",
        .media = ANY,
        .sort = BY_RELEVANCE,
        .search_type = SEARCH_TYPE_QUERIED,
        .search_attr = SEARCH_ATTR_NEW,
    };

    init_app();

    Ui ui;
    ui.font = GetFontDefault();
    ui.padding = 5;
    ui.spacing = 2;
    ui.word_wrap = true;

    char focused_video_id[64] = {0};
    char focused_video_description[4096] = {0};
    Vector2 video_desc_scrollbar_pos = { 10, 10 };

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

            free_thumbnail_queue(&thumbnail_queue); thumbnail_queue = init_thumbnail_queue();

            // evade bot detection
            if (strcmp(last_search, query.string) == 0) {
                cycle_connection(&youtube_pool);
            }

            strncpy(last_search, query.string, sizeof(last_search) - 1);
            last_search_type = query.search_type;

            char path[128];
            if (resolve_youtube_api_path(sizeof(path), path, query.search_type, internal_api_key)) {
                PreparedRequest req = {0};
                if  (configure_post_body(sizeof(req.body), req.body, query, results.continuation_token, focused_video_id) && 
                    (configure_post_header(sizeof(req.header), req.header, youtube_pool.connections->host, path, strlen(req.body)))) {

                    PersistentConnection* conn = &youtube_pool.connections[youtube_pool.current_conn];

                    void* targs = NULL;
                    void* (*thread_funt)(void*) = NULL;

                    switch (query.search_type) {
                        case SEARCH_TYPE_QUERIED:
                        case SEARCH_TYPE_TRENDING:
                        case SEARCH_TYPE_LOAD_MORE:
                        case SEARCH_TYPE_RELATED:
                            targs = init_search_thread_args(query, req, &results, &thumbnail_queue, conn);
                            thread_funt = get_results_from_query;
                            break; 
                        case SEARCH_TYPE_VIDEO_FOCUS:
                            targs = init_focused_info_args(req, conn, focused_video_description, sizeof(focused_video_description));
                            thread_funt = get_focused_video_information;
                            break;
                        default:
                            printf("query type not supported\n");
                            break;
                    }

                    if (targs && thread_funt) {
                        if (launch_task(&task_queue, targs, thread_funt) == false) {
                            printf("failed to launch task\n");
                            free(targs);
                        }
                    }
                }   
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

            // edit_mode toggles when search box is focused (T) or not (F)
            int text_box_status;
            if ((text_box_status = GuiTextBox(search_bar_bounds, query.string, sizeof(query.string), edit_mode))) {
                edit_mode = !edit_mode;
            }

            bool enter_key_pressed = text_box_status == 2;

            if (GuiButton(search_button_bounds, "Search") || enter_key_pressed) {
                if (trim_whitespace(query.string) > 0) {
                    // search = search_finished;
                    search = true;
                    query.search_type = SEARCH_TYPE_QUERIED;
                    query.search_attr = SEARCH_ATTR_NEW;
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
                // search = search_finished;
                search = true;
                query.search_type = SEARCH_TYPE_TRENDING;
                query.search_attr = SEARCH_ATTR_NEW;
                SetWindowTitle(TextFormat("[trending(loading)] - metube"));
            }

            const Rectangle related_videos_button_bounds = {
                .x = trending_button_bounds.x + trending_button_bounds.width + ui.padding,
                .y = ui.padding,
                .width = 85,
                .height = 25
            };

            if (focused_video_id[0] == '\0') {
                GuiSetState(STATE_DISABLED);
            }
            
            if (GuiButton(related_videos_button_bounds, "Related Videos")) {
                // search = search_finished;
                search = true;
                query.search_type = SEARCH_TYPE_RELATED;
                query.search_attr = SEARCH_ATTR_NEW;
                SetWindowTitle(TextFormat("[%s(loading)] - metube", focused_video_id));
            }

            GuiSetState(STATE_NORMAL);

            const Rectangle filter_window_bounds = {
                .x = ui.padding, 
                .y = search_button_bounds.y + search_button_bounds.height + ui.padding, 
                .width = search_bar_bounds.width, 
                .height = 75
            };

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
                // search = search_finished;
                search = true;
                query.search_type = last_search_type == SEARCH_TYPE_RELATED ? SEARCH_TYPE_RELATED : SEARCH_TYPE_QUERIED;
                query.search_attr = SEARCH_ATTR_APPENDING;
                SetWindowTitle(TextFormat("[%s(appending)] - metube", last_search));
            }

            GuiSetState(STATE_NORMAL);
            
            const Rectangle scroll_window_bounds = { 
                .x = ui.padding, 
                .y = search_bar_bounds.y + search_bar_bounds.height + filter_window_bounds.height + (ui.padding * 2), 
                .width = search_bar_bounds.width, 
                .height = GetScreenHeight() - scroll_window_bounds.y - load_more_button_bounds.height - (ui.padding * 2), 
            };

            const int container_height = 80;
            
            const Rectangle content_area = {
                .x = scroll_window_bounds.x,
                .y = scroll_window_bounds.y,
                .width = scroll_window_bounds.width,
                .height = container_height * results.count,
            };

            const bool vertical_scrollbar_visible = content_area.height > scroll_window_bounds.height;

            GuiScrollPanel(scroll_window_bounds, NULL, content_area, &search_result_scrollbar_pos, NULL, true);

            const Rectangle scissor_rect = padded_rectangle(1, scroll_window_bounds);

            BeginScissorMode(scissor_rect.x, scissor_rect.y, scissor_rect.width, scissor_rect.height);

            int i = 0;
            float container_y = scroll_window_bounds.y;
            for (SearchResult* search_result = results.head; search_result; search_result = search_result->next, i++, container_y += container_height) {
                const Rectangle container = { 
                    .x = ui.padding, 
                    .y = container_y + search_result_scrollbar_pos.y, 
                    .width = scroll_window_bounds.width - (vertical_scrollbar_visible ? SCROLLBAR_WIDTH : 0),
                    .height = container_height 
                };

                const Color container_color = (i % 2) ? WHITE : RAYWHITE;

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
                    
                    PersistentConnection* conn = &pool->connections[pool->current_conn];
                    if (queue_thumbnail_load(search_result->id, search_result->thumbnail_path, conn)) {
                        cycle_connection(pool);
                    }
                }

                if (CheckCollisionRecs(container, scissor_rect)) {
                    draw_search_result(search_result, thumbnail, container, container_color, ui);

                    if ((CheckCollisionPointRec(GetMousePosition(), container)) && 
                        (CheckCollisionPointRec(GetMousePosition(), scissor_rect)) &&
                        (IsMouseButtonPressed(MOUSE_BUTTON_LEFT))) {
                        if ((search_result->media_type == VIDEO) || (search_result->media_type == SHORT) || (search_result->media_type == LIVE)) {
                            // search = search_finished
                            search = true;
                            query.search_type = SEARCH_TYPE_VIDEO_FOCUS;
                            strncpy(focused_video_id, search_result->id, sizeof(focused_video_id) - 1);
                        }

                        else focused_video_description[0] = focused_video_id[0] = '\0';
                    }
                }   
            }

            EndScissorMode();

            const Rectangle focused_video_bounds = {
                .x = scroll_window_bounds.x + scroll_window_bounds.width + ui.padding,
                .y = filter_window_bounds.y,
                .width = GetScreenWidth() - focused_video_bounds.x,
                .height = GetScreenHeight() - focused_video_bounds.y - ui.padding,
            };
            
            draw_video_desc(focused_video_bounds, ui, &video_desc_scrollbar_pos, focused_video_description);

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
    // clean everything

// video playing function
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
        // handle clicked search results
    // reccomendations
    // use goto's for redundant cleanups
    // set ptrs to NULL after freeing them