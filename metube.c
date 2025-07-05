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
#define RAYGUI_IMPLEMENTATION
#include "raygui.h"

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

int resize_buffer(Buffer *buffer, const size_t new_size)
{
    if (!buffer) {
        printf("resize_buffer: buffer arg is NULL\n");
        return -1;
    }

    char *new_data = realloc(buffer->data, new_size + 1);
    if ((new_data == NULL) && (new_size > 0)) {
        printf("resize_buffer: failed to realloc %zu bytes\n", new_size);
        return -1;
    }

    buffer->data = new_data;
    buffer->size = new_size;
    buffer->data[buffer->size] = '\0';
    return 0;
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
        case LIVE:
        case VIDEO:
        case PLAYLIST: return "i.ytimg.com";
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
    bool thumbnail_loaded;
    char thumbnail_path[256];   // path to thumbnail link, relative to its host (see media type to host)    
    Texture thumbnail;

    struct SearchResult* next; 
} SearchResult;

void free_search_result(SearchResult *search_result)
{
    if (!search_result) return;
    free(search_result);
}

void print_search_result(const SearchResult *search_result) 
{
    printf("id) %s title) %s author) %s subs) %s views) %s date) %s length) %s video count) %s thumbnail id) %d type) %d\n", 
            search_result->id, search_result->title, search_result->author, search_result->subscriber_count, search_result->view_count, search_result->date_published, search_result->duration, search_result->video_count, search_result->thumbnail.id, search_result->media_type);
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
    char *encoded_query;        
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

    while (thumbnail_queue->head) {
        ThumbnailData *to_free = thumbnail_queue->head;
        thumbnail_queue->head = thumbnail_queue->head->next;
        free_thumbnail_data(to_free);
    }

    thumbnail_queue->count = 0;
    thumbnail_queue->head = thumbnail_queue->tail = NULL;

    pthread_mutex_destroy(&thumbnail_queue->mutex);
}

#define MINUTE 60
#define CACHED_THUMBNAIL_LIFETIME (MINUTE * 3)

// thumbnails stored seperatley and will be deleted when they expire (n seconds without use)
// useful when preforming similar searches within a smaller time interval 

void configure_youtube_search_query_path(const size_t n, char search_url[n], const Query query)
{
    // get the corresp. sorting param values 
    const char *sort = sort_type_to_url(query.sort);
    const char *media = media_type_to_url(query.media);

    // configure the query path 
    snprintf(search_url, n, "/results?search_query=%s&sp=%s%s", query.encoded_query, sort, media);
}

void configure_query_path(const size_t n, char search_url[n], const SortType sort, const MediaType media, const char *encoded_query)
{
    // get the corresp. sorting param values 
    const char *sort_param = sort_type_to_url(sort);
    const char *media_param = media_type_to_url(media);

    // configure the query path 
    snprintf(search_url, n, "/results?search_query=%s&sp=%s%s", encoded_query, sort_param, media_param);
}

// trims the fat (data not enclosed in object specified by tag) in place
// compatible for both arrays ('[' and ']') and objects ('{' and '}')
int parse_json_object(Buffer *buffer, const char *object, const char opening, const char closing)
{
    char *object_position = strstr(buffer->data, object);
    if (object_position) {
        char *start = strchr(object_position, opening);            
        
        // find the end position, the closing char 
        char *current = start;
        int depth = 0;

        for ( ; current ; current++) {
            if (*current == opening) {
                depth++;
            }

            else if (*current == closing) {
                depth--;
            }

            if (depth == 0) {
                break;
            }
        }

        if (depth == 0) {
            // calculate the number of characters the object data contains
            char *end = current;
            const size_t nchars = end - start + 1; 
            
            // shift the data to the front of the objects memory
            memmove(buffer->data, start, nchars);
            return resize_buffer(buffer, nchars);
        }

        else {
            printf("parse_json_object: the opening %c and closing %c of the json object is unbalanced %d\n", opening, closing, depth);
            return -1;
        }
    }

    printf("parse_json_object: \"%s\" was not found\n", object);
    return -1;
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

// read the header of a http response or n bytes into buffer (whichever comes first)
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
    char *port;
    char *host;
    char path[256];
    char body[1024];
    char header[1024];
} HTTP_Request;

bool header_contains_tag(const char *header, const char *tag)
{
    return strstr(header, tag);
}

size_t get_content_len (const char *header)
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
struct addrinfo *addrinfo = NULL;

// returns the response body of a http request in a Buffer
Buffer send_https_request(const HTTP_Request req)
{
    if (ctx == NULL) {
        ctx = SSL_CTX_new(TLS_client_method());
        if (!ctx) {
            printf("send_https_request: SSL_CTX_new failed\n");
            return (Buffer){0};
        }
    }

    // DNS resolution, looking up the ip address for a website name 
    // res is full of info needed to create a socket
    if (addrinfo == NULL) {
        struct addrinfo desired_addr_info = {0};
        desired_addr_info.ai_family = AF_UNSPEC;
        desired_addr_info.ai_socktype = SOCK_STREAM;
        if (getaddrinfo(req.host, req.port, &desired_addr_info, &addrinfo) != 0) {
            printf("send_https_request: getaddrinfo failed\n");
            addrinfo = NULL;
            return (Buffer){0};
        }
    }

    // initializing socket
    int sockfd = socket(addrinfo->ai_family, addrinfo->ai_socktype, addrinfo->ai_protocol);
    if (sockfd < 0) {
        printf("send_https_request: socket failed\n");
        freeaddrinfo(addrinfo);
        addrinfo = NULL;
        return (Buffer){0};
    }

    // connection between socket and ip address
    if (connect(sockfd, addrinfo->ai_addr, addrinfo->ai_addrlen) != 0) {
        printf("send_https_request: connect failed\n");
        freeaddrinfo(addrinfo);
        addrinfo = NULL;
        close(sockfd);
        return (Buffer){0};
    }

    SSL *ssl = SSL_new(ctx);
    SSL_set_fd(ssl, sockfd);
    if (SSL_connect(ssl) != 1) {
        freeaddrinfo(addrinfo);
        addrinfo = NULL;
        close(sockfd);
        SSL_shutdown(ssl);
        SSL_free(ssl);
        SSL_CTX_free(ctx);
        return (Buffer){0};
    }

    // sending header
    int header_write_status;
    if ((header_write_status = SSL_write(ssl, req.header, strlen(req.header))) <= 0) {
        printf("send_https_request: SSL_write (header) failed\n");
        freeaddrinfo(addrinfo);
        addrinfo = NULL;
        close(sockfd);
        SSL_shutdown(ssl);
        SSL_free(ssl);
        SSL_CTX_free(ctx);
        return (Buffer){0};
    } 

    // send body
    if (req.body[0] != '\0') {
        int body_write_status = SSL_write(ssl, req.body, strlen(req.body));
        if (body_write_status <= 0) {
            printf("send_https_request: SSL_write (body) failed\n");
            freeaddrinfo(addrinfo);
            addrinfo = NULL;
            close(sockfd);
            SSL_shutdown(ssl);
            SSL_free(ssl);
            SSL_CTX_free(ctx);
            return (Buffer){0};
        }
    }

    // extract header from ssl stream
    char header[4096] = {0};
    size_t header_len = read_header(ssl, header, sizeof(header));
    header[header_len] = '\0';
    if (header_len == 0) {
        printf("send_https_request: read_header returned 0\n");
        freeaddrinfo(addrinfo);
        addrinfo = NULL;
        close(sockfd);
        SSL_shutdown(ssl);
        SSL_free(ssl);
        SSL_CTX_free(ctx);
        return (Buffer){0};
    }

    Buffer response = init_buffer();

    // read n bytes into buffer if content length tag is present
    if (header_contains_tag(header, "Content-Length:")) {
        size_t content_length = get_content_len(header);
        if (content_length > 0) {
            ssl_read_n(ssl, &response, content_length);
        }
    }

    else if (header_contains_tag(header, "Transfer-Encoding: chunked")) {
        const char *crlf = "\r\n";
        const size_t crlf_len = strlen(crlf);
        
        int chunk_size = -1; 
        while (chunk_size != 0) {
            // read chunk size line
            char hex[16] = {0};
            int len = ssl_read_line(ssl, hex, sizeof(hex));
            if (len <= 0) {
                printf("send_https_request: failed to read chunk size\n");
                break;
            }

            // parse hex
            if (len >= crlf_len) hex[len - crlf_len] = '\0';
            chunk_size = strtol(hex, NULL, 16);
            
            if (chunk_size > 0) {
                // read chunk_size bytes into response
                ssl_read_n(ssl, &response, chunk_size);
                    
                // absorb trailing CRLF from ssl stream
                char trailing_crlf[16];
                ssl_read_line(ssl, trailing_crlf, sizeof(trailing_crlf));
            }
        }
    }

    close(sockfd);
    SSL_shutdown(ssl);
    SSL_free(ssl);

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

int configure_get_header(const size_t n, char request[n], const char *host, const char *path)
{
    int chars_written = snprintf(request, n,
        "GET %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:125.0) Gecko/20100101 Firefox/125.0\r\n"
        "Connection: close\r\n"
        "\r\n",
        path, host);

    if (chars_written >= n) {
        printf("configure_get_request: buffer is too small (%d bytes needed)\n", chars_written);
        return -1;
    }

    else return chars_written;
}

int configure_post_body(const size_t n, char post_body[n], const char *continuation_token)
{
    size_t chars_written = snprintf(post_body, n,
        "{\n"
        "  \"context\": {\n"
        "    \"client\": {\n"
        "      \"clientName\": \"WEB\",\n"
        "      \"clientVersion\": \"2.20210721.00.00\"\n"
        "    }\n"
        "  },\n"
        "  \"continuation\": \"%s\"\n"
        "}", continuation_token);

    if (chars_written >= n) {
        printf("configure_get_request: buffer is too small (%zu bytes needed)\n", chars_written);
        return -1;
    }

    else return chars_written;
}

int configure_post_header(const size_t n, char request[n], const char *host, const char *path, const size_t post_len)
{
    return snprintf(request, n,
            "POST %s HTTP/1.1\r\n"
            "Host: %s\r\n"
            "User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:125.0) Gecko/20100101 Firefox/125.0\r\n"
            "Content-Type: application/json\r\n"
            "Content-Length: %zu\r\n"
            "Connection: close\r\n"
            "\r\n",
            path, host, post_len);
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

void create_search_node_from_json(SearchResult *search_result, cJSON *item, const bool allow_shorts)
{
    search_result->media_type = UNDF;
    search_result->thumbnail = (Texture){0};
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

    // the item (the nth element of 'contents' json obj) is either a video, channel, or playlist
    // thus, only one of the values will not NULL
    cJSON *channelRenderer = cJSON_GetObjectItem(item, "channelRenderer");
    cJSON *videoRenderer = cJSON_GetObjectItem(item, "videoRenderer");
    cJSON *lockupViewModel = cJSON_GetObjectItem(item, "lockupViewModel");
    
    if (videoRenderer) {
        // check if the video is a short, i fucking hate yt shorts...
        if (video_is_youtube_short(videoRenderer) && !allow_shorts) {
            return;
        }

        // id
        cJSON *videoId = cJSON_GetObjectItem(videoRenderer, "videoId");
        if (!videoId || !videoId->valuestring) {
            search_result->media_type = UNDF;
            return;
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
            search_result->media_type = UNDF;
            return;
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
            search_result->media_type = UNDF;
            return;
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
}

#define MAX_THREADS 4

typedef struct 
{
    char search_result_id[64];
    HTTP_Request http_request;
    ThumbnailQueue *thumbnail_queue;
} LoadThumbnailThreadArgs;

void* load_thumbnail(void *args)
{
    LoadThumbnailThreadArgs *targs = (LoadThumbnailThreadArgs*) args;
    
    Buffer thumbnail_buffer = send_https_request(targs->http_request);
    if (!buffer_ready(&thumbnail_buffer)) {
        printf("load_thumbnail: send_http_request returned invalid buffer\n");
        free(targs);
        return NULL;
    }

    // create thumbnail data node
    ThumbnailData *thumbnail_data = malloc(sizeof(ThumbnailData));
    if (!thumbnail_data) {
        printf("load_thumbnail: malloc returned NULL for thumbnail_data\n");
        free(targs);
        return NULL;
    }

    thumbnail_data->image_data = thumbnail_buffer;
    strcpy(thumbnail_data->search_result_id, targs->search_result_id);

    // add node to queue
    pthread_mutex_lock(&targs->thumbnail_queue->mutex);
    enqueue_thumbnail(targs->thumbnail_queue, thumbnail_data);
    pthread_mutex_unlock(&targs->thumbnail_queue->mutex);

    free(targs);
    return NULL;
}

static char next_page_token[1024] = {0};
void extract_continuation_token(const cJSON *continuationItemRenderer)
{
    cJSON *continuationEndpoint = continuationItemRenderer ? cJSON_GetObjectItem(continuationItemRenderer, "continuationEndpoint") : NULL;
    cJSON *continuationCommand = continuationEndpoint ? cJSON_GetObjectItem(continuationEndpoint, "continuationCommand") : NULL;
    cJSON *token = continuationCommand ? cJSON_GetObjectItem(continuationCommand, "token") : NULL;
    if (token && cJSON_IsString(token)) 
        strncpy(next_page_token, token->valuestring, sizeof(next_page_token) - 1);

    else {
        printf("extract_continuation_token: token not found\n");
        memset(next_page_token, 0, sizeof(next_page_token));
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
    while (queue->head) 
        free(dequeue_task(queue));

    pthread_mutex_destroy(&queue->mutex);
    pthread_cond_destroy(&queue->cond);
}

bool application_running = true;
void* worker_thread_funct(void* args)
{
    const long int id = pthread_self();

    while (application_running) {
        pthread_mutex_lock(&task_queue.mutex);
        while (task_queue.count == 0 && application_running) 
            pthread_cond_wait(&task_queue.cond, &task_queue.mutex);

        if (!application_running) {
            pthread_mutex_unlock(&task_queue.mutex);
            break;
        }

        ThreadTask *task = dequeue_task(&task_queue);

        pthread_mutex_unlock(&task_queue.mutex);  // Release lock while processing
        // printf("thread %lX is preforming function\n", id);
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

// the arguemnts needed for the thread function 'get_results_from_query'
typedef struct
{
    bool allow_youtube_shorts;
    SearchType search_type;
    HTTP_Request http_request;
    Results *search_results;
    ThumbnailQueue *thumbnail_queue;
} SearchThreadArgs;

#define MAX_SEARCH_ITEMS 100

static int elements_added = 0; 
static bool delete_old_nodes = false;
static bool search_finished = true;
void* get_results_from_query(void* args)
{
    SearchThreadArgs* targs = (SearchThreadArgs*)args;
    elements_added = 0;
    clock_t start_time = clock(); 

    // get the information of the http request
    Buffer http = send_https_request(targs->http_request);
    bool application_is_offline = (buffer_ready(&http) == false);
    if (application_is_offline) {
        printf("get_results_from_query: send_https_request returned invalid buffer\n");
        SetWindowTitle("[offline] - metube");
        free(targs);
        search_finished = true;
        return NULL;
    }
    
    // only keep data that is found in the json object 'sectionListRenderer'
    if (targs->search_type == NEW) {
        if (parse_json_object(&http, "sectionListRenderer", '{', '}') < 0) {
            printf("get_results_from_query: parse_json_object corrupted data of passed buffer\n");
            free_buffer(&http);
            free(targs);
            search_finished = true;
            return NULL;
        }
    }

    else if (targs->search_type == APPENDING) {
        if (parse_json_object(&http, "continuationItems", '[', ']') < 0) {
            printf("get_results_from_query: parse_json_object corrupted data of passed buffer\n");
            free_buffer(&http);
            free(targs);
            search_finished = true;
            return NULL;
        }
    }

    // get json obj
    cJSON* sectionListRenderer = cJSON_Parse(http.data);
    if (!sectionListRenderer) {
        printf("get_results_from_query: cJSON_Parse returned NULL\n");
        free_buffer(&http);
        free(targs);
        search_finished = true;
        return NULL;
    }

    cJSON *sectionListRendererContents = NULL;
    cJSON *contents = NULL;
    if (targs->search_type == NEW) {
        sectionListRendererContents = cJSON_GetObjectItem(sectionListRenderer, "contents");
        cJSON *first_content = cJSON_GetArrayItem(sectionListRendererContents, 0);
        cJSON *itemSectionRenderer = first_content ? cJSON_GetObjectItem(first_content, "itemSectionRenderer") : NULL;
        contents = itemSectionRenderer ? cJSON_GetObjectItem(itemSectionRenderer, "contents") : NULL;
    }

    else if (targs->search_type == APPENDING) {
        cJSON *first_element = cJSON_GetArrayItem(sectionListRenderer, 0);
        cJSON *itemSectionRenderer = first_element ? cJSON_GetObjectItem(first_element, "itemSectionRenderer") : NULL;
        contents = itemSectionRenderer ? cJSON_GetObjectItem(itemSectionRenderer, "contents") : NULL;
    }
    
    if (contents && cJSON_IsArray(contents)) {
        // loop through every item and get the node equivalent 
        cJSON *item;
        cJSON_ArrayForEach (item, contents) {
            if ((targs->search_results->count < MAX_SEARCH_ITEMS) || (targs->search_type == NEW)) {
                SearchResult *search_result = (SearchResult*) malloc(sizeof(SearchResult));
                if (!search_result) {
                    printf("get_results_from_query: malloc returned NULL for search_result\n");
                    cJSON_Delete(sectionListRenderer);
                    free_buffer(&http);
                    free(targs);
                    search_finished = true;
                    return NULL;
                }

                create_search_node_from_json(search_result, item, targs->allow_youtube_shorts);
                if (search_result->media_type != UNDF) {
                    add_search_result(targs->search_results, search_result);
                    elements_added++;
                    LoadThumbnailThreadArgs *thumbnailargs = malloc(sizeof(LoadThumbnailThreadArgs));
                    if (thumbnailargs) {
                        HTTP_Request http_req = {0};
                        http_req.port = "443";
                        http_req.host = media_type_to_host(search_result->media_type);
                        strcpy(http_req.path, search_result->thumbnail_path);
                        configure_get_header(sizeof(http_req.header), http_req.header, http_req.host, http_req.path);

                        // configure the thread arguements to load thumbnail
                        thumbnailargs->http_request = http_req;
                        strcpy(thumbnailargs->search_result_id, search_result->id);
                        thumbnailargs->thumbnail_queue = targs->thumbnail_queue;

                        ThreadTask *async_thumbnail_load = malloc(sizeof(ThreadTask));
                        (*async_thumbnail_load) = (ThreadTask) {
                            .next = NULL,
                            .args = thumbnailargs,
                            .funct = load_thumbnail,
                        };

                        pthread_mutex_lock(&task_queue.mutex);
                            enqueue_task(async_thumbnail_load, &task_queue);
                            pthread_cond_signal(&task_queue.cond);
                        pthread_mutex_unlock(&task_queue.mutex);
                    }
                }
                else 
                    free_search_result(search_result);
            }
        }
    }

    // getting the next page token    
    if (targs->search_type == NEW) {
        cJSON *secondsectionListRendererObject = cJSON_GetArrayItem(sectionListRendererContents, 1);
        cJSON *continuationItemRenderer = secondsectionListRendererObject ? cJSON_GetObjectItem(secondsectionListRendererObject, "continuationItemRenderer") :NULL;
        extract_continuation_token(continuationItemRenderer);
    }

    else if (targs->search_type == APPENDING) {
        cJSON *second_parent_item = cJSON_GetArrayItem(sectionListRenderer, 1);
        cJSON *continuationItemRenderer = second_parent_item ? cJSON_GetObjectItem(second_parent_item, "continuationItemRenderer") : NULL;
        extract_continuation_token(continuationItemRenderer);
    }

    clock_t end_time = clock();

    delete_old_nodes = targs->search_type == NEW;
    search_finished = true;

    if (targs->search_type == NEW)
        SetWindowTitle(TextFormat("[search results(%d)] - metube", elements_added));
    else if (targs->search_type == APPENDING)
        SetWindowTitle(TextFormat("[search results(%d)] - metube", targs->search_results->count));
    
    printf("search took %f seconds, found %d items\n", ((end_time - start_time) / (CLOCKS_PER_SEC * 1.0f)) * 10, elements_added);
    
    // deinit
    cJSON_Delete(sectionListRenderer);
    free_buffer(&http);
    free(targs);

    return NULL;
}

void init_app()
{
    // init app
    SetTargetFPS(60);
    SetTraceLogLevel(LOG_ERROR);
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    SetConfigFlags(FLAG_WINDOW_ALWAYS_RUN);
    InitWindow(1000, 750, "metube");
}

Texture load_thumbnail_from_memory(const Buffer buffer, const float width, const float height)
{
    if (buffer_ready(&buffer)) {
        Image image = LoadImageFromMemory(".jpeg", (unsigned char*) buffer.data, buffer.size);
        if (IsImageReady(image)) {
            ImageResize(&image, width, height);
            Texture2D ret = LoadTextureFromImage(image);
            UnloadImage(image);
            return ret;
        }
        else 
            printf("load_thumbnail_from_memory: failed to load image data\n");
    }
    else
        printf("load_thumbnail_from_memory: buffer arg is invalid\n");
    
    return (Texture){0};
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

void process_async_loaded_thumbnails(ThumbnailQueue *thumbnail_queue, Results *results)
{
    pthread_mutex_lock(&thumbnail_queue->mutex);
    while (thumbnail_queue->head) {
        ThumbnailData *thumbnail_data = dequeue_thumbnail(thumbnail_queue);
        
        // find matching search node and load texture
        for (SearchResult *search_node = results->head; search_node; search_node = search_node->next) {
            if (strcmp(thumbnail_data->search_result_id, search_node->id) == 0) {
                // clear thumbnail
                if (IsTextureReady(search_node->thumbnail))
                    UnloadTexture(search_node->thumbnail);
                
                // add texture to cache
                search_node->thumbnail = load_thumbnail_from_memory(thumbnail_data->image_data, 160, 80);
                if (!IsTextureReady(search_node->thumbnail)) {
                    printf("%s failed to load texture\n", search_node->id);
                }
                break;
            }
        }

        // remove processed thumbnail data
        free_thumbnail_data(thumbnail_data);
    }
    pthread_mutex_unlock(&thumbnail_queue->mutex);
}


int main()
{
    Results results = init_results();
    ThumbnailQueue thumbnail_queue = init_thumbnail_queue();
    
    // TaskQueue task_queue = init_task_queue();
    task_queue = init_task_queue();
    pthread_t thread_pool[MAX_THREADS];
    init_thread_pool(MAX_THREADS, thread_pool, worker_thread_funct, &task_queue);
    
    // when true, the application starts the search process
    bool search = false;
    char search_buffer[256] = {0};

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
        process_async_loaded_thumbnails(&thumbnail_queue, &results);

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
                printf("query: \"%s\"\n", query.encoded_query);
                SetWindowTitle(TextFormat("[%s(loading)] - metube", search_buffer));

                HTTP_Request http_request = {0};
                http_request.host = "www.youtube.com";
                http_request.port = "443";

                if (search_type == NEW) {
                    configure_youtube_search_query_path(sizeof(http_request.path), http_request.path, query);
                    configure_get_header(sizeof(http_request.header), http_request.header, http_request.host, http_request.path);
                }

                else if (search_type == APPENDING) {
                    strcpy(http_request.path, "/youtubei/v1/search");
                    configure_post_body(sizeof(http_request.body), http_request.body, next_page_token);
                    configure_post_header(sizeof(http_request.header), http_request.header, http_request.host, http_request.path, strlen(http_request.body));
                }

                targs->search_type = search_type;
                targs->allow_youtube_shorts = query.allow_youtube_shorts;
                targs->search_results = &results;
                targs->thumbnail_queue = &thumbnail_queue;
                targs->http_request = http_request;
                
                // awaken a worker thread to handle 'get_results_from_query' function
                ThreadTask *search_task = malloc(sizeof(ThreadTask));
                if (!search_task) {
                    printf("main: malloc returned NULL for ThreadTask object\n");
                    free(targs);
                } 
                else {
                    (*search_task) = (ThreadTask) {
                        .next = NULL,
                        .args = targs,
                        .funct = get_results_from_query,
                    };
                    
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
                .width = 375, 
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
            if ((text_box_status = GuiTextBox(search_bar_bounds, search_buffer, sizeof(search_buffer), edit_mode))) {
                edit_mode = !edit_mode;
            }

            bool enter_key_pressed = text_box_status == 2;

            if (GuiButton(search_button_bounds, "Search") || enter_key_pressed) {
                // sanitize query
                remove_leading_whitespace(search_buffer);
                remove_trailing_whitespace(search_buffer);

                // load url encoded string into query 
                if (search_buffer[0] != '\0') {
                    if(query.encoded_query) {
                        free(query.encoded_query);
                        query.encoded_query = NULL;
                    }

                    query.encoded_query = url_encode_string(search_buffer);
                    
                    if (!query.encoded_query) 
                        printf("main: url_encode_string returned NULL\n");
                    else {
                        search = search_finished;
                        search_type = NEW;
                    }
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
            const Rectangle scroll_window_bounds = { 
                .x = search_bar_bounds.x, 
                .y = search_bar_bounds.y + search_bar_bounds.height + (show_filter_window ? (ui.padding + filter_window_bounds.height) : 0) + ui.padding, 
                .width = search_bar_bounds.width, 
                .height = GetScreenHeight() - scroll_window_bounds.y - ui.padding, 
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
            const int SCROLLBAR_WIDTH = vertical_scrollbar_visible ? 13 : 0;

            bool scrollbar_out_of_bounds = GuiScrollPanel(scroll_window_bounds, NULL, content_area, &scroll, &scrollView);
            if (scrollbar_out_of_bounds && query.encoded_query && query.encoded_query[0] != '\0' && next_page_token[0] != '\0') {
                search_type = APPENDING;
                search = search_finished && results.count < MAX_SEARCH_ITEMS;
            }

            const Rectangle scissor_rect = padded_rectangle(1, scroll_window_bounds);
            
            BeginScissorMode(scissor_rect.x, scissor_rect.y, scissor_rect.width, scissor_rect.height);
                // the y value of the ith rectangle to be drawn
                float y_level = scissor_rect.y;
                
                // for every search result, draw a container and display its data
                int i = 0;
                for (SearchResult *search_result = results.head; search_result; search_result = search_result->next, i++, y_level += content_height) {
                    // area of the ith rectangle
                    Rectangle content_rect = { 
                        .x = ui.padding, 
                        .y = y_level + scroll.y, // scroll is added so moving the scrollbar offsets all elements
                        .width = scissor_rect.width - SCROLLBAR_WIDTH,
                        .height = content_height 
                    };

                    // only process items that are onscreen
                    if (CheckCollisionRecs(content_rect, scissor_rect)) {
                        const Color background_color = (i % 2) ? WHITE : RAYWHITE;
                        DrawRectangleRec(content_rect, background_color);
                        
                        const Rectangle thumbnail_bounds = { 
                            .x = content_rect.x, 
                            .y = content_rect.y, 
                            .width = content_rect.width * 0.45f, 
                            .height = content_rect.height 
                        };
                        
                        if (IsTextureReady(search_result->thumbnail)) 
                            DrawTextureEx(search_result->thumbnail, (Vector2){ thumbnail_bounds.x, thumbnail_bounds.y }, 0.0f, 1.0f, RAYWHITE);
                        const Rectangle title_bounds = {
                            thumbnail_bounds.x + thumbnail_bounds.width,
                            content_rect.y,
                            content_rect.width - thumbnail_bounds.width,
                            content_rect.height * 0.70f
                        };

                        DrawTextBoxed(search_result->title, padded_rectangle(ui.padding, title_bounds), ui, 12, BLACK);                            

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
                }
            EndScissorMode();
        //---------------------------------------------------------------displaying UI--------------------------------------------------------------------------//
        EndDrawing();
    }

    // deinit app
    UnloadFont(ui.font);
    free_results(&results);
    free_thumbnail_queue(&thumbnail_queue);
    if (query.encoded_query) free(query.encoded_query);
    
    // ssl stuff
    if (ctx) SSL_CTX_free(ctx);
    if (addrinfo) freeaddrinfo(addrinfo);
    
    // free worker thread stuff
    application_running = false;
    pthread_cond_broadcast(&task_queue.cond);
    free_thread_pool(MAX_THREADS, thread_pool);
    free_task_queue(&task_queue);         
    
    CloseWindow();
    return 0;
}

// fix worker thread, crashes on fast load and missing images

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
    // cached thumbnails into hashmap
    // persistent socket connection, have code for this
    // fonts for L.O.T.E.
    // handle cleanup when prematurley deleting
    // thumbnail data list
    // search arguements

