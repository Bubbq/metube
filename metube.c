#include <math.h>
#include <time.h>
#include <ctype.h>
#include <netdb.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <stdbool.h>
#include <sys/socket.h>

#include "uthash.h"
#include "raylib.h"
#include "arpa/inet.h"
#include "cjson/cJSON.h"
#include "openssl/ssl.h"

#define RAYGUI_IMPLEMENTATION
#include "raygui.h"

#define MINUTE 60
#define CACHED_TEXTURE_LIFETIME (MINUTE * 1)

#define MAX_THREADS 4
#define N_CONN MAX_THREADS

#define LIKED_VIDEOS_FILE "liked_videos.json"
#define LIKED_VIDEOS_ARRAY "videos"
#define LIKED_VIDEO_ID_PATH ".likedVideoRenderer.id"
#define MAX_LIKED_VIDEOS 20

#define SUBSCRIPTIONS_FILE "subscriptions.json"
#define SUBSCRIBED_CHANNELS_ARRAY "channels"
#define SUBSCRIBED_CHANNEL_ID_PATH ".subscribedChannelRenderer.id"
#define MAX_SUBSCRIBED_CHANNELS 20

#define WATCH_HISTORY_FILE "watch_history.json"
#define WATCH_HISTORY_ARRAY "history"
#define WATCHED_VIDEO_ID_PATH ".watchedVideoRenderer.id"
#define MAX_HISTORY_LEN 20

#define MEDIUM_THUMBNAIL_VIDEO_RESOLUTION "mqdefault"

#define VALID_HTTPS_RESPONSE_CODE "200"
#define CONTENT_LENGTH_HEADER_TAG "Content-Length:"
#define TRANSFER_ENCODING_HEADER_TAG "Transfer-Encoding:"
#define CHUNKED_ENCODING "chunked"

#define HTTPS_PORT "443"
#define CLIENT_NAME "WEB"
#define CLIENT_VER "2.20250730"
#define YT_API_PLAYLIST_BROWSE_ID_PREFIX "VL"    
#define YT_API_TRENDING_BROWSE_ID "FEtrending" 
#define YT_API_CHANNEL_VIDEOS_PARAMS "EgZ2aWRlb3PyBgQKAjoA"  
#define USER_AGENT "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/130.0.0.0 Safari/537.36"

static char* continuation_token = NULL;

int bound_index_to_array (const int pos, const int array_size)
{
    return (pos + array_size) % array_size;
}

bool connected_to_internet()
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

bool file_exists(const char* filename)
{
    FILE* fp = fopen(filename, "r+");
    if (fp) {
        fclose(fp);
        return true;
    }

    else return false;
}

size_t file_len_from_current_pos(FILE* fp)
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

    const long len = file_len_from_current_pos(fp);
    if (len == 0) {
        printf("get_file_content: file_len_from_current_pos returned 0\n");
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

bool create_file(const char* filename, const char* buffer)
{
    if ((filename == NULL) || (buffer == NULL)) return false;

    FILE* fp = fopen(filename, "w");
    if (fp == NULL) {
        printf("create_file: 'fp' is null\n");
        return false;
    }

    const size_t len = strlen(buffer);

    const size_t written = fwrite(buffer, sizeof(char), len, fp);

    fclose(fp); fp = NULL;

    if (written != len) {
        printf("create_file: (%zu/%zu) chars written\n", written, len);
        return false;
    }

    return true;
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

typedef struct
{
	double start_time;
	double duration; // in seconds
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
	timer->duration = lifetime;
}

bool timer_done(Timer timer)
{ 
    const double elapsed = GetTime() - timer.start_time;
	return elapsed >= timer.duration; 
} 

typedef struct
{
    size_t size;
    char* data;
} Buffer;

Buffer buffer_init()
{
    Buffer buffer;
    buffer.data = NULL;
    buffer.size = 0;
    return buffer;
}

void buffer_write_data(Buffer *buffer, const char* data, const size_t data_size)
{
    const size_t new_size = buffer->size + data_size + 1;
    
    char *new_data = realloc(buffer->data, new_size);
    if (!new_data) {
        printf("buffer_write_data: failed to reallocate %zu bytes\n", new_size);
        return;
    }

    buffer->data = new_data;
    memcpy(&buffer->data[buffer->size], data, data_size);
    buffer->size += data_size;
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

void buffer_free(Buffer *buffer)
{
    if (buffer == NULL) return;
    if (buffer->data) free(buffer->data);
    buffer->data = NULL;
    buffer->size = 0;
}

typedef enum
{
    MEDIA_TYPE_ANY,
    MEDIA_TYPE_VIDEO,
    MEDIA_TYPE_CHANNEL,
    MEDIA_TYPE_PLAYLIST,
    MEDIA_TYPE_LIVE,
    MEDIA_TYPE_SHORT,
    MEDIA_TYPE_UNDF,
} MediaType;

#define N_MEDIA_TYPES 5

const char* media_type_to_search_param(MediaType media_type)
{
    switch (media_type) {
        case MEDIA_TYPE_SHORT:
        case MEDIA_TYPE_VIDEO: return "SAhAB";
        case MEDIA_TYPE_CHANNEL: return "SAhAC";
        case MEDIA_TYPE_PLAYLIST: return "SAhAD";
        case MEDIA_TYPE_LIVE: return "SBBABQAE";
        case MEDIA_TYPE_ANY: return "%253D";
        default:
            printf("media_type_to_search_param: invalid media_type passed\n");
            return NULL;
    }
}

char* media_type_to_thumbnail_host(const MediaType media_type)
{
    switch (media_type) {
        case MEDIA_TYPE_LIVE:
        case MEDIA_TYPE_SHORT:
        case MEDIA_TYPE_VIDEO: 
        case MEDIA_TYPE_PLAYLIST: return "i.ytimg.com";
        case MEDIA_TYPE_CHANNEL: return "yt3.ggpht.com";
        default:
            printf("media_type_to_thumbnail_host: invalid media_type passed\n");
            return NULL;
    }
}

char* media_type_to_text(const MediaType media_type)
{
    switch (media_type) {
        case MEDIA_TYPE_VIDEO: return "VIDEO";
        case MEDIA_TYPE_CHANNEL: return "CHANNEL";
        case MEDIA_TYPE_PLAYLIST: return "PLAYLIST";
        case MEDIA_TYPE_LIVE: return "LIVE";
        case MEDIA_TYPE_ANY: return "ANY";
        case MEDIA_TYPE_UNDF: return "UNDF";
        case MEDIA_TYPE_SHORT: return "SHORT";
        default:
            printf("media_type_to_text: invalid media_type passed\n");
            return NULL;
    }
}

const Vector2 media_type_to_thumbnail_dim(const MediaType media_type)
{
    switch (media_type) {
        case MEDIA_TYPE_LIVE:
        case MEDIA_TYPE_SHORT:
        case MEDIA_TYPE_VIDEO:
        case MEDIA_TYPE_PLAYLIST: return (Vector2) { 150, 80 };
        case MEDIA_TYPE_CHANNEL:  return (Vector2) { 75, 70 };
        case MEDIA_TYPE_UNDF:
        case MEDIA_TYPE_ANY:      return (Vector2) { 0 }; 
    }
}

// availible sorting types youtube provides 
typedef enum 
{
    SORT_TYPE_RELEVANCE,
    SORT_TYPE_UPLOAD_DATE,
    SORT_TYPE_VIEW_COUNT,
    SORT_TYPE_RATING,
} SortType; 
#define N_SORT_TYPES 4

char* sort_type_to_search_param(const SortType sort_type)
{
    switch (sort_type) {
        case SORT_TYPE_RELEVANCE: return "CAA";
        case SORT_TYPE_UPLOAD_DATE: return "CAI";
        case SORT_TYPE_VIEW_COUNT: return "CAM";
        case SORT_TYPE_RATING: return "CAE";
    }

    printf("sort_type_to_search_param: invalid sort_type passed\n");
    return NULL;
}

char* sort_type_to_text(const SortType sort_type)
{
    switch (sort_type) {
        case SORT_TYPE_RELEVANCE: return "Relevence";
        case SORT_TYPE_UPLOAD_DATE: return "Upload Date";
        case SORT_TYPE_VIEW_COUNT: return "Views"; 
        case SORT_TYPE_RATING: return "Rating";
        default:
            printf("sort_type_to_text: passed SortType is invalid\n");
            return NULL;
    }
}

typedef struct
{
    char id [64];
    UT_hash_handle hh;
    Texture2D thumbnail;
    Timer timer;
} TextureCacheEntry;

TextureCacheEntry* cached_texture_init(const Texture2D texture, const char* id)
{
    if ((id == NULL) || (id[0] == '\0') || (IsTextureReady(texture) == false)) {
        printf("cached_texture_init: invalid args\n");
        return NULL;
    }
    
    TextureCacheEntry* cached_thumbnail = (TextureCacheEntry*) malloc(sizeof(TextureCacheEntry));
    if (cached_thumbnail == NULL) {
        printf("cached_texture_init: malloc returned NULL\n");
        return NULL;
    }

    start_timer(&cached_thumbnail->timer, CACHED_TEXTURE_LIFETIME);

    cached_thumbnail->thumbnail = texture;
    strncpy(cached_thumbnail->id, id, sizeof(cached_thumbnail->id) - 1);
    cached_thumbnail->id[sizeof(cached_thumbnail->id) - 1] = '\0';

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

typedef struct SearchResult
{
    char thumbnail_path[256];    
    char title[256];                    
    char authorId[64];         
    char id[64];                                
    char subscriber_count[32];  
    char date_published[32];      
    char video_count[32];            
    char view_count[16];              
    char duration[16];                   
    struct SearchResult* next; 
    MediaType media_type;        
    bool thumbnail_loaded;
} SearchResult;

SearchResult* search_result_init()
{
    SearchResult* search_result = calloc(1, sizeof(SearchResult));
    if (search_result == NULL) return NULL;
    search_result->media_type = MEDIA_TYPE_UNDF;
    search_result->thumbnail_loaded = false;
    return search_result;
}

void search_result_free(SearchResult *search_result)
{
    if (!search_result) return;
    free(search_result);
}

void print_search_result(const SearchResult *search_result) 
{
    printf("id) %s title) %s author) %s subs) %s views) %s date) %s length) %s video count) %s type) %d thumbnail) %s\n", 
            search_result->id, search_result->title, search_result->authorId, search_result->subscriber_count, search_result->view_count, search_result->date_published, search_result->duration, search_result->video_count, search_result->media_type, search_result->thumbnail_path);
}

typedef struct RawThumbnail
{
    char id[256];     
    Buffer data;              
    struct RawThumbnail *next;
    MediaType media_type;
} RawThumbnail;

RawThumbnail* raw_thumbnail_init()
{
    RawThumbnail* raw_thumbnail = malloc(sizeof(RawThumbnail));
    if (raw_thumbnail == NULL) return NULL;

    raw_thumbnail->next = NULL;
    raw_thumbnail->data = buffer_init();
    raw_thumbnail->media_type = MEDIA_TYPE_UNDF;
    memset(raw_thumbnail->id, 0, sizeof(raw_thumbnail->id));

    return raw_thumbnail;
}

void raw_thumbnail_free(RawThumbnail* raw_thumbnail)
{
    if (raw_thumbnail == NULL) return;
    if (buffer_ready(&raw_thumbnail->data)) buffer_free(&raw_thumbnail->data);
    free(raw_thumbnail); raw_thumbnail = NULL;
}

typedef struct ThreadTask
{
    void *(*funct)(void *);
    void *args;
    struct ThreadTask *next;
} ThreadTask;

ThreadTask* thread_task_init()
{
    return calloc(1, sizeof(ThreadTask));
}

void thread_task_free(ThreadTask* task)
{
    if (task == NULL) return;
    if (task->args) {
        free(task->args); task->args = NULL;
    }
    free(task); task = NULL;
}

typedef enum
{
    NODE_TYPE_SEACH_RESULT,
    NODE_TYPE_RAW_THUMBNAIL,
    NODE_TYPE_THREAD_TASK,
    NODE_TYPE_UNDF,
} NodeType;

typedef struct Node 
{
    void* content;
    struct Node* next;
    NodeType type;
} Node;

Node* node_init(const NodeType node_type)
{
    Node* node = malloc(sizeof(Node));
    if (node == NULL) {
        printf("node_init: malloc returned NULL\n");
        return NULL;
    }

    node->next = NULL;
    node->type = node_type;
    switch (node->type) {
        case NODE_TYPE_SEACH_RESULT:  node->content = search_result_init(); break;
        case NODE_TYPE_RAW_THUMBNAIL: node->content = raw_thumbnail_init(); break;
        case NODE_TYPE_THREAD_TASK:   node->content = thread_task_init(); break;
        case NODE_TYPE_UNDF:          node->content = NULL; break;
    }

    return node;
}

void node_free(Node* node)
{
    if (node == NULL) return;

    switch (node->type) {
        case NODE_TYPE_SEACH_RESULT:  search_result_free(node->content); break;
        case NODE_TYPE_RAW_THUMBNAIL: raw_thumbnail_free(node->content); break;
        case NODE_TYPE_THREAD_TASK:   thread_task_free(node->content); break;
        case NODE_TYPE_UNDF:
        break;
    }

    free(node); node = NULL;
}

typedef struct
{
    pthread_cond_t cond;
    pthread_mutex_t mutex;
    Node* head; 
    Node* tail; 
    size_t count;
} List;

List list_init()
{
    List list;
    list.count = 0;
    list.head = list.tail = NULL;
    pthread_cond_init(&list.cond, NULL);
    pthread_mutex_init(&list.mutex, NULL);
    return list;
}

void list_append(List* list, Node* node)
{
    if ((list == NULL) || (node == NULL)) return;

    node->next = NULL;

    if (list->head == NULL) {
        list->head = list->tail = node;
    }

    else {
        list->tail->next = node;
        list->tail = node;
    }

    list->count++;
} 

Node* list_dequeue(List* list)
{
    if (list == NULL) return NULL;

    Node* detached = list->head;

    list->head = list->head->next;
    if (list->head == NULL) {
        list->tail = NULL;
    }

    list->count--;
    
    return detached;
}

void list_free(List* list)
{
    if (list == NULL) return;
    while (list->head && list->count != 0) node_free(list_dequeue(list));
    list->count = 0;
    list->head = list->tail = NULL;
    pthread_cond_destroy(&list->cond);
    pthread_mutex_destroy(&list->mutex);
}

void list_print(List* list)
{
    if (list == NULL) return;
    
    for (Node* current = list->head; current; current = current->next) {
        switch (current->type) {
            case NODE_TYPE_SEACH_RESULT: print_search_result(current->content); break;
            case NODE_TYPE_RAW_THUMBNAIL:
            case NODE_TYPE_THREAD_TASK:
            case NODE_TYPE_UNDF:
                break;
        }
    }
}

typedef enum
{
    QUERY_TYPE_USER_INPUT,  
    QUERY_TYPE_RELATED,  
    QUERY_TYPE_TRENDING, 
    QUERY_TYPE_VIDEO_FOCUS,
    QUERY_TYPE_VIEW_PLAYLIST,
    QUERY_TYPE_VIEW_CHANNEL,
    QUERY_TYPE_WATCH_HISTORY,
    QUERY_TYPE_VIEW_SUBSCRIBED_CHANNELS,
    QUERY_TYPE_VIEW_LIKED_VIDEOS,
} QueryType;

typedef enum
{
    QUERY_ATTR_REPLACE,
    QUERTY_ATTR_APPEND,
} QueryAttribute;

const char* query_type_to_endpoint(const QueryType search_type)
{
    switch (search_type) {
        case QUERY_TYPE_USER_INPUT: return "search";
        case QUERY_TYPE_VIEW_CHANNEL:
        case QUERY_TYPE_VIEW_PLAYLIST:
        case QUERY_TYPE_TRENDING: return "browse";
        case QUERY_TYPE_VIDEO_FOCUS: return "player";
        case QUERY_TYPE_RELATED: return "next";
        default:    
            printf("query_type_to_endpoint: invalid type passed\n");
            return NULL;
    }
}

// represents user-defined parameters for a YouTube search request
typedef struct
{
    char string[256];        
    char focused_id[64];     
    QueryType type;
    QueryAttribute attr;    
    MediaType media;          
    SortType sort;
    bool allow_youtube_shorts; 
} Query;

typedef struct
{
    char header[512];
    char path[256];
    char* payload;
} HttpsRequest;

bool configure_api_path(char* dest, const size_t dest_size, QueryType query_type, const char* key)
{
    const char* endpoint = query_type_to_endpoint(query_type);

    if ((endpoint == NULL) || (key == NULL) || (dest == NULL)) return false;

    const size_t written = snprintf(dest, dest_size, "/youtubei/v1/%s?key=%s", endpoint, key);

    return (written > 0) && (written < dest_size);
}

bool configure_get_header(char* dest, const size_t dest_size, const char* host, const char* path)
{
    if ((dest == NULL) || (host == NULL) || (path == NULL)) return false;

    const size_t len =  snprintf(dest, dest_size,
                "GET %s HTTP/1.1\r\n"
                        "Host: %s\r\n"
                        "User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:125.0) Gecko/20100101 Firefox/125.0\r\n"
                        "Connection: keep-alive\r\n"
                        "\r\n",
                        path, host);
    
    return (len > 0) && (len < dest_size);
}

bool configure_post_header(char* dest, const size_t dest_size, const char *host, const char *path, const size_t post_body_length)
{
    const size_t len = snprintf(dest, dest_size,
                        "POST %s HTTP/1.1\r\n"
                        "Host: %s\r\n"
                        "User-Agent: " USER_AGENT "\r\n"
                        "Content-Type: application/json\r\n"
                        "Accept: application/json\r\n"
                        "Content-Length: %zu\r\n"
                        "Connection: keep-alive\r\n"
                        "\r\n",
                        path, host, post_body_length);
    
    return (len > 0) && (len < dest_size);
}

bool valid_post_request(const HttpsRequest post)
{
    return (post.path[0] != '\0') && (post.header[0] != '\0') && (post.payload) && (post.payload[0] != '\0');
}

bool add_queried_search_payload(cJSON* root, const Query* q)
{
    if ((root == NULL) || (q == NULL) || (q->string[0] == '\0')) {
        printf("add_queried_search_payload: invalid input\n");
        return false;
    }

    const char* sort_url = sort_type_to_search_param(q->sort);
    const char* media_url = media_type_to_search_param(q->media);

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

    if (cJSON_AddStringToObject(root, "browseId", YT_API_TRENDING_BROWSE_ID) == NULL) {
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

    if (cJSON_AddStringToObject(root, "continuation", continuation_token) == false) {
        printf("add_continuation_token: failed to add 'continuation'\n");
        return false;
    }

    return true;
}

cJSON* configure_base_request_payload()
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

cJSON* configure_payload(const Query* query)
{
    if (query == NULL) return NULL;

    cJSON* root = configure_base_request_payload();
    if (root == NULL) {
        printf("configure_payload: failed to create base payload\n");
        return NULL;
    }

    bool (*payload_funct)(cJSON*,const Query*) = NULL;

    if (query->attr == QUERTY_ATTR_APPEND)  {
        if (continuation_token && continuation_token[0] != '\0') {
            if (add_continuation_payload(root, continuation_token) == false) {
                cJSON_Delete(root); root = NULL;
            }
        }

        else {
            printf("configure_payload: no continuation token for payload\n");
            cJSON_Delete(root); root = NULL;
        }

        return root;
    }

    else if (query->attr == QUERY_ATTR_REPLACE) {
        switch (query->type) {
            case QUERY_TYPE_RELATED:    
            case QUERY_TYPE_VIDEO_FOCUS:   payload_funct = add_view_related_videos_payload;  break;
            case QUERY_TYPE_USER_INPUT:    payload_funct = add_queried_search_payload;       break;
            case QUERY_TYPE_TRENDING:      payload_funct = add_view_trending_videos_payload; break;
            case QUERY_TYPE_VIEW_PLAYLIST: payload_funct = add_view_playlist_videos_payload; break;
            case QUERY_TYPE_VIEW_CHANNEL:  payload_funct = add_view_channel_videos_payload;  break;
            case QUERY_TYPE_WATCH_HISTORY: return NULL;;
            case QUERY_TYPE_VIEW_SUBSCRIBED_CHANNELS: return NULL;;
            case QUERY_TYPE_VIEW_LIKED_VIDEOS: return NULL;
        }
    }

    if (payload_funct && (payload_funct(root, query) == false)) {
        printf("configure_payload: failed to add payload\n");
        cJSON_Delete(root); root = NULL;
    }

    return root;
}

HttpsRequest configure_post_request(const Query query, const char* host)
{
    HttpsRequest req = (HttpsRequest) {0};

    if (host == NULL) return req;

    if (configure_api_path(req.path, sizeof(req.path), query.type, "") == false) {
        printf("configure_post_request: failed to resolve path\n");
        return (HttpsRequest) {0};
    }

    cJSON* payload = configure_payload(&query);

    if (payload == NULL) {
        printf("configure_post_request: 'payload' is NULL'\n");
        return (HttpsRequest) {0};
    }

    req.payload = cJSON_Print(payload);

    if (configure_post_header(req.header, sizeof(req.header), host, req.path, strlen(req.payload)) == false) {
        printf("configure_post_request: failed to configure header\n");
        free(req.payload); req.payload = NULL;
        cJSON_Delete(payload); payload = NULL;
        return (HttpsRequest) {0};
    }
    
    cJSON_Delete(payload); payload = NULL;
    
    return req;
}

static SSL_CTX *ssl_ctx = NULL;

typedef struct {
    pthread_mutex_t mutex;
    char host[64];
    char port[16];
    SSL *ssl;
    int sockfd;
    bool connected;
    struct addrinfo *address_information;
} Connection;

void connection_init(Connection* connection, const char* host, const char* port)
{
    if ((host == NULL) || (port == NULL)) return;
    pthread_mutex_init(&connection->mutex, NULL);
    strncpy(connection->host, host, sizeof(connection->host) - 1);
    connection->host[sizeof(connection->host) - 1] = '\0';
    strncpy(connection->port, port, sizeof(connection->port) - 1);
    connection->port[sizeof(connection->port) - 1] = '\0';
    connection->ssl = NULL;
    connection->sockfd = -1; 
    connection->connected = false;
    connection->address_information = NULL;
}

bool valid_fd(const int fd)
{
    return fd >= 0;
}

void disconnect(Connection* connection)
{
    if (connection == NULL) return;

    if (connection->address_information) {
        freeaddrinfo(connection->address_information);
        connection->address_information = NULL;
    }

    if (connection->ssl) {
        SSL_shutdown(connection->ssl);
        SSL_free(connection->ssl);
        connection->ssl = NULL;
    }

    if (valid_fd(connection->sockfd)) {
        close(connection->sockfd);
        connection->sockfd = -1;
    }

    connection->connected = false;
}

void connection_free(Connection* connection)
{
    if (connection == NULL) return;
    disconnect(connection);
    pthread_mutex_destroy(&connection->mutex);
}

bool connection_establish(Connection* connection, SSL_CTX* ssl_ctx)
{
    if ((connection == NULL) || (ssl_ctx == NULL) || (connection->host[0] == '\0') || (connection->port[0] == '\0')) return false;

    disconnect(connection);

    struct addrinfo desired_address_information = {0};
    desired_address_information.ai_family = AF_INET;
    desired_address_information.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(connection->host, connection->port, &desired_address_information, &connection->address_information) != 0) {
        printf("establish_persistent_connection: getaddrinfo failed for %s:%s\n", connection->host, connection->port);
        return false;
    }

    connection->sockfd = socket(connection->address_information->ai_family, connection->address_information->ai_socktype, connection->address_information->ai_protocol);
    if (connection->sockfd < 0) {
        printf("establish_persistent_connection: socket creation failed\n");
        disconnect(connection);
        return false;
    }

    if (connect(connection->sockfd, connection->address_information->ai_addr, connection->address_information->ai_addrlen) != 0) {
        printf("establish_persistent_connection: connect failed for the host: \"%s\"\n", connection->host);
        disconnect(connection);
        return false;
    }

    connection->ssl = SSL_new(ssl_ctx);
    if (connection->ssl == NULL) {
        printf("establish_persistent_connection: SSL_new failed\n");
        disconnect(connection);
        return false;
    }

    SSL_set_fd(connection->ssl, connection->sockfd);
    if (SSL_connect(connection->ssl) != 1) {
        printf("establish_persistent_connection: SSL_connect failed for host %s\n", connection->host);
        disconnect(connection);
        return false;
    }

    return true;
}

typedef struct
{
    size_t current_conn;
    Connection connections[N_CONN];
} ConnectionPool;

static ConnectionPool youtube_pool;
static ConnectionPool video_thumbnail_pool;
static ConnectionPool channel_thumbnail_pool;

ConnectionPool* media_type_to_pool(const MediaType media_type)
{   
    switch (media_type) {
        case MEDIA_TYPE_LIVE:
        case MEDIA_TYPE_SHORT:
        case MEDIA_TYPE_VIDEO:
        case MEDIA_TYPE_PLAYLIST: return &video_thumbnail_pool;
        case MEDIA_TYPE_CHANNEL: return &channel_thumbnail_pool;
        default:
            printf("media_type_to_pool: invalid type passed %d\n", media_type);
            return NULL;
    }
}

ConnectionPool init_connection_pool(const char* host)
{
    ConnectionPool pool = {0};
    for (int c = 0; c < N_CONN; c++) connection_init(&pool.connections[c], host, HTTPS_PORT);
    return pool;
}

void free_connection_pool(ConnectionPool* connection_pool)
{
    if (connection_pool == NULL) return;
    for(int c = 0; c < N_CONN; c++) connection_free(&connection_pool->connections[c]);
}

void cycle_connection(ConnectionPool* connection_pool)
{
    connection_pool->current_conn = bound_index_to_array((connection_pool->current_conn + 1), N_CONN);
}

int ssl_read_line(SSL* ssl, char* buffer, const size_t buffer_size) 
{
    if ((ssl == NULL) || (buffer == NULL)) return -1;

    size_t pos = 0;
    char c;

    while (pos < buffer_size - 1) {
        int byte = SSL_read(ssl, &c, 1);
        if (byte <= 0) {
            printf("ssl_read_line: SSL_read returned %d\n", byte);
            return byte;
        }

        buffer[pos++] = c;

        if (c == '\n') break;
    }

    buffer[pos] = '\0';

    return pos;
}

int ssl_read_header(SSL* ssl, char* dest, const size_t dest_size)
{
    if ((ssl == NULL) || (dest == NULL)) return 0;

    const char* last_line = "\r\n";

    size_t total_len = 0;
    char line[1024] = {0};
    int line_len = 0;

    while(strcmp(line, last_line) != 0) {
        line_len = ssl_read_line(ssl, line, sizeof(line));
        if (line_len <= 0) {
            printf("ssl_read_header: 'line_len' is %d\n", line_len);
            return line_len;
        }

        strncat(dest, line, dest_size - total_len);

        total_len += line_len;
    }

    dest[total_len] = '\0';

    return total_len;
}

int ssl_read_n(SSL* ssl, Buffer* buffer, const size_t n)
{
    if ((ssl == NULL) || (buffer == NULL)) return -1;

    char data[4096];

    size_t bytes_read = 0;
    size_t bytes_remaining = n;
    while (bytes_remaining > 0) {
        size_t to_read = bytes_remaining < sizeof(data) ? bytes_remaining : sizeof(data);
        
        int read = SSL_read(ssl, data, to_read);
        if (read <= 0) {
            printf("ssl_read_n: SSL read returned %d\n", read);
            return read;
        }

        bytes_read += read;
        bytes_remaining -= read; 
        buffer_write_data(buffer, data, read);
    }      

    return bytes_read;
}

bool ssl_write_request(SSL* ssl, const HttpsRequest req)
{
    if (ssl == NULL) return false;

    if (SSL_write(ssl, req.header, strlen(req.header)) <= 0) {
        printf("ssl_write_request: failed to write header\n");
        return false;
    }

    if (req.payload && (req.payload[0] != '\0')) {
        if (SSL_write(ssl, req.payload, strlen(req.payload)) <= 0) {
            printf("ssl_write_request: failed to write body\n");
            return false;
        }
    }

    return true;
}

int get_https_request_code(const char* response_line, char* dest, const size_t dest_size)
{
    if (response_line == NULL) return -1;

    bool in_request_code = false;
    
    int i = 0;

    for (const char* current = response_line; (*current) && (i < dest_size); current++) {
        const char c = (*current);

        if (in_request_code == false) {
            if (isdigit(c)) in_request_code = true;
        }

        if (in_request_code) {
            if (isdigit(c) == false) break;

            dest[i++] = c;
        }
    }

    dest[i] = '\0';

    return i;
}

bool valid_request_code(const char* response_header)
{
    if (response_header == NULL) return false;

    char* response_line = strstr(response_header, "HTTP/1.1");
    if (response_line == NULL) {
        printf("valid_request_code: \"HTTP/1.1\" not found\n");
        return false;
    }

    response_line += strlen("HTTP/1.1");

    char request_code[8] = {0};
    if (get_https_request_code(response_line, request_code, sizeof(request_code)) <= 0) {
        printf("valid_request_code: get_https_request_code failed\n");
        return false;
    }

    return (strcmp(request_code, VALID_HTTPS_RESPONSE_CODE) == 0);
}

Buffer send_https_request(const HttpsRequest request, SSL_CTX* ssl_ctx, Connection* connection)
{
    Buffer res = buffer_init();

    if ((ssl_ctx == NULL) || (connection == NULL)) return res;

    pthread_mutex_lock(&connection->mutex);

    if (connected_to_internet() == false) {
        printf("send_https_request: no internet connection\n");
        connection->connected = false;
        pthread_mutex_unlock(&connection->mutex);
        return res;
    }

    if (connection->connected == false) {
        if ((connection->connected = connection_establish(connection, ssl_ctx)) == false) {
            printf("send_https_request: connection_established failed\n");
            pthread_mutex_unlock(&connection->mutex);
            return res;
        }
    }

    if (ssl_write_request(connection->ssl, request) == false) {
        printf("send_https_request: ssl_write_request failed\n");
        connection->connected = false;
        pthread_mutex_unlock(&connection->mutex);
        return res;
    }

    char header[4096] = {0};
    if (ssl_read_header(connection->ssl, header, sizeof(header)) <= 0) {
        printf("send_https_request: ssl_read_header failed\n");
        connection->connected = false;
        pthread_mutex_unlock(&connection->mutex);
        return res;
    }

    if (valid_request_code(header) == false) {
        printf("send_https_request: valid_request_code failed\n");
        create_file("invalid_code_header.txt", header);
        connection->connected = false;
        pthread_mutex_unlock(&connection->mutex);
        return res;
    }

    char* content_length_line = strstr(header, CONTENT_LENGTH_HEADER_TAG);
    if (content_length_line) {
        char len_str[16] = {0};
        int i = 0;

        char* ptr = content_length_line + strlen(CONTENT_LENGTH_HEADER_TAG);
        if (ptr) {
            while (*ptr && isspace((unsigned char) *ptr)) ptr++;

            for ( ; (*ptr) && (*ptr != '\n'); ptr++) {
                if (isdigit(*ptr)) len_str[i++] = *ptr;
            }
        }

        len_str[i] = '\0';

        int content_length = atoi(len_str);
        if (content_length == 0) {
            printf("send_https_request: invalid content length read\n");
            create_file("invalid_content_len_header.txt", header);
            connection->connected = false;
            pthread_mutex_unlock(&connection->mutex);
            return res;
        }

        ssl_read_n(connection->ssl, &res, content_length);
    }

    char* transfer_encoding_line = strstr(header, TRANSFER_ENCODING_HEADER_TAG);
    if (transfer_encoding_line) {
        char encoding_type[16] = {0};
        int i = 0;

        char* ptr = transfer_encoding_line += strlen(TRANSFER_ENCODING_HEADER_TAG);
        if (ptr) {
            while (*ptr && isspace(*ptr)) ptr++;

            for (; *ptr && (*ptr != '\n'); ptr++) {
                if (isalpha(*ptr)) encoding_type[i++] = *ptr;
            }
        }
        
        encoding_type[i] = '\0';

        if (strcmp(encoding_type, CHUNKED_ENCODING) == 0) {
            const char *crlf = "\r\n";
            const size_t crlf_len = strlen(crlf);
            
            int chunk_size = -1; 
            while (chunk_size != 0) {
                char hex[16] = {0};
                int len = ssl_read_line(connection->ssl, hex, sizeof(hex));
                if (len <= crlf_len) {
                    printf("send_https_request: failed to read chunk size\n");
                    buffer_free(&res);
                    connection->connected = false;
                    pthread_mutex_unlock(&connection->mutex);
                    return res;
                }

                hex[len - crlf_len] = '\0';

                chunk_size = strtol(hex, NULL, 16);
                int read = ssl_read_n(connection->ssl, &res, chunk_size);
                if (read != chunk_size) {
                    printf("send_https_request: (%d/%d) bytes read\n", read, chunk_size);
                    buffer_free(&res);
                    connection->connected = false;
                    pthread_mutex_unlock(&connection->mutex);
                    return res;
                }

                char trailing_crlf[16];
                ssl_read_line(connection->ssl, trailing_crlf, sizeof(trailing_crlf));
            }
        }
    }

    pthread_mutex_unlock(&connection->mutex);
    return res;
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

cJSON* parse_json_file(const char* filename)
{
    if ((filename == NULL) || (filename[0] == '\0')) return NULL;

    char* buffer = get_file_content(filename);
    if (buffer == NULL) {
        printf("parse_json_file: 'buffer' is null\n");
        return NULL;
    }

    cJSON* json = cJSON_Parse(buffer);

    free(buffer); buffer = NULL;

    return json;
}

bool write_json_file(const cJSON* json, const char* filename)
{
    if ((json == NULL) || (filename == NULL)) return false;

    char* buffer = cJSON_Print(json);
    if (buffer == NULL) {
        printf("write_json_file: 'buffer' is null\n");
        return false; 
    }

    const bool write_status = create_file(filename, buffer);

    free(buffer); buffer = NULL;

    return write_status;
}

cJSON* cjson_pointer_get(cJSON* root, const char* path)
{
    if (root == NULL) return NULL;
    if (path == NULL) return root;

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

    if (video_is_youtube_short(videoRenderer)) {
        if (allow_youtube_shorts == false){
            video->media_type = MEDIA_TYPE_UNDF;
            return;
        }

        else video->media_type = MEDIA_TYPE_SHORT;
    }

    const char* id_path = ".videoId";

    if (assign_string_from_path(videoRenderer, id_path, video->id, sizeof(video->id)) == false) {
        printf("parse_video: id assign fail (json path: \"%s\")\n", id_path);
        video->media_type = MEDIA_TYPE_UNDF;
        return;
    }

    video->media_type = MEDIA_TYPE_VIDEO;

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
        video->media_type = MEDIA_TYPE_LIVE;

        const char* live_viewers_path = ".viewCountText.runs[0].text";

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
        related_vid->media_type = MEDIA_TYPE_UNDF;
        return;
    }

    related_vid->media_type = MEDIA_TYPE_VIDEO;

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
        playlist_vid->media_type = MEDIA_TYPE_UNDF;
        return;
    }

    playlist_vid->media_type = MEDIA_TYPE_VIDEO;

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

void parse_watched_video(cJSON* root, SearchResult* watched_video)
{
    if ((root == NULL) || (watched_video == NULL)) return;

    if (assign_string_from_path(root, ".id", watched_video->id, sizeof(watched_video->id)) == false) {
        printf("parse_watched_video: assign id fail\n");
        watched_video->media_type = MEDIA_TYPE_UNDF;
        return;
    }

    watched_video->media_type = MEDIA_TYPE_VIDEO;

    if (assign_string_from_path(root, ".title", watched_video->title, sizeof(watched_video->title)) == false) {
        printf("parse_watched_video: assign title fail\n");
    } 

    if (assign_string_from_path(root, ".authorId", watched_video->authorId, sizeof(watched_video->authorId)) == false) {
        printf("parse_watched_video: assign authorId fail\n");
    } 

    if (assign_string_from_path(root, ".duration", watched_video->duration, sizeof(watched_video->duration)) == false) {
        printf("parse_watched_video: assign duration fail\n");
    } 

    if (assign_string_from_path(root, ".view_count", watched_video->view_count, sizeof(watched_video->view_count)) == false) {
        printf("parse_watched_video: assign view_count fail\n");
    } 

    if (assign_string_from_path(root, ".thumbnail_path", watched_video->thumbnail_path, sizeof(watched_video->thumbnail_path)) == false) {
        printf("parse_watched_video: assign thumbnail_path fail\n");
    } 

    if (assign_string_from_path(root, ".date_published", watched_video->date_published, sizeof(watched_video->date_published)) == false) {
        printf("parse_watched_video: assign date_published fail\n");
    } 
}

void parse_channel_result(cJSON* channelRenderer, SearchResult* channel)
{
    if ((channelRenderer == NULL) || (channel == NULL)) return;

    const char* id_path = ".channelId";

    if (!assign_string_from_path(channelRenderer, id_path, channel->id, sizeof(channel->id))) {
        printf("parse_channel: id assign fail (json path: \"%s\")\n", id_path);
        channel->media_type = MEDIA_TYPE_UNDF;
        return;
    }

    channel->media_type = MEDIA_TYPE_CHANNEL;

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

void parse_subscribed_channel(cJSON* subscribedChannelRenderer, SearchResult* channel)
{
    if ((subscribedChannelRenderer == NULL) || (channel == NULL)) {
        printf("parse_subscribed_channel: invalid input(s)\n");
        return;
    }

    if (assign_string_from_path(subscribedChannelRenderer, ".id", channel->id, sizeof(channel->id)) == false) {
        printf("parse_subscribed_channel: id assign fail\n");
        channel->media_type = MEDIA_TYPE_UNDF;
        return;
    }

    channel->media_type = MEDIA_TYPE_CHANNEL;

    if (assign_string_from_path(subscribedChannelRenderer, ".thumbnail_path", channel->thumbnail_path, sizeof(channel->thumbnail_path)) == false) {
        printf("parse_subscribed_channel: thumbail_path assign fail\n");
    }
    
    if (assign_string_from_path(subscribedChannelRenderer, ".title", channel->title, sizeof(channel->title)) == false) {
        printf("parse_subscribed_channel: title assign fail\n");
    }

    if (assign_string_from_path(subscribedChannelRenderer, ".subscriber_count", channel->subscriber_count, sizeof(channel->subscriber_count)) == false) {
        printf("parse_subscribed_channel: subscriber_count assign fail\n");
    }
}

void parse_playlist(cJSON *lockupViewModel, SearchResult *playlist)
{
    if ((lockupViewModel == NULL) || (playlist == NULL)) return;

    const char* id_path = ".contentId";

    if (!assign_string_from_path(lockupViewModel, id_path, playlist->id, sizeof(playlist->id))) {
        printf("parse_playlist: id assign fail (json path: \"%s\")\n", id_path);
        playlist->media_type = MEDIA_TYPE_UNDF;
        return;
    }
    
    playlist->media_type = MEDIA_TYPE_PLAYLIST;

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

static bool application_running = true;

void* worker_thread_funct(void* args)
{
    List* task_queue = (List*) args;
    
    while (application_running) {
        pthread_mutex_lock(&task_queue->mutex);
        while ((task_queue->count == 0) && application_running) 
            pthread_cond_wait(&task_queue->cond, &task_queue->mutex);

        if (application_running == false) {
            pthread_mutex_unlock(&task_queue->mutex);
            break;
        }

        Node* node = list_dequeue(task_queue);
        ThreadTask* task = (ThreadTask*) node->content;

        pthread_mutex_unlock(&task_queue->mutex); 
        task->funct(task->args);
        free(task); task = NULL;
        free(node); node = NULL;
    }

    return NULL;
}

bool launch_task(List* task_queue, void* targs, void* (*funct)(void*))
{
    if ((task_queue == NULL) || (targs == NULL) || (funct == NULL)) return false;

    Node* node = node_init(NODE_TYPE_THREAD_TASK);
    if ((node == NULL) || (node->content == NULL)) {
        printf("launch_task: node_init failed\n");
        if (node) {
            free(node); node = NULL;
        } 
        return false;
    }

    ThreadTask* task = (ThreadTask*) node->content;
    task->next = NULL;
    task->args = targs;
    task->funct = funct;

    pthread_mutex_lock(&task_queue->mutex);
    list_append(task_queue, node);
    pthread_cond_signal(&task_queue->cond);
    pthread_mutex_unlock(&task_queue->mutex);

    return true;
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
    char thumbnail_path[256];
    char id[64];
    List* thumbnail_queue;
    MediaType media_type;
} LoadThumbnailArgs;

void* load_thumbnail(void* args)
{
    LoadThumbnailArgs* targs = (LoadThumbnailArgs*) args;
    if (targs == NULL) {
        printf("load_thumbnail: args are null\n");
        return NULL;
    }

    ConnectionPool* pool = media_type_to_pool(targs->media_type);
    if (pool == NULL) {
        printf("load_thumbnail: 'pool' is null\n");
        goto clean;
    }

    Connection* conn = &pool->connections[pool->current_conn];

    HttpsRequest req = {0};
    if (configure_get_header(req.header, sizeof(req.header), conn->host, targs->thumbnail_path) == false) {
        printf("load_thumbnail: req header was truncated\n");
        goto clean;
    }

    Buffer res = send_https_request(req, ssl_ctx, conn);
    if (buffer_ready(&res) == false) {
        printf("load_thumbnail: thumbnail response is invalid\n");
        goto clean;
    }

    Node* node = node_init(NODE_TYPE_RAW_THUMBNAIL);
    if ((node == NULL) || (node->content == NULL)) {
        printf("load_thumbnail: invalid node created\n");
        buffer_free(&res);
        goto clean;
    }

    RawThumbnail* raw = (RawThumbnail*) node->content;

    raw->next = NULL;
    raw->data = res;
    raw->media_type = targs->media_type;
    snprintf(raw->id, sizeof(raw->id), "%s", targs->id);

    pthread_mutex_lock(&targs->thumbnail_queue->mutex);
    list_append(targs->thumbnail_queue, node);
    pthread_mutex_unlock(&targs->thumbnail_queue->mutex);

    clean:
        free(targs); targs = NULL;
        return NULL;
}

typedef struct
{
    Query query;
    List* search_results;
} SearchThreadArgs;

const char* get_results_list_path(const QueryType search_type, const QueryAttribute search_attr)
{
    switch (search_type) {
        case QUERY_TYPE_USER_INPUT: 
            if (search_attr == QUERY_ATTR_REPLACE) return ".contents.twoColumnSearchResultsRenderer.primaryContents.sectionListRenderer.contents[0].itemSectionRenderer.contents";
            if (search_attr == QUERTY_ATTR_APPEND) return ".onResponseReceivedCommands[0].appendContinuationItemsAction.continuationItems[0].itemSectionRenderer.contents";
        case QUERY_TYPE_RELATED: 
            if (search_attr == QUERY_ATTR_REPLACE) return "contents.twoColumnWatchNextResults.secondaryResults.secondaryResults.results";
            if (search_attr == QUERTY_ATTR_APPEND) return ".onResponseReceivedEndpoints[0].appendContinuationItemsAction.continuationItems";
        case QUERY_TYPE_VIEW_PLAYLIST: 
            if (search_attr == QUERY_ATTR_REPLACE) return ".contents.twoColumnBrowseResultsRenderer.tabs[0].tabRenderer.content.sectionListRenderer.contents[0].itemSectionRenderer.contents[0].playlistVideoListRenderer.contents";
            if (search_attr == QUERTY_ATTR_APPEND) return ".onResponseReceivedActions[0].appendContinuationItemsAction.continuationItems";
        case QUERY_TYPE_VIEW_CHANNEL: 
            if (search_attr == QUERY_ATTR_REPLACE) return ".contents.twoColumnBrowseResultsRenderer.tabs[1].tabRenderer.content.richGridRenderer.contents";
            if (search_attr == QUERTY_ATTR_APPEND) return ".onResponseReceivedActions[0].appendContinuationItemsAction.continuationItems";
        case QUERY_TYPE_TRENDING:                  return ".contents.twoColumnBrowseResultsRenderer.tabs[0].tabRenderer.content.sectionListRenderer.contents[2].itemSectionRenderer.contents[0].shelfRenderer.content.expandedShelfContentsRenderer.items";
        case QUERY_TYPE_WATCH_HISTORY:             return WATCH_HISTORY_ARRAY;
        case QUERY_TYPE_VIEW_SUBSCRIBED_CHANNELS:  return SUBSCRIBED_CHANNELS_ARRAY;
        case QUERY_TYPE_VIEW_LIKED_VIDEOS:         return LIKED_VIDEOS_ARRAY;
        case QUERY_TYPE_VIDEO_FOCUS: break;
    }

    return NULL;
}

int create_results_from_json(cJSON* json, List* results, const QueryType search_type, const QueryAttribute search_attr, const bool allow_youtube_shorts)
{
    if ((json == NULL || (results == NULL))) return -1;

    const char* path = get_results_list_path(search_type, search_attr); // .channels

    cJSON* results_array = cjson_pointer_get(json, path);
    if (valid_cjson_array(results_array) == false) {
        printf("create_results_from_json: invalid results array from path %s\n", path);
        return -1;
    }

    char author_id[64] = {0};
    if (search_type == QUERY_TYPE_VIEW_CHANNEL) {
        const char* author_id_path = (search_attr == QUERY_ATTR_REPLACE) 
                                     ? ".contents.twoColumnBrowseResultsRenderer.tabs[0].tabRenderer.endpoint.browseEndpoint.browseId"
                                     : ".responseContext.serviceTrackingParams[0].params[3].value";

        if (assign_string_from_path(json, author_id_path, author_id, sizeof(author_id)) == false) {
            printf("create_results_from_json: failed to parse author id from the path %s\n", author_id_path);
        }
    }

    int elements_added = 0;
    const int old_size = results->count;

    cJSON *item;
    cJSON_ArrayForEach (item, results_array) {
        Node* node = node_init(NODE_TYPE_SEACH_RESULT);
        if ((node == NULL) || (node->content == NULL)) {
            printf("create_results_from_json: node_init returned NULL\n");
            return 0;
        }

        SearchResult* search_result = (SearchResult*) node->content;
        
        cJSON* videoRenderer             = cjson_pointer_get(item, ".videoRenderer");     
        cJSON* richItemRenderer          = cjson_pointer_get(item, ".richItemRenderer.content.videoRenderer");
        cJSON* playlistVideoRenderer     = cjson_pointer_get(item, ".playlistVideoRenderer"); 
        cJSON* watchedVideoRenderer      = cjson_pointer_get(item, ".watchedVideoRenderer");
        cJSON* likedVideoRenderer        = cjson_pointer_get(item, ".likedVideoRenderer");
        cJSON* channelRenderer           = cjson_pointer_get(item, ".channelRenderer");  
        cJSON* subscribedChannelRenderer = cjson_pointer_get(item, ".subscribedChannelRenderer");
        cJSON* lockupViewModel           = cjson_pointer_get(item, ".lockupViewModel");   
        
        if      (videoRenderer)             parse_video(videoRenderer, author_id, allow_youtube_shorts, search_result);
        else if (richItemRenderer)          parse_video(richItemRenderer, author_id, allow_youtube_shorts,search_result);
        else if (playlistVideoRenderer)     parse_playlist_video(playlistVideoRenderer, search_result);
        else if (watchedVideoRenderer)      parse_watched_video(watchedVideoRenderer, search_result);
        else if (likedVideoRenderer)        parse_watched_video(likedVideoRenderer, search_result);
        else if (channelRenderer)           parse_channel_result(channelRenderer, search_result);
        else if (subscribedChannelRenderer) parse_subscribed_channel(subscribedChannelRenderer, search_result);
        else if (lockupViewModel) {
            if (search_type == QUERY_TYPE_RELATED) parse_related_video(lockupViewModel, search_result);
            else parse_playlist(lockupViewModel, search_result);
        }

        if (search_result->media_type != MEDIA_TYPE_UNDF) {
            elements_added++;
            list_append(results, node);
        }

        else node_free(node);
    }

    if (search_attr == QUERY_ATTR_REPLACE) {
        for (int i = 0; results->head && (i < old_size); i++) {
            Node* to_del = list_dequeue(results);
            node_free(to_del);
        }
    }

    return elements_added;
}

const char* query_type_to_text(const QueryType query_type)
{
    switch (query_type) {
        case QUERY_TYPE_USER_INPUT: return "QUERIED";
        case QUERY_TYPE_RELATED: return "RELATED";
        case QUERY_TYPE_TRENDING: return "TRENDING";
        case QUERY_TYPE_VIDEO_FOCUS: return "VIDEO FOCUS";
        case QUERY_TYPE_VIEW_PLAYLIST: return "VIEW PLAYLIST";
        case QUERY_TYPE_VIEW_CHANNEL: return "VIEW CHANNEL";
        default:
            return NULL;
    }
}

const char* query_attr_to_text(const QueryAttribute query_attr)
{
    switch (query_attr) {
        case QUERY_ATTR_REPLACE: return "NEW";
        case QUERTY_ATTR_APPEND: return "APPENDING";
        default:
            return NULL;
    }
}

const char* get_continuation_token_path(const QueryType search_type, const QueryAttribute search_attr)
{
    switch (search_type) {
        case QUERY_TYPE_USER_INPUT:
            if (search_attr == QUERY_ATTR_REPLACE) return ".contents.twoColumnSearchResultsRenderer.primaryContents.sectionListRenderer.contents[1].continuationItemRenderer.continuationEndpoint.continuationCommand.token";
            if (search_attr == QUERTY_ATTR_APPEND) return ".onResponseReceivedCommands[0].appendContinuationItemsAction.continuationItems[1].continuationItemRenderer.continuationEndpoint.continuationCommand.token";
        case QUERY_TYPE_RELATED:
            if (search_attr == QUERY_ATTR_REPLACE) return ".contents.twoColumnWatchNextResults.secondaryResults.secondaryResults.results[-1].continuationItemRenderer.continuationEndpoint.continuationCommand.token";
            if (search_attr == QUERTY_ATTR_APPEND) return ".onResponseReceivedEndpoints[0].appendContinuationItemsAction.continuationItems[-1].continuationItemRenderer.continuationEndpoint.continuationCommand.token";
        case QUERY_TYPE_VIEW_PLAYLIST:
            if (search_attr == QUERY_ATTR_REPLACE) return ".contents.twoColumnBrowseResultsRenderer.tabs[0].tabRenderer.content.sectionListRenderer.contents[0].itemSectionRenderer.contents[0].playlistVideoListRenderer.contents[-1].continuationItemRenderer.continuationEndpoint.commandExecutorCommand.commands[1].continuationCommand.token";
            if (search_attr == QUERTY_ATTR_APPEND) return ".onResponseReceivedActions[0].appendContinuationItemsAction.continuationItems[-1].continuationItemRenderer.continuationEndpoint.continuationCommand.token";
        case QUERY_TYPE_VIEW_CHANNEL:
            if (search_attr == QUERY_ATTR_REPLACE) return ".contents.twoColumnBrowseResultsRenderer.tabs[1].tabRenderer.content.richGridRenderer.contents[-1].continuationItemRenderer.continuationEndpoint.continuationCommand.token";
            if (search_attr == QUERTY_ATTR_APPEND) return ".onResponseReceivedActions[0].appendContinuationItemsAction.continuationItems[-1].continuationItemRenderer.continuationEndpoint.continuationCommand.token";
        default: return NULL;
    }
}

void get_continuation_token(cJSON* json, const QueryType query_type, const QueryAttribute query_attr)
{
    if (json == NULL) {
        printf("get_continuation_token: invalid input(s)\n");
        return;
    }

    const char* continuation_path = get_continuation_token_path(query_type, query_attr);
    
    if (continuation_token) {
        free(continuation_token); continuation_token = NULL;
    }

    const cJSON* token_obj = cjson_pointer_get(json, continuation_path);
    if (valid_cjson_string(token_obj) == false) {
        printf("get_continuation_token: 'token_obj' is invalid (path: %s)\n", continuation_path);
        return;
    }

    if ((continuation_token = strdup(token_obj->valuestring)) == NULL) {
        printf("get_continuation_token: strdup failed\n");
    }
}

cJSON* get_json_response(const HttpsRequest* req, SSL_CTX* ssl_ctx, Connection* conn)
{
    if ((req == NULL) || (ssl_ctx == NULL) || (conn == NULL)) {
        printf("get_json_response: invalid input(s)\n");
        return NULL;
    }

    Buffer res = send_https_request((*req), ssl_ctx, conn);
    if (buffer_ready(&res) == false) {
        printf("get_json_response: invalid response recived\n");
        return NULL;
    }

    cJSON* ret = cJSON_Parse(res.data);

    buffer_free(&res);
    
    return ret;
}

void log_search(const QueryType query_type, const QueryAttribute query_attr, const float duration, const int nresults)
{
    const char* type_text = query_type_to_text(query_type);
    const char* attr_text = query_attr_to_text(query_attr);
    printf("%s (%s) took %f seconds, %d items found\n", type_text, attr_text, duration, nresults);
}

void* get_results_from_query(void* args)
{
    float start_time = GetTime(); // preformance check
    
    SearchThreadArgs* targs = (SearchThreadArgs*) args;
    if (targs == NULL) {
        printf("get_results_from_query: invalid arguements passed\n");
        SetWindowTitle("[failed] - metube");
        return NULL;
    }

    Connection* conn = &youtube_pool.connections[youtube_pool.current_conn];

    HttpsRequest req = configure_post_request(targs->query, conn->host);
    if (valid_post_request(req) == false) {
        printf("get_results_from_query: invalid post req configured\n");
        free(targs); targs = NULL;
        return NULL;
    }

    cJSON* json_res = get_json_response(&req, ssl_ctx, conn);

    free(req.payload); req.payload = NULL;

    if (json_res == NULL) {
        printf("get_results_from_query: 'json_res' is null\n");
        free(targs); targs = NULL;
        return NULL;
    }

    const QueryType query_type = targs->query.type;
    const QueryAttribute query_attr = targs->query.attr;
    
    pthread_mutex_lock(&targs->search_results->mutex);

    const int elements_added = create_results_from_json(json_res, targs->search_results, query_type, query_attr, targs->query.allow_youtube_shorts);
    
    pthread_mutex_unlock(&targs->search_results->mutex);

    get_continuation_token(json_res, query_type, query_attr);

    SetWindowTitle(TextFormat("[search results(%zu)] - metube", targs->search_results->count));
    
    log_search(query_type, query_attr, GetTime() - start_time, elements_added);

    free(targs); targs = NULL;
    cJSON_Delete(json_res); json_res = NULL;
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
    DrawTextEx(ui.font, text, (Vector2){length_area.x + ui.padding, length_area.y + ui.padding}, font_size, ui.spacing, text_color);
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

void draw_search_result(SearchResult *search_result, const Texture thumbnail, const Rectangle container,  const Color color, const Ui ui)
{
    DrawRectangleRec(container, color);

    const Rectangle thumbnail_area = { 
        .x = container.x, 
        .y = container.y, 
        .width = container.width * 0.45f, 
        .height = container.height 
    };

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
        case MEDIA_TYPE_SHORT:
        case MEDIA_TYPE_VIDEO:
            DrawTextBoxed(TextFormat("%s   %s", search_result->date_published, search_result->view_count), padded_rectangle(ui.padding, subtext_area), ui, 11.5, BLACK);
            DrawTextureEx(thumbnail, (Vector2){thumbnail_area.x, thumbnail_area.y}, 0.0f, 1.0f, WHITE);
            draw_thumbnail_subtext(thumbnail_area, ui, RAYWHITE, 12, search_result->duration);
            break;
        case MEDIA_TYPE_LIVE:
            DrawTextBoxed(TextFormat("%s watching", search_result->view_count), padded_rectangle(ui.padding, subtext_area), ui, 12, BLACK);
            DrawTextureEx(thumbnail, (Vector2){thumbnail_area.x, thumbnail_area.y}, 0.0f, 1.0f, WHITE);
            draw_thumbnail_subtext(thumbnail_area, ui, RAYWHITE, 12, "LIVE");
            break;
        case MEDIA_TYPE_CHANNEL: {
            const float x_padding = thumbnail.width / 2.0f;
            const float y_padding = (container.height - thumbnail.height) / 2.0f;
            DrawTextureEx(thumbnail, (Vector2){thumbnail_area.x + x_padding, thumbnail_area.y + y_padding}, 0.0f, 1.0f, WHITE);
            DrawTextBoxed(search_result->subscriber_count, padded_rectangle(ui.padding, subtext_area), ui, 12, BLACK);
            draw_thumbnail_subtext(thumbnail_area, ui, RAYWHITE, 12, "Channel");
            break;
        }
        case MEDIA_TYPE_PLAYLIST:
            DrawTextureEx(thumbnail, (Vector2){thumbnail_area.x, thumbnail_area.y}, 0.0f, 1.0f, WHITE);
            draw_thumbnail_subtext(thumbnail_area, ui, RAYWHITE, 12, search_result->video_count);
            break;
        default:    
            break;
    }
}

void process_raw_thumbnail(RawThumbnail* raw_thumbnail, TextureCacheEntry** hashtable)
{   
    if ((raw_thumbnail == NULL) || (buffer_ready(&raw_thumbnail->data) == false)) return;
    
    const Vector2 dimension = media_type_to_thumbnail_dim(raw_thumbnail->media_type);

    const Texture2D thumbnail = load_texture_from_memory(raw_thumbnail->data, dimension.x, dimension.y);
    if (IsTextureReady(thumbnail) == false) {
        printf("process_raw_thumbnail: 'thumbnail' is invalid\n");
        return;
    }

    TextureCacheEntry* cached_entry = cached_texture_init(thumbnail, raw_thumbnail->id);
    if (cached_entry == NULL) {
        printf("process_raw_thumbnail: 'cached_entry' is null\n");
        return;
    }
    
    cache_texture(hashtable, cached_entry);
}

void process_thumbnail_queue(List* thumbnail_queue, TextureCacheEntry **hashtable)
{
    if (thumbnail_queue == NULL) return;

    while (thumbnail_queue->head != NULL) {
        Node* node = list_dequeue(thumbnail_queue);

        if (node->type == NODE_TYPE_RAW_THUMBNAIL) {
            process_raw_thumbnail(node->content, hashtable);
        }

        node_free(node);
    }
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

bool queue_thumbnail_load(const char* search_result_id, const char* thumbnail_path, List* task_queue, List* thumbnail_queue, MediaType media_type)
{
    if ((search_result_id == NULL) || (thumbnail_path == NULL) || (task_queue == NULL) || (thumbnail_queue == NULL)) {
        printf("queue_thumbnail_load: invalid args\n");
        return false;
    }

    LoadThumbnailArgs* targs = malloc(sizeof(LoadThumbnailArgs));
    if (targs == NULL) {
        printf("queue_thumbnail_load: malloc returned NULL for 'targs'\n");
        return false;
    }

    targs->media_type = media_type;
    targs->thumbnail_queue = thumbnail_queue;
    snprintf(targs->id, sizeof(targs->id), "%s", search_result_id);
    snprintf(targs->thumbnail_path, sizeof(targs->thumbnail_path), "%s", thumbnail_path);

    return launch_task(task_queue, targs, load_thumbnail);
}

typedef struct
{
    SearchResult info;
    char* description;
} HighlightedVideo;

void draw_highlighted_video(const Rectangle container, Ui ui, Vector2* scrollbar_position, HighlightedVideo* highlighted_video)
{
    if ((scrollbar_position == NULL) || (highlighted_video == NULL)) return;

    const Color text_color = BLACK;
    const int font_size = 12;
    const int spacing = 2;

    const Rectangle scroll_window_area = {
        .x = container.x,
        .y = container.y + (container.height * 0.25f),
        .width = container.width - ui.padding,
        .height = container.height * 0.75f,
    };

    const float padded_width = container.width - (ui.padding * 2);

    const float line_height = font_size + spacing;
    const int nlines = anticipate_lines_wordwrap(ui.font, highlighted_video->description, font_size, spacing, padded_width);
    const float video_desc_text_height = line_height * nlines;
    
    const Rectangle scroll_content_area = {
        .x = scroll_window_area.x,
        .y = scroll_window_area.y,
        .width = scroll_window_area.width,
        .height = video_desc_text_height,
    };

    GuiScrollPanel(scroll_window_area, NULL, scroll_content_area, scrollbar_position, NULL, false);

    const Rectangle video_desc_bounds = {
        .x = scroll_content_area.x,
        .y = scroll_window_area.y + scrollbar_position->y,
        .height = video_desc_text_height,
        .width = scroll_content_area.width,
    };

    BeginScissorMode(scroll_window_area.x, scroll_window_area.y, scroll_window_area.width, scroll_window_area.height);

    const Rectangle padded_video_desc_bounds = padded_rectangle(ui.padding, video_desc_bounds);
    DrawTextBoxed(highlighted_video->description, padded_video_desc_bounds, ui, font_size, text_color);

    EndScissorMode();
}

typedef struct
{
    HttpsRequest req;
    Connection* conn;
    HighlightedVideo* highlighted_video;
} FocusedInfoArgs;

FocusedInfoArgs* init_focused_info_args(HttpsRequest req, Connection* conn, HighlightedVideo* highlighted_video)
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

void* get_video_description(void* args)
{
    cJSON* json = NULL;
    Buffer res = buffer_init();
    FocusedInfoArgs* targs = (FocusedInfoArgs*) args;
    if (targs == NULL) {
        printf("get_video_description: 'targs' is NULL\n");
        goto cleanup;
    }

    res = send_https_request(targs->req, ssl_ctx, targs->conn); 
    if (buffer_ready(&res) == false) {
        printf("get_video_description: invaild https response\n");
        goto cleanup;
    }

    json = cJSON_Parse(res.data);
    if (json == NULL) {
        printf("get_video_description: cJSON_Parse returned NULL\n");
        goto cleanup;
    }

    if (targs->highlighted_video->description) {
        free(targs->highlighted_video->description); targs->highlighted_video->description = NULL;
    }

    const char* desc_path = ".videoDetails.shortDescription";

    const cJSON* shortDescription = cjson_pointer_get(json, desc_path);
    if (valid_cjson_string(shortDescription)) {
        if ((targs->highlighted_video->description = strdup(shortDescription->valuestring)) == NULL) {
            printf("get_video_description: strdup failed\n");
        }
    }

    SetWindowTitle(TextFormat("[%s] - metube", targs->highlighted_video->info.title));

    cleanup:
        if (targs->req.payload) free(targs->req.payload);
        if (buffer_ready(&res)) buffer_free(&res);
        if (json) cJSON_Delete(json);
        if (targs) free(targs);
        return NULL;
}

bool queue_focused_video_task(const Query query, List* task_queue, Connection* conn, HighlightedVideo* highlighted_video)
{
    if ((task_queue == NULL) || (conn == NULL) || (highlighted_video == NULL)) {
        printf("queue_focused_video_task: invalid input\n");
        return false;
    }

    HttpsRequest post = configure_post_request(query, conn->host);
    if (valid_post_request(post) == false) {
        printf("queue_focused_video_task: 'post' is invalid\n");
        return false;
    }

    FocusedInfoArgs* targs = init_focused_info_args(post, conn, highlighted_video);

    return launch_task(task_queue, targs, get_video_description);
}

cJSON* create_empty_array_object(const char* array_name)
{
    if ((array_name == NULL) || (array_name[0] == '\0')) return NULL;

    cJSON* root = cJSON_CreateObject();

    cJSON* array = cJSON_CreateArray();

    cJSON_AddItemToObject(root, array_name, array);

    return root;
}

int find_array_item_by_id(const cJSON* array, const char* id, const char* id_path)
{
    if ((array == NULL) || (id == NULL) || (id_path == NULL)) {
        printf("find_array_item_by_id: invalid input(s)\n");
        return -1;
    }

    int i = 0;
    cJSON* item;
    cJSON_ArrayForEach(item, array) {
        const cJSON* id_item = cjson_pointer_get(item, id_path);
        if (valid_cjson_string(id_item) == false) {
            printf("find_array_item_by_id: path %s not found\n", id_path);
            return -1;
        }

        if (strcmp(id_item->valuestring, id) == 0) {
            return i; 
        }

        i++;
    }

    return -1;
}

cJSON* init_video_json_object(const SearchResult* video)
{
    if ((video == NULL) || (video->media_type != MEDIA_TYPE_VIDEO)) return NULL;

    cJSON* video_obj = cJSON_CreateObject();

    cJSON_AddStringToObject(video_obj, "id", video->id);
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
    if ((history == NULL) || (cJSON_IsArray(history) == false)) return -1;

    const char* time_added_path = "watchedVideoRenderer.time_added";

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

void update_watch_history(cJSON* root, const SearchResult* watched_video)
{
    if ((root == NULL) || (watched_video == NULL)) return;

    cJSON* history = cjson_pointer_get(root, WATCH_HISTORY_ARRAY);
    if (valid_cjson_array(history) == false) {
        printf("update_watch_history: invalid array parsed\n");
        return;
    } 

    const int watched_video_index = find_array_item_by_id(history, watched_video->id, WATCHED_VIDEO_ID_PATH);
    const bool found = (watched_video_index >= 0);
    cJSON* to_add = (found)
                    ? cJSON_DetachItemFromArray(history, watched_video_index)
                    : init_video_json_object(watched_video);

    if (to_add == NULL) {
        printf("update_watch_history: 'to_add' is NULL found_status: %d\n", found);
        return;
    }

    if (found) {
        cJSON* watchedVideoRenderer = cjson_pointer_get(to_add, ".watchedVideoRenderer");
        if (watchedVideoRenderer) {
            cJSON_ReplaceItemInObject(watchedVideoRenderer, "time_added", cJSON_CreateNumber((double)time(NULL)));
            cJSON_InsertItemInArray(history, 0, to_add);
        }
    }

    else {
        if (cJSON_GetArraySize(history) == MAX_HISTORY_LEN) {
            remove_oldest_watched_video(history);
        }

        cJSON* watchedVideoRenderer = cJSON_CreateObject();
        cJSON_AddItemToObject(watchedVideoRenderer, "watchedVideoRenderer", to_add);
        cJSON_InsertItemInArray(history, 0, watchedVideoRenderer);
    }
}

typedef struct
{
    char thumbnail_path[256];
    char name[256];
    char id[64];
    char subscriber_count[32];
    TextureCacheEntry* cached;
    bool thumbnail_loaded;
} HighlightedChannel;

bool is_subbed_to_channel(cJSON* subscribed_channels_json, const char* id)
{
    if ((subscribed_channels_json == NULL) || (id == NULL)) return false;

    cJSON* channels = cjson_pointer_get(subscribed_channels_json, SUBSCRIBED_CHANNELS_ARRAY);
    if (valid_cjson_array(channels) == false) {
        printf("is_subbed_to_channel: 'channels' is invalid array\n");
        return false;
    }

    const int found = find_array_item_by_id(channels, id, SUBSCRIBED_CHANNEL_ID_PATH);

    return (found >= 0);
}

void update_subscription(cJSON* subscribed_channels_json, const HighlightedChannel* channel_info, const bool subbed_to_channel)
{
    if ((subscribed_channels_json == NULL) || (channel_info == NULL)) return;

    cJSON* channels = cJSON_GetObjectItem(subscribed_channels_json, SUBSCRIBED_CHANNELS_ARRAY);
    if (valid_cjson_array(channels) == false) {
        printf("update_subscription:%s is not valid array object\n", SUBSCRIBED_CHANNELS_ARRAY);
        return;
    }

    if (subbed_to_channel) {
        int i = find_array_item_by_id(channels, channel_info->id, SUBSCRIBED_CHANNEL_ID_PATH);
        if (i >= 0) {
            cJSON* to_del = cJSON_DetachItemFromArray(channels, i);
            cJSON_Delete(to_del); to_del = NULL;
        }
    }

    else if (cJSON_GetArraySize(channels) < MAX_SUBSCRIBED_CHANNELS) {
        cJSON* channel = cJSON_CreateObject(); 

        cJSON_AddStringToObject(channel, "thumbnail_path", channel_info->thumbnail_path);
        cJSON_AddStringToObject(channel, "title", channel_info->name);
        cJSON_AddStringToObject(channel, "id", channel_info->id);
        cJSON_AddStringToObject(channel, "subscriber_count", channel_info->subscriber_count);

        cJSON* subscribedChannelRenderer = cJSON_CreateObject();
        cJSON_AddItemToObject(subscribedChannelRenderer, "subscribedChannelRenderer", channel);

        cJSON_AddItemToArray(channels, subscribedChannelRenderer);
    }
}

void draw_highlighted_channel(const Rectangle container, const Ui* ui, cJSON* subscribed_channels_json, const HighlightedChannel* highlighted_channel, bool* subbed_to_channel)
{
    DrawRectangleLinesEx(container, 1, GRAY);
    
    if ((ui == NULL) || (highlighted_channel == NULL) || (highlighted_channel->cached == NULL) || (highlighted_channel->id[0] == '\0')) return;

    const Vector2 dim = media_type_to_thumbnail_dim(MEDIA_TYPE_CHANNEL);

    if (cached_texture_is_ready(highlighted_channel->cached)) {
        start_timer(&highlighted_channel->cached->timer, CACHED_TEXTURE_LIFETIME);
        DrawTextureEx(highlighted_channel->cached->thumbnail, (Vector2){container.x + ui->padding, container.y + ui->padding}, 0.0f, 1.0f, RAYWHITE);
    }

    const Rectangle name_bounds = {
        .x = container.x + dim.x + ui->padding,
        .y = container.y,
        .width = container.width - dim.x,
        .height = container.height / 2.0f, 
    };

    DrawTextEx(ui->font, highlighted_channel->name, (Vector2){name_bounds.x + ui->padding, name_bounds.y + ui->padding}, 12, ui->spacing, BLACK);
    
    const Rectangle subtext_bounds = {
        .x = container.x + dim.x + ui->padding,
        .y = name_bounds.y + name_bounds.height,
        .width = container.width - dim.x,
        .height = container.height / 2.0f, 
    };

    DrawTextEx(ui->font, highlighted_channel->subscriber_count, (Vector2){subtext_bounds.x + ui->padding, subtext_bounds.y + ui->padding}, 12, ui->spacing, BLACK);

    const float button_width = 75;
    const Rectangle subscribe_button_bounds = {
        .x = container.x + dim.x + (container.width / 2.0f) - ui->padding,
        .y = container.y + name_bounds.height,
        .width = button_width,
        .height = 25,
    };

    const char* button_text = (*subbed_to_channel) ? "Unsubscribe" : "Subscribe";

    if (GuiButton(subscribe_button_bounds, button_text)) {
        update_subscription(subscribed_channels_json, highlighted_channel, (*subbed_to_channel));
        (*subbed_to_channel) = !(*subbed_to_channel);
    }
}

typedef struct
{
    Query query;
    TextureCacheEntry** texture_cache;
    cJSON* subscribed_channels_json;
    HighlightedChannel* channel;
    List* raw_thumbnail_queue;
    bool* subbed_to_channel;
    List* results;
} ParseChannelArgs;

void* parse_channel(void* args)
{
    ParseChannelArgs* targs = (ParseChannelArgs*) args;
    if ((targs == NULL) ||
        (targs->texture_cache == NULL) || 
        (targs->channel == NULL) ||
        (targs->raw_thumbnail_queue == NULL) ||
        (targs->subbed_to_channel == NULL) ||
        (targs->results == NULL)) {
        printf("parse_channel: invalid input(s)\n");
        return NULL;
    }
    
    Connection* conn = &youtube_pool.connections[youtube_pool.current_conn]; 

    HttpsRequest req = configure_post_request(targs->query, conn->host);
    if (valid_post_request(req) == false) {
        printf("parse_channel: invalid post request\n");
        if (req.payload) {
            free(req.payload); req.payload = NULL;
        }
        free(targs); targs = NULL;
        return NULL;
    }

    cJSON* json = get_json_response(&req, ssl_ctx, conn);
    if (json == NULL) {
        printf("parse_channel: 'targs' is null\n");
        if (req.payload) {
            free(req.payload); req.payload = NULL;
        }
        free(targs); targs = NULL;
        return NULL;
    }

    if (req.payload) {
        free(req.payload); req.payload = NULL;
    }

    const QueryType query_type = targs->query.type;
    const QueryAttribute query_attr = targs->query.attr;
    
    pthread_mutex_lock(&targs->results->mutex);
    create_results_from_json(json, targs->results, query_type, query_attr, false);
    pthread_mutex_unlock(&targs->results->mutex);

    get_continuation_token(json, query_type, query_attr);

    if (query_attr == QUERTY_ATTR_APPEND) {
        goto clean;
    }

    strncpy(targs->channel->id, targs->query.focused_id, sizeof(targs->channel->id) - 1);
    targs->channel->id[sizeof(targs->channel->id) - 1] = '\0';
    
    const char* title_path = ".header.pageHeaderRenderer.pageTitle";
    
    if (assign_string_from_path(json, title_path, targs->channel->name, sizeof(targs->channel->name)) == false) {
        printf("parse_channel: failed to assign channel title\n");
    }

    const char* subscriber_count_path = ".header.pageHeaderRenderer.content.pageHeaderViewModel.metadata.contentMetadataViewModel.metadataRows[1].metadataParts[0].text.content";
    
    if (assign_string_from_path(json, subscriber_count_path, targs->channel->subscriber_count, sizeof(targs->channel->subscriber_count)) == false) {
        printf("parse_channel: failed to assign sub count\n");
    }
    
    const char* thumbnail_path = ".header.pageHeaderRenderer.content.pageHeaderViewModel.image.decoratedAvatarViewModel.avatar.avatarViewModel.image.sources[0].url";
    
    const cJSON* thumbnail_url_item = cjson_pointer_get(json, thumbnail_path);
    if (valid_cjson_string(thumbnail_url_item) == false) {
        printf("parse_channel: failed to assign thumbnail path\n");
        targs->channel->thumbnail_loaded = false;
        targs->channel->thumbnail_path[0] = '\0';
        targs->channel->cached = NULL;
        goto clean;
    }

    // channels have two potential path prefixes
    const char* path1 = strstr(thumbnail_url_item->valuestring, "/ytc");
    const char* path2 = strrchr(thumbnail_url_item->valuestring, '/');

    if ((path1 == NULL) && (path2 == NULL)) {
        printf("parse_channel: channel thumbnail path does not start with '/' or '/ytc'\n");
        goto clean;
    }

    strncpy(targs->channel->thumbnail_path, path1 ? path1 : path2, sizeof(targs->channel->thumbnail_path) - 1);
    targs->channel->thumbnail_path[sizeof(targs->channel->thumbnail_path) - 1] = '\0';
    
    (*targs->subbed_to_channel) = is_subbed_to_channel(targs->subscribed_channels_json, targs->channel->id);
    
    TextureCacheEntry* cached = find_cached_thumbnail(targs->channel->id, targs->texture_cache);
    if (cached) {
        targs->channel->thumbnail_loaded = true;
        targs->channel->cached = cached;
    }

    else {
        LoadThumbnailArgs* thumb_args = malloc(sizeof(LoadThumbnailArgs));
        if (thumb_args == NULL) {
            printf("parse_channel: 'thumb_args' is null\n");
            targs->channel->thumbnail_loaded = false;
            targs->channel->thumbnail_path[0] = '\0';
            targs->channel->cached = NULL;
            goto clean;
        }

        thumb_args->media_type = MEDIA_TYPE_CHANNEL;
        thumb_args->thumbnail_queue = targs->raw_thumbnail_queue;
        snprintf(thumb_args->id, sizeof(thumb_args->id), "%s", targs->channel->id);
        snprintf(thumb_args->thumbnail_path, sizeof(thumb_args->thumbnail_path), "%s", targs->channel->thumbnail_path);

        load_thumbnail(thumb_args);

        targs->channel->thumbnail_loaded = false;
    }

    SetWindowTitle(TextFormat("[Uploads from %s] - metube", targs->channel->name));

    clean: 
        cJSON_Delete(json); json = NULL;
        free(targs); targs = NULL;
        return NULL;
}

bool like_video(cJSON* liked_videos_json, const SearchResult* liked_video)
{
    if ((liked_videos_json == NULL) || (liked_video == NULL)) return false;

    cJSON* videos = cjson_pointer_get(liked_videos_json, LIKED_VIDEOS_ARRAY);
    if (valid_cjson_array(videos) == false) {
        printf("like_video:%s is an invalid cJSON* array object\n", LIKED_VIDEOS_ARRAY);
        return false;
    }

    int found_index = find_array_item_by_id(videos, liked_video->id, LIKED_VIDEO_ID_PATH);

    if ((found_index < 0) && (cJSON_GetArraySize(videos) < MAX_LIKED_VIDEOS)) {
        cJSON* obj = init_video_json_object(liked_video);
        if (obj == NULL) {
            printf("like_video: 'obj' is null\n");
            return false;
        }

        cJSON* likedVideoRenderer = cJSON_CreateObject();
        cJSON_AddItemToObject(likedVideoRenderer, "likedVideoRenderer", obj);
        
        cJSON_InsertItemInArray(videos, 0, likedVideoRenderer);
    }
    
    return true;
}

// press 'r' to update connection status rather than checking every time
// update the title
// disable all query elements if the internet is down
// need to have some sort of timeout if they decide to turn wifi off before checking 

int main()
{
    TextureCacheEntry* cached_thumbnails = NULL;
    List thumbnail_queue = list_init();
    List task_queue = list_init();
    List results = list_init();
    pthread_t thread_pool[MAX_THREADS]; init_thread_pool(MAX_THREADS, thread_pool, worker_thread_funct, &task_queue);
    
    ssl_ctx = SSL_CTX_new(TLS_client_method());
    if (ssl_ctx == NULL) {
        printf("error initalizing SSL_CTX object\n");
        return 1;
    } 

    youtube_pool = init_connection_pool("www.youtube.com");
    video_thumbnail_pool = init_connection_pool(media_type_to_thumbnail_host(MEDIA_TYPE_VIDEO)); // playlists, videos, shorts, and live videos all share the same host
    channel_thumbnail_pool = init_connection_pool(media_type_to_thumbnail_host(MEDIA_TYPE_CHANNEL));

    cJSON* watch_history_json = file_exists(WATCH_HISTORY_FILE) ? 
                                parse_json_file(WATCH_HISTORY_FILE) : 
                                create_empty_array_object(WATCH_HISTORY_ARRAY);
    
    if (watch_history_json == NULL) {
        printf("failed to create watched history json object\n");
        goto cleanup;
    }

    cJSON* subscribed_channels_json = file_exists(SUBSCRIPTIONS_FILE) ?
                                      parse_json_file(SUBSCRIPTIONS_FILE) : 
                                      create_empty_array_object(SUBSCRIBED_CHANNELS_ARRAY);
    
    if (subscribed_channels_json == NULL) {
        printf("failed to create subscribed channels json obj\n");
        goto cleanup;
    }

    cJSON* liked_videos_json = file_exists(LIKED_VIDEOS_FILE) ? 
                               parse_json_file(LIKED_VIDEOS_FILE) : 
                               create_empty_array_object(LIKED_VIDEOS_ARRAY);

    if (liked_videos_json == NULL) {
        printf("failed to create liked videos json object\n");
        goto cleanup;
    }

    // when true, the application starts the search process
    bool search = false;
    bool edit_mode = false;
    QueryType last_search_type = -1;
    char last_search_query[512] = {0};

    Vector2 search_result_scrollbar_pos = { 10, 10 };

    // the current_query that the user has constructed
    Query query = {
        .allow_youtube_shorts = true,
        .string = "",
        .media = MEDIA_TYPE_ANY,
        .sort = SORT_TYPE_RELEVANCE,
        .type = QUERY_TYPE_USER_INPUT,
        .attr = QUERY_ATTR_REPLACE,
    };

    init_app();

    Ui ui;
    ui.font = GetFontDefault();
    ui.padding = 5;
    ui.spacing = 2;
    ui.word_wrap = true;

    Vector2 video_desc_scrollbar_pos = { 10, 10 };
    
    bool load_video_information = false;
    HighlightedVideo highlighted_video = {0};
    
    bool view_watch_history = false;
    bool view_subscribed_channels = false;
    bool view_liked_videos = false;
    
    bool subbed_to_channel = false;
    bool load_channel_information = false;
    HighlightedChannel highlighted_channel = {0};
    
    while (!WindowShouldClose())
    {
        if (HASH_COUNT(cached_thumbnails) > 0) {
            remove_expired_thumbnails(&cached_thumbnails);
        }

        if (thumbnail_queue.count > 0) {
            pthread_mutex_lock(&thumbnail_queue.mutex);
            process_thumbnail_queue(&thumbnail_queue, &cached_thumbnails);
            pthread_mutex_unlock(&thumbnail_queue.mutex);
        }

        if (load_video_information) {
            load_video_information = false;

            Connection* conn = &youtube_pool.connections[youtube_pool.current_conn];

            if (queue_focused_video_task(query, &task_queue, conn, &highlighted_video) == false) {
                printf("failed to queue focused video task\n");
            }
        }

        if (search) {
            search = false;

            list_free(&thumbnail_queue); thumbnail_queue = list_init();

            // evade bot detection
            if (strcmp(last_search_query, query.string) == 0) {
                cycle_connection(&youtube_pool);
            }

            last_search_type = query.type;
            strncpy(last_search_query, query.string, sizeof(last_search_query) - 1);

            SearchThreadArgs* targs = malloc(sizeof(SearchThreadArgs));
            if (targs == NULL) {
                printf("'targs' is null\n");
                goto cleanup;
            }

            targs->query = query;
            targs->search_results = &results;

            if (launch_task(&task_queue, targs, get_results_from_query) == false) {
                printf("failed to launch task: 'get_results_from_query'\n");
                free(targs); targs = NULL;
            }
        }

        if (view_watch_history) {
            view_watch_history = false;

            if (continuation_token) {
                free(continuation_token); continuation_token = NULL;
            }

            pthread_mutex_lock(&results.mutex);
            create_results_from_json(watch_history_json, &results, QUERY_TYPE_WATCH_HISTORY, QUERY_ATTR_REPLACE, true);
            pthread_mutex_unlock(&results.mutex);
       
            SetWindowTitle("[History] - metube");
        }

        if (view_subscribed_channels) {
            view_subscribed_channels = false;

            if (continuation_token) {
                free(continuation_token); continuation_token = NULL;
            }

            pthread_mutex_lock(&results.mutex);
            create_results_from_json(subscribed_channels_json, &results, QUERY_TYPE_VIEW_SUBSCRIBED_CHANNELS, QUERY_ATTR_REPLACE, false);
            pthread_mutex_unlock(&results.mutex);

            SetWindowTitle("[Subscriptions] - metube");
        }

        if (view_liked_videos) {
            view_liked_videos = false;

            if (continuation_token) {
                free(continuation_token); continuation_token = NULL;
            }

            pthread_mutex_lock(&results.mutex);
            create_results_from_json(liked_videos_json, &results, QUERY_TYPE_VIEW_LIKED_VIDEOS, QUERY_ATTR_REPLACE, true);
            pthread_mutex_unlock(&results.mutex);

            SetWindowTitle("[Liked Videos] - metube");
        }

        if (load_channel_information) {
            load_channel_information = false;
            last_search_type = query.type;

            ParseChannelArgs* targs = malloc(sizeof(ParseChannelArgs));
            if (targs) {
                targs->query = query;
                targs->results = &results;
                targs->channel = &highlighted_channel;
                targs->texture_cache = &cached_thumbnails;
                targs->subbed_to_channel = &subbed_to_channel;
                targs->raw_thumbnail_queue = &thumbnail_queue;
                targs->subscribed_channels_json = subscribed_channels_json;
                
                if (launch_task(&task_queue, targs, parse_channel) == false) {
                    printf("failed to launch parse_channel task\n");
                    free(targs); targs = NULL;
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
                    search = true;
                    query.attr = QUERY_ATTR_REPLACE;
                    query.type = QUERY_TYPE_USER_INPUT;
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
                query.attr = QUERY_ATTR_REPLACE;
                query.type = QUERY_TYPE_TRENDING;
            }

            const Rectangle related_videos_button_bounds = {
                .x = trending_button_bounds.x + trending_button_bounds.width + ui.padding,
                .y = ui.padding,
                .width = 85,
                .height = 25
            };

            if (highlighted_video.info.id[0] == '\0') {
                GuiSetState(STATE_DISABLED);
            }
            
            if (GuiButton(related_videos_button_bounds, "Related Videos")) {
                search = true;
                query.attr = QUERY_ATTR_REPLACE;
                query.type = QUERY_TYPE_RELATED;
                strncpy(query.focused_id, highlighted_video.info.id, sizeof(query.focused_id) - 1);
                query.focused_id[sizeof(query.focused_id) - 1] = '\0';
                SetWindowTitle(TextFormat("[Related:%s(loading)] - metube", query.focused_id));                
            }

            GuiSetState(STATE_NORMAL);

            if (highlighted_video.info.id[0] == '\0') {
                GuiSetState(STATE_DISABLED);
            }

            const Rectangle users_videos_button_bounds = {
                .x = related_videos_button_bounds.x + related_videos_button_bounds.width + ui.padding,
                .y = ui.padding,
                .width = 85,
                .height = 25,
            };

            if (GuiButton(users_videos_button_bounds, "User's Videos")) {
                load_channel_information = true;
                query.attr = QUERY_ATTR_REPLACE;
                query.type = QUERY_TYPE_VIEW_CHANNEL;

                strncpy(query.focused_id, highlighted_video.info.authorId, sizeof(query.focused_id) - 1);
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
                view_watch_history = true;
                query.attr = QUERY_ATTR_REPLACE;
                query.type = QUERY_TYPE_WATCH_HISTORY;
            }

            const Rectangle view_subscriptions_button = {
                .x = watch_history_button.x + watch_history_button.width + ui.padding,
                .y = ui.padding,
                .width = 80,
                .height = 25,
            };

            if (GuiButton(view_subscriptions_button, "Subscriptions")) {
                view_subscribed_channels = true;
                query.attr = QUERY_ATTR_REPLACE;
                query.type = QUERY_TYPE_VIEW_SUBSCRIBED_CHANNELS;
            }

            const Rectangle like_video_button_bounds = {
                .x = view_subscriptions_button.x + view_subscriptions_button.width + ui.padding,
                .y = ui.padding,
                .width = 30,
                .height = 25,
            };

            if (highlighted_video.info.id[0] == '\0') GuiSetState(STATE_DISABLED);

            if (GuiButton(like_video_button_bounds, "Like")) {
                if (like_video(liked_videos_json, &highlighted_video.info) == false) {
                    printf("failed to like %s\n", highlighted_video.info.title);
                }
            }

            GuiSetState(STATE_NORMAL);

            const Rectangle view_liked_videos_button_bounds = {
                .x = like_video_button_bounds.x + like_video_button_bounds.width + ui.padding,
                .y = ui.padding,
                .width = 70,
                .height = 25
            };

            if (GuiButton(view_liked_videos_button_bounds, "Liked Videos")) {
                view_liked_videos = true;
            }

            const Rectangle filter_window_bounds = {
                .x = ui.padding, 
                .y = search_button_bounds.y + search_button_bounds.height + ui.padding, 
                .width = search_bar_bounds.width, 
                .height = 75
            };

            draw_filter_window(&query, filter_window_bounds, ui.font, ui.padding);

            const float focused_channel_min_y = 170;
            const float focused_channel_height = 80;
            const Rectangle focused_channel_bounds = {
                .x = ui.padding,
                .y = fmax(focused_channel_min_y, GetScreenHeight() - focused_channel_height - ui.padding),
                .height = focused_channel_height,
                .width = 350,
            };

            if ((highlighted_channel.thumbnail_loaded == false) && (highlighted_channel.thumbnail_path[0] != '\0')) { 
                highlighted_channel.thumbnail_loaded = true;
                highlighted_channel.cached = find_cached_thumbnail(highlighted_channel.id, &cached_thumbnails);
            }

            draw_highlighted_channel(focused_channel_bounds, &ui, subscribed_channels_json, &highlighted_channel, &subbed_to_channel);

            const Rectangle scroll_window_bounds = { 
                .x = ui.padding, 
                .y = search_bar_bounds.y + search_bar_bounds.height + filter_window_bounds.height + (ui.padding * 2), 
                .width = search_bar_bounds.width, 
                .height = GetScreenHeight() - scroll_window_bounds.y - focused_channel_height - (ui.padding * 2), 
            };

            pthread_mutex_lock(&results.mutex); 

            const bool load_more_button_visible = (continuation_token) && (continuation_token[0] != '\0');
            
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

            for (Node* node = results.head; node; node = node->next, i++, container_y += container_height) {
                SearchResult* search_result = (SearchResult*) node->content;

                container.y = container_y + search_result_scrollbar_pos.y;

                if (CheckCollisionRecs(scissor_rect, container) == false) {
                    continue;
                }

                const bool result_is_highlighted = strcmp(search_result->id, highlighted_video.info.id) == 0;

                const Color container_color = result_is_highlighted ? 
                                              BLUE :
                                              ((i % 2) ? WHITE : RAYWHITE);

                Texture2D thumbnail = (Texture2D){0};

                TextureCacheEntry *cached = find_cached_thumbnail(search_result->id, &cached_thumbnails);
                if (cached_texture_is_ready(cached)) {
                    thumbnail = cached->thumbnail;
                    start_timer(&cached->timer, CACHED_TEXTURE_LIFETIME); // refresh lifetime
                }

                else if ((search_result->thumbnail_loaded == false) && (search_result->thumbnail_path[0] != '\0')) {
                    search_result->thumbnail_loaded = true;

                    if (queue_thumbnail_load(search_result->id, search_result->thumbnail_path, &task_queue, &thumbnail_queue, search_result->media_type)) {
                        ConnectionPool* pool = media_type_to_pool(search_result->media_type);
                        cycle_connection(pool);
                    }
                }

                draw_search_result(search_result, thumbnail, container, container_color, ui);

                if ((CheckCollisionPointRec(GetMousePosition(), container)) && 
                    (CheckCollisionPointRec(GetMousePosition(), scissor_rect)) &&
                    (IsMouseButtonPressed(MOUSE_BUTTON_LEFT))) {
                    query.attr = QUERY_ATTR_REPLACE;

                    strncpy(query.focused_id, search_result->id, sizeof(query.focused_id) - 1);
                    query.focused_id[sizeof(query.focused_id) - 1] = '\0';

                    switch (search_result->media_type) {
                        case MEDIA_TYPE_LIVE:
                        case MEDIA_TYPE_SHORT:
                        case MEDIA_TYPE_VIDEO:
                            if (result_is_highlighted == false) {
                                load_video_information = true;
                                query.type = QUERY_TYPE_VIDEO_FOCUS;
                                
                                memcpy(&highlighted_video.info, search_result, sizeof(SearchResult));

                                update_watch_history(watch_history_json, search_result);
                            }
                            break;
                        case MEDIA_TYPE_PLAYLIST:
                            search = true;
                            query.type = QUERY_TYPE_VIEW_PLAYLIST;
                            SetWindowTitle(TextFormat("[Playlist:%s(loading)] - metube", query.focused_id));
                            break;
                        case MEDIA_TYPE_CHANNEL:
                            load_channel_information = true;
                            query.type = QUERY_TYPE_VIEW_CHANNEL;
                            SetWindowTitle(TextFormat("[Channel:%s(loading)] - metube", query.focused_id));
                            break;
                        default:
                            printf("CRITICAL: invalid media type pressed\n");
                            goto cleanup;
                    }
                }
            }

            pthread_mutex_unlock(&results.mutex);

            const Rectangle load_more_button_bounds = {
                .x = container.x,
                .y = container.y + container_height,
                .width = container.width,
                .height = container_height,
            };

            if (load_more_button_visible && GuiButton(load_more_button_bounds, "LOAD MORE")) {
                if (last_search_type == QUERY_TYPE_VIEW_CHANNEL) load_channel_information = true;
                else search = true;
                query.type = last_search_type;
                query.attr = QUERTY_ATTR_APPEND;
            }
            
            EndScissorMode();

            const Rectangle focused_video_bounds = {
                .x = scroll_window_bounds.x + scroll_window_bounds.width + ui.padding,
                .y = filter_window_bounds.y,
                .width = GetScreenWidth() - focused_video_bounds.x,
                .height = GetScreenHeight() - focused_video_bounds.y - ui.padding,
            };
            
            draw_highlighted_video(focused_video_bounds, ui, &video_desc_scrollbar_pos, &highlighted_video);

            DrawFPS(GetScreenWidth() - 75, ui.padding);

        EndDrawing();
    }
    
    cleanup:
        // free worker thread stuff
        application_running = false;
        pthread_cond_broadcast(&task_queue.cond);
        free_thread_pool(MAX_THREADS, thread_pool);
        list_free(&task_queue);         
        
        // deinit app
        UnloadFont(ui.font);
        list_free(&results);
        list_free(&thumbnail_queue);
        free_cached_textures(&cached_thumbnails);
        
        if (watch_history_json) {
            write_json_file(watch_history_json, WATCH_HISTORY_FILE);
            cJSON_Delete(watch_history_json); watch_history_json = NULL;
        }

        if (subscribed_channels_json) {
            write_json_file(subscribed_channels_json, SUBSCRIPTIONS_FILE);
            cJSON_Delete(subscribed_channels_json); subscribed_channels_json = NULL;
        }

        if (liked_videos_json) {
            write_json_file(liked_videos_json, LIKED_VIDEOS_FILE);
            cJSON_Delete(liked_videos_json); liked_videos_json = NULL;
        }

        if (continuation_token) {
            free(continuation_token); continuation_token = NULL;
        } 
        
        if (highlighted_video.description) {
            free(highlighted_video.description); highlighted_video.description = NULL;
        }
        
        // ssl stuff
        if (ssl_ctx) SSL_CTX_free(ssl_ctx);
        free_connection_pool(&youtube_pool);
        free_connection_pool(&video_thumbnail_pool);
        free_connection_pool(&channel_thumbnail_pool);
        
        CloseWindow();
        
        return 0;
}

// stuff to do:
    // able to add videos to created playlist
    // fonts for L.O.T.E.
    // handle connecticity issues (no wifi on startup, changing connections, etc.)
    // goto's for redundant cleanups
    // set all ptrs to NULL after freeing them
    // thumbnail frames from video click
    // better create_results_from_json?
    // move ui stuff together