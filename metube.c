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

#include "raylib.h"
#include "raylib/src/raylib.h"
#define RAYGUI_IMPLEMENTATION
#include "raygui.h"

#define MAX_THREADS 4

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
    Texture2D thumbnail;
    struct SearchResult* next; 
} SearchResult;

SearchResult *init_search_result()
{
    SearchResult *search_result = (SearchResult*) malloc(sizeof(SearchResult));
    if (search_result == NULL) {
        printf("init_search_result: malloc returned NULL\n");
        return NULL;
    }

    search_result->media_type = UNDF;
    search_result->thumbnail_loaded = false;
    search_result->thumbnail = (Texture2D){0};
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
    printf("id) %s title) %s author) %s subs) %s views) %s date) %s length) %s video count) %s type) %d\n", 
            search_result->id, search_result->title, search_result->author, search_result->subscriber_count, search_result->view_count, search_result->date_published, search_result->duration, search_result->video_count, search_result->media_type);
}

// linked list of search results returned from a query
typedef struct
{
    size_t count;           
    SearchResult* head;    
    SearchResult* tail;     
} Results;

Results init_results() 
{
    Results search_results;
    search_results.head = search_results.tail = NULL;
    search_results.count = 0;
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

// represents user-defined parameters for a YouTube search request
typedef struct
{
    bool allow_youtube_shorts;      
    char string[512];        
    MediaType media;          
    SortType sort;           
} Query;

// holds raw thumbnail image data fetched from an HTTP request
// intended for later conversion to a Texture (see LoadTextureFromMemory in raylib)
typedef struct ThumbnailData
{
    Buffer image_data;              
    char search_result_id[256];     
    struct ThumbnailData *next;
} ThumbnailData;

void free_thumbnail_data(ThumbnailData *thumbnail_data)
{
    if (!thumbnail_data) return;
    if (buffer_ready(&thumbnail_data->image_data)) free_buffer(&thumbnail_data->image_data);
    free(thumbnail_data);
}

// thread-safe queue for storing in-memory thumbnail data. 
// supports appending from a background thread and consuming from the main thread
typedef struct 
{
    size_t count;
    ThumbnailData *head;
    ThumbnailData *tail;  
    pthread_mutex_t mutex;
} ThumbnailQueue;

ThumbnailQueue init_thumbnail_queue()
{
    ThumbnailQueue thumbnail_queue;
    thumbnail_queue.count = 0;
    thumbnail_queue.head = thumbnail_queue.tail = NULL;
    pthread_mutex_init(&thumbnail_queue.mutex, NULL);
    return thumbnail_queue;
}

void enqueue_thumbnail(ThumbnailQueue *thumbnail_queue, ThumbnailData *thumbnail_data) 
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

ThumbnailData* dequeue_thumbnail(ThumbnailQueue *thumbnail_queue)
{
    if (!thumbnail_queue) {
        printf("dequeue_thumbnail: 'thumbnail_queue' arg is NULL\n");
        return NULL;
    }

    if (thumbnail_queue->count == 0) {
        printf("dequeue_thumbnail: 'thumbnail_queue' arg is empty\n");
        return NULL;
    }

    ThumbnailData *ret = thumbnail_queue->head;

    thumbnail_queue->head = ret->next;
    if (!thumbnail_queue->head) {
        thumbnail_queue->tail = NULL;
    }

    thumbnail_queue->count--;

    return ret;
}

void free_thumbnail_queue(ThumbnailQueue *thumbnail_queue)
{
    if (!thumbnail_queue) return;
    while (thumbnail_queue->head) free_thumbnail_data(dequeue_thumbnail(thumbnail_queue));
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
        size_t to_read = bytes_remaining < sizeof(data) ? bytes_remaining : sizeof(data);
        
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
    char body[1024];
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
        
        size_t chunk_size = -1; 
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

// returns an allocated string that is the url encoding of the string passed
char* url_encode_string(const char *str)
{
    if (!str) {
        printf("url_encode: arguement is NULL");
        return NULL;
    }

    const size_t str_len = strlen(str);

    // worst case senario is when all characters are url encoded
    char *encoded_str = malloc((str_len * 3) + 1);
    if (!encoded_str) {
        printf("url_encode_string: malloc returned NULL\n");
        return NULL;
    }

    char *ptr = encoded_str;
    
    for (size_t i = 0; i < str_len; i++) {
        unsigned char c = (unsigned) str[i];
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            *ptr++ = c;
        }
        
        // every non alpha character is replace with a % and 2 hex digits
        else {
            sprintf(ptr, "%%%02X", c);
            ptr += 3;
        }
    }

    (*ptr) = '\0';

    return encoded_str;
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
            "Content-Length: %zu\r\n"
            "Connection: keep-alive\r\n"
            // "Connection: close\r\n"
            "\r\n",
            path, host, post_body_length);
}

size_t configure_post_body(char *post_body, size_t n, const char *query, const char *params, const char *continuation)
{
    if (continuation && continuation[0] != '\0') {
        return snprintf(post_body, n,
            "{\n"
            "  \"context\": {\n"
            "    \"client\": {\n"
            "      \"clientName\": \"WEB\",\n"
            "      \"clientVersion\": \"2.20210721.00.00\"\n"
            "    }\n"
            "  },\n"
            "  \"continuation\": \"%s\"\n"
            "}", continuation);
    } 

    else if ((query && query[0] != '\0') && (params && params[0] != '\0')) {
            return snprintf(post_body, n,
                "{\n"
                "  \"context\": {\n"
                "    \"client\": {\n"
                "      \"clientName\": \"WEB\",\n"
                "      \"clientVersion\": \"2.20210721.00.00\"\n"
                "    }\n"
                "  },\n"
                "  \"query\": \"%s\",\n"
                "  \"params\": \"%s\"\n"
                "}", query, params);
    }

    else
        return snprintf(post_body, n,
            "{ \"error\": \"Missing query or continuation\" }");
}

int bound_index_to_array (const int pos, const int array_size)
{
    return (pos + array_size) % array_size;
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

SearchResult* create_search_node_from_json(cJSON *item, const bool allow_shorts)
{
    SearchResult *search_result = init_search_result();
    if (search_result == NULL) return NULL;

    // the item (the nth element of 'contents' json obj) is either a video, channel, or playlist
    // thus, only one of the values will not NULL
    cJSON *channelRenderer = cJSON_GetObjectItem(item, "channelRenderer");
    cJSON *videoRenderer = cJSON_GetObjectItem(item, "videoRenderer");
    cJSON *lockupViewModel = cJSON_GetObjectItem(item, "lockupViewModel");
    
    if (videoRenderer) {
        // check if the video is a short, i fucking hate yt shorts...
        if (video_is_youtube_short(videoRenderer) && !allow_shorts) {
            free_search_result(search_result);
            return NULL;
        }

        // id
        cJSON *videoId = cJSON_GetObjectItem(videoRenderer, "videoId");
        if (!videoId || !videoId->valuestring) {
            search_result->media_type = UNDF;
            free_search_result(search_result);
            return NULL;
        }
        
        strncpy(search_result->id, videoId->valuestring, sizeof(search_result->id) - 1);
        search_result->id[sizeof(search_result->id) - 1] = '\0';

        
        // title
        cJSON *title = cJSON_GetObjectItem(videoRenderer, "title");
        cJSON* runs = title ? cJSON_GetObjectItem(title, "runs") : NULL;
        if (runs && cJSON_IsArray(runs)) {
            cJSON* first_element = cJSON_GetArrayItem(runs, 0);
            cJSON *text = first_element ? cJSON_GetObjectItem(first_element, "text") : NULL;
            if (text && text->valuestring) {
                strncpy(search_result->title, text->valuestring, sizeof(search_result->title));
            }
        }

        // thumbnail path
        if (search_result->id[0] != '\0')
            snprintf(search_result->thumbnail_path, sizeof(search_result->thumbnail_path), "/vi/%s/mqdefault.jpg", search_result->id);

        // author
        cJSON *ownerText = cJSON_GetObjectItem(videoRenderer, "ownerText");
        runs = ownerText ? cJSON_GetObjectItem(ownerText, "runs") : NULL;
        if (runs && cJSON_IsArray(runs)) {
            cJSON *first_element = cJSON_GetArrayItem(runs, 0);
            cJSON *text = first_element ? cJSON_GetObjectItem(first_element, "text") : NULL;
            if (text && text->valuestring) {
                strncpy(search_result->author, text->valuestring, sizeof(search_result->author));
            }
        }

        // video can either be a livestream or normal vid
        cJSON *viewCountText = cJSON_GetObjectItem(videoRenderer, "viewCountText");
        
        runs = viewCountText ? cJSON_GetObjectItem(viewCountText, "runs") : NULL;
        cJSON *simpleText = viewCountText ? cJSON_GetObjectItem(viewCountText, "simpleText") : NULL;

        if (runs && cJSON_IsArray(runs)) {
            cJSON *first_element = cJSON_GetArrayItem(runs, 0);
            cJSON *text = first_element ? cJSON_GetObjectItem(first_element, "text") : NULL;
            if (text && text->valuestring) {
                strncpy(search_result->view_count, text->valuestring, sizeof(search_result->view_count));
                format_view_count(search_result->view_count);
                search_result->media_type = LIVE;
            }
        }
        
        else if (simpleText && simpleText->valuestring) {
            strncpy(search_result->view_count, simpleText->valuestring, sizeof(search_result->view_count));
            format_view_count(search_result->view_count);
            search_result->media_type = VIDEO;
        }

        // publish date
        cJSON *publishedTimeText = cJSON_GetObjectItem(videoRenderer, "publishedTimeText");
        simpleText = publishedTimeText ? cJSON_GetObjectItem(publishedTimeText, "simpleText") : NULL;
        if (simpleText && simpleText->valuestring) {
            strncpy(search_result->date_published, simpleText->valuestring, sizeof(search_result->date_published));
        }

        // video length
        cJSON *lengthText = cJSON_GetObjectItem(videoRenderer, "lengthText");
        simpleText = lengthText ? cJSON_GetObjectItem(lengthText, "simpleText") : NULL;
        if (simpleText && simpleText->valuestring) {
            strncpy(search_result->duration, simpleText->valuestring, sizeof(search_result->duration));
        }
    }

    else if (channelRenderer) {
        search_result->media_type = CHANNEL;

        // id
        cJSON* channelId = cJSON_GetObjectItem(channelRenderer, "channelId");
        if (!channelId || !channelId->valuestring) {
            free_search_result(search_result);
            return NULL;
        }
        strncpy(search_result->id, channelId->valuestring, sizeof(search_result->id) - 1);
        search_result->id[sizeof(search_result->id) - 1] = '\0';

        // title
        cJSON *title = cJSON_GetObjectItem(channelRenderer, "title");
        cJSON *simpleText = title ? cJSON_GetObjectItem(title, "simpleText") : NULL;
        if (simpleText && simpleText->valuestring) {
            strncpy(search_result->title, simpleText->valuestring, sizeof(search_result->title));
        }

        // subscriber count
        cJSON *videoCountText = cJSON_GetObjectItem(channelRenderer, "videoCountText");
        simpleText = videoCountText ? cJSON_GetObjectItem(videoCountText, "simpleText") : NULL;
        if(simpleText && simpleText->valuestring) {
            strncpy(search_result->subscriber_count, simpleText->valuestring, sizeof(search_result->subscriber_count));
        }

        // thumbnail link
        cJSON *thumbnails = cJSON_GetObjectItem(cJSON_GetObjectItem(channelRenderer, "thumbnail"), "thumbnails");
        if (thumbnails && cJSON_IsArray(thumbnails)) {
            cJSON *first_thumbnail = cJSON_GetArrayItem(thumbnails, 0);
            cJSON *url = first_thumbnail ? cJSON_GetObjectItem(first_thumbnail, "url") : NULL;
            if(url && url->valuestring) {
                // the path either starts with '/ytc', or just '/'
                char *path1 = strstr(url->valuestring, "/ytc");
                char *path2 = strrchr(url->valuestring, '/');
                strncpy(search_result->thumbnail_path, path1 ? path1 : path2, sizeof(search_result->thumbnail_path));
            }
        }
    }

    else if (lockupViewModel) {
        search_result->media_type = PLAYLIST;

        // id
        cJSON *contentId = cJSON_GetObjectItem(lockupViewModel, "contentId");
        if (!contentId || !contentId->valuestring) {
            free_search_result(search_result);
            return NULL;
        }
        strncpy(search_result->id, contentId->valuestring, sizeof(search_result->id) - 1);
        search_result->id[sizeof(search_result->id) - 1] = '\0';

        // title
        cJSON *metadata = cJSON_GetObjectItem(lockupViewModel, "metadata");
        cJSON *lockupMetadataViewModel = metadata ? cJSON_GetObjectItem(metadata, "lockupMetadataViewModel") : NULL;
        cJSON *title = lockupMetadataViewModel ? cJSON_GetObjectItem(lockupMetadataViewModel, "title") : NULL;
        cJSON *content = title ? cJSON_GetObjectItem(title, "content") : NULL;
        if (content && content->valuestring) {
            strncpy(search_result->title, content->valuestring, sizeof(search_result->title));
        }

        cJSON *contentImage = cJSON_GetObjectItem(lockupViewModel, "contentImage");
        cJSON *collectionThumbnailViewModel = contentImage ? cJSON_GetObjectItem(contentImage, "collectionThumbnailViewModel") : NULL;
        cJSON *primaryThumbnail = collectionThumbnailViewModel ? cJSON_GetObjectItem(collectionThumbnailViewModel, "primaryThumbnail") : NULL;
        cJSON *thumbnailViewModel = primaryThumbnail ? cJSON_GetObjectItem(primaryThumbnail, "thumbnailViewModel") : NULL;

        // thumbnail path
        cJSON *image = thumbnailViewModel ? cJSON_GetObjectItem(thumbnailViewModel, "image") : NULL;
        cJSON *sources = image ? cJSON_GetObjectItem(image, "sources") : NULL;
        if (sources && cJSON_IsArray(sources)) {
            cJSON* first_source = cJSON_GetArrayItem(sources, 0);
            cJSON *url = first_source ? cJSON_GetObjectItem(first_source, "url") : NULL;
            if (url && url->valuestring) {
                char *thumbnail_path = strstr(url->valuestring, "/vi");
                strncpy(search_result->thumbnail_path, thumbnail_path, sizeof(search_result->thumbnail_path));
            }
        }

        // number of videos in playlist
        cJSON *overlays = thumbnailViewModel ? cJSON_GetObjectItem(thumbnailViewModel, "overlays") : NULL;
        if (overlays && cJSON_IsArray(overlays)) {
            cJSON *overlay;
            cJSON_ArrayForEach (overlay, overlays) {
                cJSON *thumbnailOverlayBadgeViewModel = cJSON_GetObjectItem(overlay, "thumbnailOverlayBadgeViewModel");
                cJSON *thumbnailBadges = thumbnailOverlayBadgeViewModel ? cJSON_GetObjectItem(thumbnailOverlayBadgeViewModel, "thumbnailBadges") : NULL;
                if (thumbnailBadges && cJSON_IsArray(thumbnailBadges)) {
                    cJSON *thumbnailBadge;
                    cJSON_ArrayForEach (thumbnailBadge, thumbnailBadges) {
                        cJSON *thumbnailBadgeViewModel = cJSON_GetObjectItem(thumbnailBadge, "thumbnailBadgeViewModel");
                        if (thumbnailBadgeViewModel) {
                            cJSON *text = cJSON_GetObjectItem(thumbnailBadgeViewModel, "text");
                            if (text && text->valuestring) {
                                strncpy(search_result->video_count, text->valuestring, sizeof(search_result->video_count));
                                break;
                            }
                        }
                    }
                }
            }
        }
    }

    return search_result;
}

typedef struct 
{
    char search_result_id[64];
    PreparedRequest request;
    PersistentConnection *connection;
    ThumbnailQueue *thumbnail_queue;
} ThumbnailLoaderParams;

void* load_thumbnail(void *args)
{
    ThumbnailLoaderParams *targs = (ThumbnailLoaderParams*) args;
    
    Buffer thumbnail_buffer = send_https_request(targs->request, targs->connection);
    if (buffer_ready(&thumbnail_buffer) == false) {
        printf("load_thumbnail: send_http_request returned invalid buffer\n");
        free(targs);
        return NULL;
    }

    ThumbnailData *thumbnail_data = malloc(sizeof(ThumbnailData));
    if (thumbnail_data == NULL) {
        printf("load_thumbnail: malloc returned NULL for thumbnail_data\n");
        free(targs);
        return NULL;
    }

    thumbnail_data->image_data = thumbnail_buffer;
    strcpy(thumbnail_data->search_result_id, targs->search_result_id);

    pthread_mutex_lock(&targs->thumbnail_queue->mutex);
    enqueue_thumbnail(targs->thumbnail_queue, thumbnail_data);
    pthread_mutex_unlock(&targs->thumbnail_queue->mutex);

    free(targs);
    return NULL;
}

static char continuation_token[1024] = {0};
void extract_continuation_token(const cJSON *continuationItemRenderer)
{
    cJSON *continuationEndpoint = continuationItemRenderer ? cJSON_GetObjectItem(continuationItemRenderer, "continuationEndpoint") : NULL;
    cJSON *continuationCommand = continuationEndpoint ? cJSON_GetObjectItem(continuationEndpoint, "continuationCommand") : NULL;
    cJSON *token = continuationCommand ? cJSON_GetObjectItem(continuationCommand, "token") : NULL;
    if (token && cJSON_IsString(token)) 
        strncpy(continuation_token, token->valuestring, sizeof(continuation_token) - 1);
    else {
        printf("extract_continuation_token: token not found\n");
        memset(continuation_token, 0, sizeof(continuation_token));
    }
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

typedef enum
{
    NEW,
    APPENDING,
} SearchType;

typedef struct
{
    bool allow_youtube_shorts;
    PreparedRequest request;
    Results *search_results;
    ThumbnailQueue *thumbnail_queue;
    PersistentConnection *youtube_connection;
} SearchThreadArgs;

static int elements_added = 0; 
static bool delete_old_nodes = false;
static bool search_finished = true;
void* new_search(void* args)
{
    SearchThreadArgs* targs = (SearchThreadArgs*)args;
    float start_time = GetTime(); 
    elements_added = 0;

    Buffer http = send_https_request(targs->request, targs->youtube_connection);
    bool application_is_offline = (buffer_ready(&http) == false);
    if (application_is_offline) {
        SetWindowTitle("[offline] - metube");
        free(targs);
        search_finished = true;
        return NULL;
    }

    cJSON* cjson = cJSON_Parse(http.data);
    if (cjson == NULL) {
        printf("get_results_from_query: cJSON_Parse returned NULL\n");
        SetWindowTitle("[offline] - metube");
        free_buffer(&http);
        free(targs);
        search_finished = true;
        return NULL;
    }

    cJSON *contents = cJSON_GetObjectItem(cjson, "contents");
    cJSON *twoColumnSearchResultsRenderer = contents ? cJSON_GetObjectItem(contents, "twoColumnSearchResultsRenderer") : NULL;
    cJSON *primaryContents = twoColumnSearchResultsRenderer ? cJSON_GetObjectItem(twoColumnSearchResultsRenderer, "primaryContents") : NULL;
    cJSON *sectionListRenderer = primaryContents ? cJSON_GetObjectItem(primaryContents, "sectionListRenderer") : NULL;
    contents = sectionListRenderer ? cJSON_GetObjectItem(sectionListRenderer, "contents") : NULL;

    // next page token
    cJSON *continuationItemRenderer = contents ? cJSON_GetArrayItem(contents, 1)->child : NULL;
    extract_continuation_token(continuationItemRenderer);

    cJSON *itemSectionRenderer = contents ? cJSON_GetArrayItem(contents, 0)->child : NULL;
    contents = itemSectionRenderer ? cJSON_GetObjectItem(itemSectionRenderer, "contents") : NULL;
    if (contents && cJSON_IsArray(contents)) {
        cJSON *item;
        cJSON_ArrayForEach (item, contents) {
            SearchResult *search_result = create_search_node_from_json(item, targs->allow_youtube_shorts);
            if (search_result) {
                if (search_result->media_type != UNDF) {
                    elements_added++;
                    add_search_result(targs->search_results, search_result);
                }
                else free_search_result(search_result);
            }
        }
    }

    delete_old_nodes = true;
    search_finished = true;

    SetWindowTitle(TextFormat("[search results(%d)] - metube", elements_added));

    float end_time = GetTime();
    printf("new search took %f seconds, found %d items\n", end_time - start_time, elements_added);
    
    cJSON_Delete(cjson);
    free_buffer(&http);
    free(targs);
    return NULL;
}

void* load_more_results(void* args)
{
    SearchThreadArgs* targs = (SearchThreadArgs*)args;
    float start_time = GetTime(); 
    elements_added = 0;
    
    Buffer http = send_https_request(targs->request, targs->youtube_connection);
    bool application_is_offline = (buffer_ready(&http) == false);
    if (application_is_offline) {
        SetWindowTitle("[offline] - metube");
        free(targs);
        search_finished = true;
        return NULL;
    }

    cJSON* cjson = cJSON_Parse(http.data);
    if (cjson == NULL) {
        printf("get_results_from_query: cJSON_Parse returned NULL\n");
        SetWindowTitle("[offline] - metube");
        free_buffer(&http);
        free(targs);
        search_finished = true;
        return NULL;
    }

    cJSON *onResponseReceivedCommands = cJSON_GetObjectItem(cjson, "onResponseReceivedCommands");
    cJSON *firstElement = onResponseReceivedCommands && cJSON_IsArray(onResponseReceivedCommands) ? cJSON_GetArrayItem(onResponseReceivedCommands, 0) : NULL;
    cJSON *appendContinuationItemsAction = firstElement ? cJSON_GetObjectItem(firstElement, "appendContinuationItemsAction") : NULL;
    cJSON *continuationItems = appendContinuationItemsAction ? cJSON_GetObjectItem(appendContinuationItemsAction, "continuationItems") : NULL;
    
    // getting search result items
    cJSON *itemSectionRenderer = continuationItems && cJSON_IsArray(continuationItems) ? cJSON_GetArrayItem(continuationItems, 0)->child : NULL; 
    cJSON *contents = itemSectionRenderer ? cJSON_GetObjectItem(itemSectionRenderer, "contents") : NULL;
    if (contents && cJSON_IsArray(contents)) {
        cJSON *item;
        cJSON_ArrayForEach (item, contents) {
            SearchResult *search_result = create_search_node_from_json(item, targs->allow_youtube_shorts);
            if (search_result) {
                if (search_result->media_type != UNDF) {
                    elements_added++;
                    add_search_result(targs->search_results, search_result);
                }
                else free_search_result(search_result);
            }
        }
    }
    
    // next page token
    cJSON *continuationItemRenderer = continuationItems ? cJSON_GetArrayItem(continuationItems, 1)->child : NULL;
    extract_continuation_token(continuationItemRenderer);
    
    search_finished = true;

    SetWindowTitle(TextFormat("[search results(%d)] - metube", targs->search_results->count));

    float end_time = GetTime();
    printf("appending search took %f seconds, found %d items\n", end_time - start_time, elements_added);
    
    cJSON_Delete(cjson);
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

Texture2D load_thumbnail_from_memory(const Buffer buffer, const float width, const float height)
{
    if (!buffer_ready(&buffer)) {
        printf("load_thumbnail_from_mem: buffer obj passed is invalid\n");
        return (Texture2D){0};
    }

    else if (width < 0 || height < 0) {
        printf("load_thumbnail_from_mem: thumbnail dimensions are invalid\n");
        return (Texture2D){0};
    }

    Image image = LoadImageFromMemory(".jpg", (unsigned char *)buffer.data, buffer.size);
    if (!IsImageReady(image)) {
        printf("load_thumbnail_from_mem: LoadImageFromMemory returned invalid image obj\n");
        return (Texture2D){0};
    }

    ImageResize(&image, width, height);
    
    Texture2D ret = LoadTextureFromImage(image);
    if (!IsTextureReady(ret)) {
        printf("load_thumbnail_from_mem: LoadTextureFromImage returned invalid Texture obj\n");
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

    create_file_from_memory("response", youtube_page);

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

int main()
{
    SSL_library_init();
    OpenSSL_add_all_algorithms();
    
    Results results = init_results();
    ThumbnailQueue thumbnail_queue = init_thumbnail_queue();
    TaskQueue task_queue = init_task_queue();
    pthread_t thread_pool[MAX_THREADS];
    init_thread_pool(MAX_THREADS, thread_pool, worker_thread_funct, &task_queue);
    
    ctx = SSL_CTX_new(TLS_client_method());
    if (!ctx) {
        printf("error initalizing SSL_CTX object\n");
        return 1;
    } 
    
    int current_yt_conn = 0;
    PersistentConnection yt_connections[N_CONN] = {0};
    for (int c = 0; c < N_CONN; c++) init_persistent_connection(&yt_connections[c], media_type_to_host(ANY), "443");

    int current_video_conn = 0;
    PersistentConnection video_connections[N_CONN] = {0};
    for (int c = 0; c < N_CONN; c++) init_persistent_connection(&video_connections[c], media_type_to_host(VIDEO), "443");

    int current_channel_conn = 0;
    PersistentConnection channel_connections[N_CONN] = {0};
    for (int c = 0; c < N_CONN; c++) init_persistent_connection(&channel_connections[c], media_type_to_host(CHANNEL), "443");

    char internal_api_key[64];
    printf("extracting internal youtube api key..\n");
    youtube_internal_api_key(&yt_connections[current_yt_conn], sizeof(internal_api_key), internal_api_key);
    printf("INTERNAL KEY: \"%s\"\n", internal_api_key);

    // when true, the application starts the search process
    bool search = false;
    char last_search[512] = {0};

    // the current_query that the user has constructed
    Query query = {0};
    SearchType search_type;

    // used in 'GuiTextBox' function
    // only true when the text window is focused
    bool edit_mode = false;

    // for filter window
    bool show_filter_window = false;

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
        pthread_mutex_lock(&thumbnail_queue.mutex);
        while (thumbnail_queue.head) {
            ThumbnailData *thumbnail_data = dequeue_thumbnail(&thumbnail_queue);
            
            for (SearchResult *search_node = results.head; search_node; search_node = search_node->next) {
                if (strcmp(thumbnail_data->search_result_id, search_node->id) == 0) {
                    if (IsTextureReady(search_node->thumbnail)) UnloadTexture(search_node->thumbnail);
                    search_node->thumbnail = load_thumbnail_from_memory(thumbnail_data->image_data, 150, 80);
                    break;
                }
            }
            
            free_thumbnail_data(thumbnail_data);
        }
        pthread_mutex_unlock(&thumbnail_queue.mutex);

        if (delete_old_nodes) {
            scroll.y = 0;
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
            search = false;
            search_finished = false;
            SearchThreadArgs *targs = malloc(sizeof(SearchThreadArgs));
            if (!targs) 
                printf("main: malloc returned NULL for targs\n");
            else {
                const char *display_string = search_type == NEW ? "loading" : "appending";
                SetWindowTitle(TextFormat("[%s(%s)] - metube",query.string, display_string));
                   
                free_thumbnail_queue(&thumbnail_queue);
                thumbnail_queue = init_thumbnail_queue();

                targs->allow_youtube_shorts = query.allow_youtube_shorts;
                targs->search_results = &results;
                targs->thumbnail_queue = &thumbnail_queue;
                targs->youtube_connection = &yt_connections[current_yt_conn];

                // only cycle through connections on repeated searches to avoid bot detection
                if (strcmp(last_search, query.string) == 0 && search_type == NEW) {
                    current_yt_conn = bound_index_to_array((current_yt_conn + 1), N_CONN);
                }
                
                strcpy(last_search, query.string);

                const char *params = TextFormat("%s%s", sort_type_to_url(query.sort), media_type_to_url(query.media));
                const char *continuation = search_type == APPENDING ? continuation_token : NULL;
                configure_post_body(targs->request.body, sizeof(targs->request.body), query.string, params, continuation);
                
                const char *search_path = TextFormat("/youtubei/v1/search?key=%s", internal_api_key);
                configure_post_header(sizeof(targs->request.header), targs->request.header, targs->youtube_connection->host, search_path, strlen(targs->request.body));
                
                // awaken a worker thread to handle thread function
                ThreadTask *search_task = malloc(sizeof(ThreadTask));
                if (!search_task) {
                    printf("main: malloc returned NULL for ThreadTask object\n");
                    free(targs);
                } 
                
                else {
                    search_task->next = NULL;
                    search_task->args = targs;

                    if (search_type == APPENDING) search_task->funct = load_more_results;
                    else if (search_type == NEW) search_task->funct = new_search;

                    pthread_mutex_lock(&task_queue.mutex);
                    enqueue_task(search_task, &task_queue);
                    pthread_cond_signal(&task_queue.cond);
                    pthread_mutex_unlock(&task_queue.mutex);
                }
            }   
        }

        BeginDrawing();
        ClearBackground(RAYWHITE);
        //---------------------------------------------------------------searching UI--------------------------------------------------------------------------------------//
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
                // sanitize query
                remove_leading_whitespace(query.string);
                remove_trailing_whitespace(query.string);

                // load url encoded string into query 
                if (query.string[0] != '\0') {
                    search = search_finished;
                    search_type = NEW;
                }
            }
        //---------------------------------------------------------------searching UI--------------------------------------------------------------------------------------//

        //---------------------------------------------------------------filtering UI--------------------------------------------------------------------------------------//
            const Rectangle filter_button_bounds = { 
                .x = search_button_bounds.x + search_button_bounds.width + ui.padding, 
                .y = ui.padding, 
                .width = 50, 
                .height = 25 
            };
            
            const Rectangle filter_window_bounds = {
                .x = ui.padding, 
                .y = search_button_bounds.y + search_button_bounds.height + ui.padding, 
                .width = search_bar_bounds.width, 
                .height = 75
            };

            // toggle filter window on press
            if (GuiButton(filter_button_bounds, "Filter")) show_filter_window = !show_filter_window;
            if (show_filter_window) {
                draw_filter_window(&query, filter_window_bounds, ui.font, ui.padding);
            }
        //---------------------------------------------------------------filtering UI--------------------------------------------------------------------------------------//

        //---------------------------------------------------------------displaying UI---------------------------------------------------------------------------------------//
            const float button_height = 30;
            const Rectangle load_more_button_bounds = {
                .x = search_bar_bounds.x,
                .height = button_height,
                .y = GetScreenHeight() - button_height - ui.padding,
                .width = search_bar_bounds.width,
            };

            if (results.count > 0 && continuation_token[0] != '\0') {
                if (GuiButton(load_more_button_bounds, "LOAD MORE") && query.string[0] != '\0') {
                    search_type = APPENDING;
                    search = search_finished;
                }
            }
            
            const Rectangle scroll_window_bounds = { 
                .x = search_bar_bounds.x, 
                .y = search_bar_bounds.y + search_bar_bounds.height + (show_filter_window ? (ui.padding + filter_window_bounds.height) : 0) + ui.padding, 
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
                // area of the ith rectangle
                Rectangle content_rect = { 
                    .x = ui.padding, 
                    .y = scissor_rect.y + scroll.y, // scroll is added so moving the scrollbar offsets all elements
                    .width = scissor_rect.width - SCROLLBAR_WIDTH,
                    .height = content_height 
                };
                
                // for every search result, draw a container and display its data
                int i = 0;
                for (SearchResult *search_result = results.head; search_result; search_result = search_result->next, i++, content_rect.y += content_height) {
                    if (CheckCollisionRecs(content_rect, scissor_rect) == false) {
                        continue;
                    }

                    const Color background_color = (i % 2) ? WHITE : RAYWHITE;
                    DrawRectangleRec(content_rect, background_color);
                    
                    const Rectangle thumbnail_bounds = { 
                        .x = content_rect.x, 
                        .y = content_rect.y, 
                        .width = content_rect.width * 0.45f, 
                        .height = content_rect.height 
                    };
                    
                    const Rectangle title_bounds = {
                        thumbnail_bounds.x + thumbnail_bounds.width,
                        content_rect.y,
                        content_rect.width - thumbnail_bounds.width,
                        content_rect.height * 0.70f
                    };

                    if (IsTextureReady(search_result->thumbnail)) {
                        DrawTexturePro(search_result->thumbnail, (Rectangle){0,0,150,80}, thumbnail_bounds, (Vector2){0,0}, 0, WHITE);
                    }
                    
                    else if (search_result->thumbnail_loaded == false) {
                        search_result->thumbnail_loaded = true;

                        ThumbnailLoaderParams *targs = (ThumbnailLoaderParams*) malloc(sizeof(ThumbnailLoaderParams));
                        targs->thumbnail_queue = &thumbnail_queue;
                        
                        if (search_result->media_type == CHANNEL) {
                            targs->connection = &channel_connections[current_channel_conn];
                            current_channel_conn = bound_index_to_array((current_channel_conn + 1), N_CONN);
                        }

                        else {
                            targs->connection = &video_connections[current_video_conn];
                            current_video_conn = bound_index_to_array((current_video_conn + 1), N_CONN);
                        }
                        
                        targs->request.body[0] = '\0';
                        strcpy(targs->request.path, search_result->thumbnail_path);
                        configure_get_header(sizeof(targs->request.header), targs->request.header, targs->connection->host, targs->request.path);
                        
                        strcpy(targs->search_result_id, search_result->id);

                        ThreadTask *thread_task = malloc(sizeof(ThreadTask));
                        thread_task->next = NULL;
                        thread_task->args = targs;
                        thread_task->funct = load_thumbnail;

                        pthread_mutex_lock(&task_queue.mutex);
                        enqueue_task(thread_task, &task_queue);
                        pthread_cond_signal(&task_queue.cond);
                        pthread_mutex_unlock(&task_queue.mutex);
                    }

                    if (search_result->title[0] != '\0') {
                        DrawTextBoxed(search_result->title, padded_rectangle(ui.padding, title_bounds), ui, 12, BLACK);                            
                    }

                    const Rectangle subtext_bounds = {
                        .x = thumbnail_bounds.x + thumbnail_bounds.width,
                        .y = title_bounds.y + title_bounds.height,
                        .width = title_bounds.width,
                        .height = content_rect.height - title_bounds.height,
                    };

                    switch (search_result->media_type) {
                        case VIDEO:
                            DrawTextBoxed(TextFormat("%s - %s views", search_result->date_published, search_result->view_count), padded_rectangle(ui.padding, subtext_bounds), ui, 11.5, BLACK);
                            draw_thumbnail_subtext(thumbnail_bounds, ui, RAYWHITE, 11, search_result->duration);
                            break;
                        case LIVE:
                            DrawTextBoxed(TextFormat("%s watching", search_result->view_count), padded_rectangle(ui.padding, subtext_bounds), ui, 11.5, BLACK);
                            draw_thumbnail_subtext(thumbnail_bounds, ui, RAYWHITE, 11, "LIVE");
                            break;
                        case CHANNEL:
                            DrawTextBoxed(search_result->subscriber_count, padded_rectangle(ui.padding, subtext_bounds), ui, 11.5, BLACK);
                            break;
                        case PLAYLIST:
                            draw_thumbnail_subtext(thumbnail_bounds, ui, RAYWHITE, 11, search_result->video_count);
                            break;
                        default:    
                            break;
                    }
                }
                
            EndScissorMode();
        //---------------------------------------------------------------displaying UI--------------------------------------------------------------------------//
        EndDrawing();
    }

    // deinit app
    UnloadFont(ui.font);
    free_results(&results);
    free_thumbnail_queue(&thumbnail_queue);
    
    // ssl stuff
    if (ctx) SSL_CTX_free(ctx);
    for (int c = 0; c < N_CONN; c++) free_persistent_connection(&yt_connections[c]);
    for (int c = 0; c < N_CONN; c++) free_persistent_connection(&video_connections[c]);
    for (int c = 0; c < N_CONN; c++) free_persistent_connection(&channel_connections[c]);
    
    // free worker thread stuff
    application_running = false;
    pthread_cond_broadcast(&task_queue.cond);
    free_thread_pool(MAX_THREADS, thread_pool);
    free_task_queue(&task_queue);         
    
    CloseWindow();
    return 0;
}

// searching feature
    // clean everything

// video playing function
    // show video information when double clicking video
    // play video when pressing button

// video management function
    // subscribe to different channels
    // have a liked videos playist
    // able to add videos to playlist

// after everythings done:
    // fix bastard bug
    // missing images from appending seach -> the address of the thumbnail changes from time loading it to drawing it
    // fonts for L.O.T.E.
    // handle cleanup when prematurley deleting
        // thumbnail data list
        // search arguements
    // switching between wired and wifi causes some sort of deadlock

