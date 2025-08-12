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

#include "include/raw_thumbnail.h"
#include "include/utils.h"
#include "include/query.h"
#include "include/timer.h"
#include "include/buffer.h"
#include "include/yt_client.h"
#include "include/connection.h"
#include "include/json_utils.h"
#include "include/https_utils.h"
#include "include/texture_cache.h"

#include "include/uthash.h"
#include "include/raylib.h"
#include "arpa/inet.h"
#include "cjson/cJSON.h"
#include "openssl/ssl.h"

#define RAYGUI_IMPLEMENTATION
#include "include/raygui.h"

#define MAX_THREADS 4
#define N_CONN MAX_THREADS

#define ARRAY_NAME "array"

#define OBJ_ID_PATH           "id" 
#define OBJ_THUMBNAIL_PATH   "thumbnail_path"
#define OBJ_TITLE_PATH        "title"
#define OBJ_AUTHOR_ID_PATH    "authorId"
#define OBJ_SUB_COUNT_PATH    "subscriber_count"
#define OBJ_UPLOAD_DATE_PATH  "date_published"
#define OBJ_VIDEO_COUNT_PATH  "video_count"
#define OBJ_VIEW_COUNT_PATH   "view_count"
#define OBJ_VIDEO_LENGTH_PATH "duration"
#define OBJ_MEDIA_TYPE_PATH   "media_type"
#define OBJ_TIME_ADDED_PATH   "time_added"

#define MAX_ITEM_SIZE 20

#define LIKED_VIDEOS_FILE "liked_videos.json"
#define SUBSCRIPTIONS_FILE "subscriptions.json"
#define WATCH_HISTORY_FILE "watch_history.json"

#define MEDIUM_THUMBNAIL_VIDEO_RESOLUTION "mqdefault"

#define HTTPS_PORT "443"

static SSL_CTX* ssl_ctx = NULL;
static char* continuation_token = NULL;
static const char* api_key = "AIzaSyAO_FJ2SlqU8Q4STEHLGCilw_Y9_11qcW8";

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

bool video_is_youtube_short(cJSON *videoRenderer) 
{
    const char* path = ".navigationEndpoint.commandMetadata.webCommandMetadata.url";
    const cJSON* url = cjson_pointer_get(videoRenderer, path); 
    if (json_string_is_valid(url)) {
        return strstr(url->valuestring, "/shorts");
    }

    return false;
}

bool video_is_live(cJSON* videoRenderer)
{
    const char* path = ".badges[0].metadataBadgeRenderer.label";
    const cJSON* label = cjson_pointer_get(videoRenderer, path);
    if (json_string_is_valid(label)) {
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
    if (json_string_is_valid(channelThumbnailLink)) {
        // the path either starts with '/ytc', or just '/'
        const char* path1 = strstr(channelThumbnailLink->valuestring, "/ytc");
        const char* path2 = strrchr(channelThumbnailLink->valuestring, '/');
        snprintf(channel->thumbnail_path, sizeof(channel->thumbnail_path), "%s", (path1 ? path1 : path2));
    }
}

void parse_playlist_result(cJSON *lockupViewModel, SearchResult *playlist)
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
    Connection* conn;
    MediaType media_type;
} LoadThumbnailArgs;

void* load_thumbnail(void* args)
{
    LoadThumbnailArgs* targs = (LoadThumbnailArgs*) args;
    if (targs == NULL) {
        printf("load_thumbnail: args are null\n");
        return NULL;
    }

    HttpsRequest req = {0};
    if (configure_get_header(req.header, sizeof(req.header), targs->conn->host, targs->thumbnail_path, USER_AGENT, CONNECTION_STATUS, HTTP_PROTOCOL_VER) == false) {
        printf("load_thumbnail: req header was truncated\n");
        goto clean;
    }

    Buffer res = get_https_response(req, ssl_ctx, targs->conn, HTTP_PROTOCOL_VER);
    if (buffer_is_ready(&res) == false) {
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
    Connection* conn;
    List* search_results;
} SearchThreadArgs;

const char* get_results_list_path(const QueryType search_type, const QueryAttribute search_attr)
{
    switch (search_type) {
        case QUERY_TYPE_USER_INPUT: 
            if (search_attr == QUERY_ATTR_REPLACE) return ".contents.twoColumnSearchResultsRenderer.primaryContents.sectionListRenderer.contents[0].itemSectionRenderer.contents";
            if (search_attr == QUERTY_ATTR_APPEND) return ".onResponseReceivedCommands[0].appendContinuationItemsAction.continuationItems[0].itemSectionRenderer.contents";
        case QUERY_TYPE_VIEW_RELATED: 
            if (search_attr == QUERY_ATTR_REPLACE) return "contents.twoColumnWatchNextResults.secondaryResults.secondaryResults.results";
            if (search_attr == QUERTY_ATTR_APPEND) return ".onResponseReceivedEndpoints[0].appendContinuationItemsAction.continuationItems";
        case QUERY_TYPE_VIEW_PLAYLIST: 
            if (search_attr == QUERY_ATTR_REPLACE) return ".contents.twoColumnBrowseResultsRenderer.tabs[0].tabRenderer.content.sectionListRenderer.contents[0].itemSectionRenderer.contents[0].playlistVideoListRenderer.contents";
            if (search_attr == QUERTY_ATTR_APPEND) return ".onResponseReceivedActions[0].appendContinuationItemsAction.continuationItems";
        case QUERY_TYPE_VIEW_CHANNEL: 
            if (search_attr == QUERY_ATTR_REPLACE) return ".contents.twoColumnBrowseResultsRenderer.tabs[1].tabRenderer.content.richGridRenderer.contents";
            if (search_attr == QUERTY_ATTR_APPEND) return ".onResponseReceivedActions[0].appendContinuationItemsAction.continuationItems";
        case QUERY_TYPE_VIEW_TRENDING:                  return ".contents.twoColumnBrowseResultsRenderer.tabs[0].tabRenderer.content.sectionListRenderer.contents[2].itemSectionRenderer.contents[0].shelfRenderer.content.expandedShelfContentsRenderer.items";
        case QUERY_TYPE_VIEW_WATCH_HISTORY:             
        case QUERY_TYPE_VIEW_LIKED_VIDEOS:         
        case QUERY_TYPE_VIEW_SUBSCRIBED_CHANNELS:  return ARRAY_NAME;
        case QUERY_TYPE_VIEW_VIDEO:               return NULL;
    }

    return NULL;
}

void parse_user_data(cJSON* user_data, SearchResult* dest) 
{
    const cJSON* media_type = cjson_pointer_get(user_data, OBJ_MEDIA_TYPE_PATH);
    if (cJSON_IsNumber(media_type) == false) {
        printf("parse_user_data: user_data element has invalid media type\n");
        dest->media_type = MEDIA_TYPE_UNDF;
        return;
    }
    
    dest->media_type = (MediaType) media_type->valueint;

    assign_string_from_path(user_data, OBJ_ID_PATH, dest->id, sizeof(dest->id));
    assign_string_from_path(user_data, OBJ_TITLE_PATH, dest->title, sizeof(dest->title));
    assign_string_from_path(user_data, OBJ_THUMBNAIL_PATH, dest->thumbnail_path, sizeof(dest->thumbnail_path));

    switch (dest->media_type) {
        case MEDIA_TYPE_LIVE:
        case MEDIA_TYPE_SHORT:
        case MEDIA_TYPE_VIDEO:
            assign_string_from_path(user_data, OBJ_AUTHOR_ID_PATH, dest->authorId, sizeof(dest->authorId));
            assign_string_from_path(user_data, OBJ_VIDEO_LENGTH_PATH, dest->duration, sizeof(dest->duration));
            assign_string_from_path(user_data, OBJ_VIEW_COUNT_PATH, dest->view_count, sizeof(dest->view_count));
            assign_string_from_path(user_data, OBJ_UPLOAD_DATE_PATH, dest->date_published, sizeof(dest->date_published));
            break;
        case MEDIA_TYPE_CHANNEL:
            assign_string_from_path(user_data, OBJ_SUB_COUNT_PATH, dest->subscriber_count, sizeof(dest->subscriber_count));
            break;
        case MEDIA_TYPE_PLAYLIST:
            assign_string_from_path(user_data, OBJ_VIDEO_COUNT_PATH, dest->video_count, sizeof(dest->video_count));
            break;
        case MEDIA_TYPE_ANY:
        case MEDIA_TYPE_UNDF:
            break;
    }
}

int create_results_from_json(cJSON* json, List* results, const QueryType search_type, const QueryAttribute search_attr, const bool allow_youtube_shorts)
{
    if ((json == NULL || (results == NULL))) return -1;

    const char* path = get_results_list_path(search_type, search_attr); 

    cJSON* results_array = cjson_pointer_get(json, path);
    if (cJSON_IsArray(results_array) == false) {
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
        cJSON* channelRenderer           = cjson_pointer_get(item, ".channelRenderer");  
        cJSON* lockupViewModel           = cjson_pointer_get(item, ".lockupViewModel");   
        
        if      (videoRenderer)             parse_video(videoRenderer, author_id, allow_youtube_shorts, search_result);
        else if (richItemRenderer)          parse_video(richItemRenderer, author_id, allow_youtube_shorts,search_result);
        else if (playlistVideoRenderer)     parse_playlist_video(playlistVideoRenderer, search_result);
        else if (channelRenderer)           parse_channel_result(channelRenderer, search_result);
        else if (lockupViewModel) {
            if (search_type == QUERY_TYPE_VIEW_RELATED) 
                parse_related_video(lockupViewModel, search_result);
            else 
                parse_playlist_result(lockupViewModel, search_result);
        }

        else parse_user_data(item, search_result);

        if (search_result->media_type != MEDIA_TYPE_UNDF) {
            elements_added++; list_append(results, node);
        }

        else node_free(node);
    }

    if (search_attr == QUERY_ATTR_REPLACE) {
        for (int i = 0; results->head && (i < old_size); i++) {
            node_free(list_dequeue(results));
        }
    }

    return elements_added;
}

const char* get_continuation_token_path(const QueryType search_type, const QueryAttribute search_attr)
{
    switch (search_type) {
        case QUERY_TYPE_USER_INPUT:
            if (search_attr == QUERY_ATTR_REPLACE) return ".contents.twoColumnSearchResultsRenderer.primaryContents.sectionListRenderer.contents[1].continuationItemRenderer.continuationEndpoint.continuationCommand.token";
            if (search_attr == QUERTY_ATTR_APPEND) return ".onResponseReceivedCommands[0].appendContinuationItemsAction.continuationItems[1].continuationItemRenderer.continuationEndpoint.continuationCommand.token";
        case QUERY_TYPE_VIEW_RELATED:
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
    if (json_string_is_valid(token_obj) == false) {
        printf("get_continuation_token: 'token_obj' is invalid (path: %s)\n", continuation_path);
        return;
    }

    if ((continuation_token = strdup(token_obj->valuestring)) == NULL) {
        printf("get_continuation_token: strdup failed\n");
    }
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

    HttpsRequest req = configure_post_request(targs->query, targs->conn->host, api_key, continuation_token);
    if (post_request_is_ready(req) == false) {
        printf("get_results_from_query: invalid post req configured\n");
        free(targs); targs = NULL;
        return NULL;
    }

    cJSON* json_res = get_json_response(&req, ssl_ctx, targs->conn, HTTP_PROTOCOL_VER);

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

bool draw_toggle_filter(const Rectangle container, const Ui ui, const char* label_text, const char* value_text)
{
    const Color text_color = BLACK;
    const int font_size = 10;

    const char* button_text = "SWITCH";
    const float button_width = 50;
    const float button_height = container.height - (ui.padding * 2);

    const float y_level = container.y + ui.padding;

    const Vector2 label_pos = {
        .x = container.x + ui.padding,
        .y = y_level,
    };

    DrawTextEx(ui.font, label_text, label_pos,font_size, ui.spacing, text_color);
    
    const Vector2 value_pos = {
        .x = ui.padding + (container.x + (container.width / 2.0f)) - (MeasureTextEx(ui.font, value_text, font_size, ui.spacing).x / 2.0f),
        .y = y_level
    };

    DrawTextEx(ui.font, value_text, value_pos, font_size, ui.spacing, text_color);
    
    const Rectangle button_bounds = {
        .x = container.x + container.width - button_width - ui.padding,
        .y = y_level,
        .width = button_width,
        .height = button_height,
    };

    return GuiButton(button_bounds, button_text);
}

void draw_filter_window(const Rectangle container, const Ui ui, Query* query)
{
    DrawRectangleLinesEx(container, 1, GRAY);

    const SortType filterable_sort_types [] = {
        SORT_TYPE_RELEVANCE,
        SORT_TYPE_UPLOAD_DATE,
        SORT_TYPE_VIEW_COUNT,
        SORT_TYPE_RATING,
    };          

    const size_t nsorts = sizeof(filterable_sort_types) / sizeof(filterable_sort_types[0]);

    const Rectangle sort_type_bounds = {
        .x = container.x,
        .y = container.y,
        .width = container.width,
        .height = container.height / 3.0f,
    };

    const char* sort_text = sort_type_to_text(query->sort);

    if (draw_toggle_filter(sort_type_bounds, ui, "ORDER", sort_text)) {
        query->sort = bound_index_to_array((query->sort + 1), nsorts);
    }

    const MediaType filterable_media_types [] = {
        MEDIA_TYPE_ANY,
        MEDIA_TYPE_VIDEO,
        MEDIA_TYPE_CHANNEL,
        MEDIA_TYPE_PLAYLIST,
        MEDIA_TYPE_LIVE,
    };

    const size_t nmedias = sizeof(filterable_media_types) / sizeof(filterable_media_types[0]);

    const Rectangle media_type_bounds = {
        .x = container.x,
        .y = sort_type_bounds.y + sort_type_bounds.height,
        .width = container.width,
        .height = container.height / 3.0f,
    };

    const char* media_text = media_type_to_text(query->media);

    if (draw_toggle_filter(media_type_bounds, ui, "TYPE", media_text)) {
        query->media = bound_index_to_array((query->media + 1), nmedias);
    }

    const Rectangle allow_shorts_bounds = {
        .x = container.x,
        .y = media_type_bounds.y + media_type_bounds.height,
        .width = container.width,
        .height = container.height / 3.0f,
    };

    const char* allow_shorts_text = query->allow_youtube_shorts ? "YES" : "NO";

    if (draw_toggle_filter(allow_shorts_bounds, ui, "SHORTS", allow_shorts_text)) {
        query->allow_youtube_shorts = !query->allow_youtube_shorts;
    }
}

void draw_search_result(SearchResult *search_result, const Texture thumbnail, const Rectangle container,  const Color color, const Ui ui)
{
    DrawRectangleRec(container, color);

    const int font_size = 12;

    const float thumbnail_width = media_type_to_thumbnail_dim(MEDIA_TYPE_VIDEO).x;

    const Rectangle thumbnail_area = { 
        .x = container.x, 
        .y = container.y, 
        .width = thumbnail_width, 
        .height = container.height 
    };

    const Rectangle title_area = {
        .x = thumbnail_area.x + thumbnail_area.width,
        .y = thumbnail_area.y,
        .width = container.width - thumbnail_area.width,
        .height = thumbnail_area.height * 0.70f
    };

    if (search_result->title[0] != '\0') {
        DrawTextBoxed(search_result->title, padded_rectangle(ui.padding, title_area), ui, font_size, BLACK);                            
    }

    const Rectangle subtext_area = {
        .x = thumbnail_area.x + thumbnail_area.width,
        .y = title_area.y + title_area.height,
        .width = title_area.width,
        .height = container.height - title_area.height,
    };

    switch (search_result->media_type) {
        case MEDIA_TYPE_SHORT:
        case MEDIA_TYPE_VIDEO: {
            const Vector2 date_published_pos = {
                .x = subtext_area.x + ui.padding,
                .y = subtext_area.y + ui.padding,
            };

            DrawTextEx(ui.font, search_result->date_published, date_published_pos, font_size, ui.spacing, BLACK);
            
            const Vector2 view_count_pos = {
                .x = subtext_area.x + subtext_area.width - MeasureTextEx(ui.font, search_result->view_count, font_size, ui.spacing).x - ui.padding,
                .y = subtext_area.y + ui.padding,
            };

            DrawTextEx(ui.font, search_result->view_count, view_count_pos, font_size, ui.spacing, BLACK);
            
            DrawTextureEx(thumbnail, (Vector2){thumbnail_area.x, thumbnail_area.y}, 0.0f, 1.0f, WHITE);
            draw_thumbnail_subtext(thumbnail_area, ui, RAYWHITE, font_size, search_result->duration);
            break;
        }
        case MEDIA_TYPE_LIVE: {
            const Vector2 view_count_pos = {
                .x = subtext_area.x + ui.padding,
                .y = subtext_area.y + ui.padding,
            };

            DrawTextEx(ui.font, TextFormat("%s watching", search_result->view_count), view_count_pos, font_size, ui.spacing, BLACK);
            
            DrawTextureEx(thumbnail, (Vector2){thumbnail_area.x, thumbnail_area.y}, 0.0f, 1.0f, WHITE);
            draw_thumbnail_subtext(thumbnail_area, ui, RAYWHITE, 12, "LIVE");
            break;
        }
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

bool queue_thumbnail_load(List* task_queue, List* thumbnail_queue, Connection* conn, const char* search_result_id, const char* thumbnail_path,  MediaType media_type)
{
    
    if ((task_queue == NULL) || (thumbnail_queue == NULL) || (conn == NULL) || (search_result_id == NULL) || (thumbnail_path == NULL)) {
        printf("queue_thumbnail_load: invalid args\n");
        return false;
    }

    LoadThumbnailArgs* targs = malloc(sizeof(LoadThumbnailArgs));
    if (targs == NULL) {
        printf("queue_thumbnail_load: malloc returned NULL for 'targs'\n");
        return false;
    }

    targs->conn = conn;
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
        .width = fmax(20, container.width - ui.padding),
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

    res = get_https_response(targs->req, ssl_ctx, targs->conn, HTTP_PROTOCOL_VER); 
    if (buffer_is_ready(&res) == false) {
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
    if (json_string_is_valid(shortDescription)) {
        if ((targs->highlighted_video->description = strdup(shortDescription->valuestring)) == NULL) {
            printf("get_video_description: strdup failed\n");
        }
    }

    SetWindowTitle(TextFormat("[%s] - metube", targs->highlighted_video->info.title));

    cleanup:
        if (targs->req.payload) free(targs->req.payload);
        if (buffer_is_ready(&res)) buffer_free(&res);
        if (json) cJSON_Delete(json);
        if (targs) free(targs);
        return NULL;
}

cJSON* init_search_result_json(const SearchResult* result)
{
    if ((result == NULL) || (result->media_type == MEDIA_TYPE_UNDF)) return NULL;
    
    cJSON* result_json = cJSON_CreateObject();

    if (result_json) {
        cJSON_AddStringToObject(result_json, OBJ_ID_PATH, result->id);
        cJSON_AddStringToObject(result_json, OBJ_TITLE_PATH, result->title);
        cJSON_AddNumberToObject(result_json, OBJ_MEDIA_TYPE_PATH, result->media_type);
        cJSON_AddStringToObject(result_json, OBJ_THUMBNAIL_PATH, result->thumbnail_path);
        cJSON_AddNumberToObject(result_json, OBJ_TIME_ADDED_PATH, (double)time(NULL));
        
        switch (result->media_type) {
            case MEDIA_TYPE_LIVE:
            case MEDIA_TYPE_SHORT:
            case MEDIA_TYPE_VIDEO:
                cJSON_AddStringToObject(result_json, OBJ_VIDEO_LENGTH_PATH, result->duration);
                cJSON_AddStringToObject(result_json, OBJ_AUTHOR_ID_PATH, result->authorId);
                cJSON_AddStringToObject(result_json, OBJ_VIEW_COUNT_PATH, result->view_count);
                cJSON_AddStringToObject(result_json, OBJ_UPLOAD_DATE_PATH, result->date_published);
                break;
            case MEDIA_TYPE_CHANNEL:  cJSON_AddStringToObject(result_json, OBJ_SUB_COUNT_PATH, result->subscriber_count); break;
            case MEDIA_TYPE_PLAYLIST: cJSON_AddStringToObject(result_json, OBJ_VIDEO_COUNT_PATH, result->video_count); break;
            default: return NULL;
        }
    }

    return result_json;
}

void add_user_data(cJSON* data, const SearchResult* interacted_result)
{
    if ((data == NULL) || (interacted_result == NULL)) return;

    cJSON* array = cjson_pointer_get(data, ARRAY_NAME);
    if (cJSON_IsArray(array) == false) {
        printf("add_user_data: invalid array parsed\n");
        return;
    }

    cJSON* to_add = NULL;

    const int i = find_array_item(array, interacted_result->id, OBJ_ID_PATH);

    if (i >= 0) {
        to_add = cJSON_DetachItemFromArray(array, i);
        cJSON* timestamp = cJSON_CreateNumber(time(NULL));
        if (timestamp) {
            cJSON_ReplaceItemInObject(to_add, OBJ_TIME_ADDED_PATH, timestamp);
        }
    }

    else {
        to_add = init_search_result_json(interacted_result);
        if (to_add) {
            const size_t array_size = cJSON_GetArraySize(array);
            if (array_size == MAX_ITEM_SIZE) {
                cJSON* delete = cJSON_DetachItemFromArray(array, (array_size - 1));
                cJSON_Delete(delete); delete = NULL;
            }
        }
    }

    cJSON_InsertItemInArray(array, 0, to_add);
}

void delete_user_data(cJSON* data, const char* id)
{
    if ((data == NULL) || (valid_string(id) == false)) return;

    cJSON* array = cjson_pointer_get(data, ARRAY_NAME);
    if (cJSON_IsArray(array) == false) {
        fprintf(stderr, "delete_user_data: parsed json is invalid array\n");
        return;
    }

    const int i = find_array_item(array, id, OBJ_ID_PATH);
    if (i >= 0) {
        cJSON* delete = cJSON_DetachItemFromArray(array, i);
        cJSON_Delete(delete); delete = NULL;
    }
}

typedef struct
{
    SearchResult info;
    TextureCacheEntry* cached;
    bool is_subscribed;
} HighlightedChannel;

bool is_subbed_to_channel(cJSON* subscribed_channels_json, const char* id)
{
    if ((subscribed_channels_json == NULL) || (id == NULL)) return false;

    cJSON* subbed_channels = cjson_pointer_get(subscribed_channels_json, ARRAY_NAME);
    
    if (cJSON_IsArray(subbed_channels) == false) return false;

    const int found = find_array_item(subbed_channels, id, OBJ_ID_PATH);

    return (found >= 0);
}

void draw_highlighted_channel(const Rectangle container, const Ui* ui, cJSON* subscribed_channels_json, HighlightedChannel* highlighted_channel)
{
    DrawRectangleLinesEx(container, 1, GRAY);
    
    const float font_size = 17;
    const float title_size = 25;

    if ((ui == NULL) || (highlighted_channel == NULL) || (highlighted_channel->cached == NULL) || (highlighted_channel->info.id[0] == '\0')) return;

    const Vector2 dim = media_type_to_thumbnail_dim(MEDIA_TYPE_CHANNEL);

    if (texture_cache_entry_is_ready(highlighted_channel->cached)) {
        timer_start(&highlighted_channel->cached->timer, CACHED_TEXTURE_LIFETIME);
        DrawTextureEx(highlighted_channel->cached->texture, (Vector2){container.x + ui->padding, container.y + ui->padding}, 0.0f, 1.0f, RAYWHITE);
    }

    const Vector2 title_pos = {
        .x = container.x + dim.x + (ui->padding * 2),
        .y = container.y + (container.height / 3.0f) - (title_size / 2.0f),
    };

    DrawTextEx(ui->font, highlighted_channel->info.title, title_pos, title_size, ui->spacing, BLACK);
    
    const Vector2 sub_count_pos = {
        .x = container.x + dim.x + (ui->padding * 2),
        .y = container.y + container.height - MeasureTextEx(ui->font, highlighted_channel->info.subscriber_count, font_size, ui->spacing).y - ui->padding,
    };

    DrawTextEx(ui->font, highlighted_channel->info.subscriber_count, sub_count_pos, font_size, ui->spacing, BLACK);

    const float button_width = 75;
    const float button_height = 25;
    const Rectangle subscribe_button_bounds = {
        .x = container.x + container.width - button_width - ui->padding,
        .y = container.y + container.height - button_height - ui->padding,
        .width = button_width,
        .height = 25,
    };

    const char* button_text = highlighted_channel->is_subscribed ? "Unsubscribe" : "Subscribe";

    if (GuiButton(subscribe_button_bounds, button_text)) {
        if (highlighted_channel->is_subscribed)
            delete_user_data(subscribed_channels_json, highlighted_channel->info.id);
        else
            add_user_data(subscribed_channels_json, &highlighted_channel->info);
        
        highlighted_channel->is_subscribed = !highlighted_channel->is_subscribed;
    }
}

typedef struct
{
    Query query;
    TextureCacheEntry** texture_cache;
    cJSON* subscribed_channels_json;
    HighlightedChannel* channel;
    Connection* thumbnail_conn;
    Connection* results_conn;
    List* raw_thumbnail_queue;
    List* results;
} ParseChannelArgs;

void* parse_channel(void* args)
{
    ParseChannelArgs* targs = (ParseChannelArgs*) args;
    if ((targs == NULL) ||
        (targs->texture_cache == NULL) || 
        (targs->channel == NULL) ||
        (targs->raw_thumbnail_queue == NULL) ||
        (targs->results == NULL)) {
        printf("parse_channel: invalid input(s)\n");
        return NULL;
    }
    
    HttpsRequest req = configure_post_request(targs->query, targs->results_conn->host, api_key, continuation_token);
    if (post_request_is_ready(req) == false) {
        printf("parse_channel: invalid post request\n");
        if (req.payload) {
            free(req.payload); req.payload = NULL;
        }
        free(targs); targs = NULL;
        return NULL;
    }

    cJSON* json = get_json_response(&req, ssl_ctx, targs->results_conn, HTTP_PROTOCOL_VER);
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

    strncpy(targs->channel->info.id, targs->query.focused_id, sizeof(targs->channel->info.id) - 1);
    targs->channel->info.id[sizeof(targs->channel->info.id) - 1] = '\0';

    targs->channel->info.media_type = MEDIA_TYPE_CHANNEL;
    
    const char* title_path = ".header.pageHeaderRenderer.pageTitle";
    
    if (assign_string_from_path(json, title_path, targs->channel->info.title, sizeof(targs->channel->info.title)) == false) {
        printf("parse_channel: failed to assign channel title\n");
    }

    const char* subscriber_count_path = ".header.pageHeaderRenderer.content.pageHeaderViewModel.metadata.contentMetadataViewModel.metadataRows[1].metadataParts[0].text.content";
    
    if (assign_string_from_path(json, subscriber_count_path, targs->channel->info.subscriber_count, sizeof(targs->channel->info.subscriber_count)) == false) {
        printf("parse_channel: failed to assign sub count\n");
    }
    
    const char* thumbnail_path = ".header.pageHeaderRenderer.content.pageHeaderViewModel.image.decoratedAvatarViewModel.avatar.avatarViewModel.image.sources[0].url";
    
    const cJSON* thumbnail_url_item = cjson_pointer_get(json, thumbnail_path);
    if (json_string_is_valid(thumbnail_url_item) == false) {
        printf("parse_channel: failed to assign thumbnail path\n");
        targs->channel->info.thumbnail_loaded = false;
        targs->channel->info.thumbnail_path[0] = '\0';
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

    strncpy(targs->channel->info.thumbnail_path, path1 ? path1 : path2, sizeof(targs->channel->info.thumbnail_path) - 1);
    targs->channel->info.thumbnail_path[sizeof(targs->channel->info.thumbnail_path) - 1] = '\0';
    
    targs->channel->is_subscribed = is_subbed_to_channel(targs->subscribed_channels_json, targs->channel->info.id);
    
    TextureCacheEntry* cached = texture_cache_find_entry(targs->texture_cache, targs->channel->info.id);
    if (cached) {
        targs->channel->info.thumbnail_loaded = true;
        targs->channel->cached = cached;
    }

    else {
        LoadThumbnailArgs* thumb_args = malloc(sizeof(LoadThumbnailArgs));
        if (thumb_args == NULL) {
            printf("parse_channel: 'thumb_args' is null\n");
            targs->channel->info.thumbnail_loaded = false;
            targs->channel->info.thumbnail_path[0] = '\0';
            targs->channel->cached = NULL;
            goto clean;
        }

        thumb_args->conn = targs->thumbnail_conn;
        thumb_args->media_type = MEDIA_TYPE_CHANNEL;
        thumb_args->thumbnail_queue = targs->raw_thumbnail_queue;
        snprintf(thumb_args->id, sizeof(thumb_args->id), "%s", targs->channel->info.id);
        snprintf(thumb_args->thumbnail_path, sizeof(thumb_args->thumbnail_path), "%s", targs->channel->info.thumbnail_path);

        load_thumbnail(thumb_args);

        targs->channel->info.thumbnail_loaded = false;
    }

    SetWindowTitle(TextFormat("[Uploads from %s] - metube", targs->channel->info.title));

    clean: 
        cJSON_Delete(json); json = NULL;
        free(targs); targs = NULL;
        return NULL;
}

void view_user_data(List* results, cJSON* user_data, char** continuation_token, const char* app_title, bool* load_flag, QueryType type)
{
    if ((results == NULL) || (user_data == NULL) || (continuation_token == NULL) || (app_title == NULL) || (load_flag == NULL)) return;

    (*load_flag) = false;

    if (*continuation_token) {
        free(*continuation_token); (*continuation_token) = NULL;
    }

    pthread_mutex_lock(&results->mutex);
    create_results_from_json(user_data, results, type, QUERY_ATTR_REPLACE, true);
    pthread_mutex_unlock(&results->mutex);

    SetWindowTitle(app_title);
}

void draw_video_management_buttons(const Rectangle container, Query* query, HighlightedVideo* selected_video, cJSON* liked_video_data, bool* launch_search, bool* load_channel_info)
{
    const int padding = 5;

    const int nbuttons = 4;
    const int min_button_width = 5;
    const float button_width = (container.width - (padding * (nbuttons - 1))) / nbuttons; 

    const Rectangle play_video_button_bounds = {
        .x = container.x,
        .y = container.y,
        .width = fmax(min_button_width, button_width),
        .height = container.height,
    };

    if (selected_video->info.id[0] == '\0') GuiSetState(STATE_DISABLED);

    if (GuiButton(play_video_button_bounds, "Play Video")) {
        // TODO
    } 
    
    const Rectangle like_video_button_bounds = {
        .x = play_video_button_bounds.x + play_video_button_bounds.width + padding,
        .y = padding,
        .width = fmax(min_button_width, button_width),
        .height = container.height,
    };

    if (GuiButton(like_video_button_bounds, "Like Video")) {
        add_user_data(liked_video_data, &selected_video->info);
    }

    const Rectangle related_videos_button_bounds = {
        .x = like_video_button_bounds.x + like_video_button_bounds.width + padding,
        .y = container.y,
        .width = fmax(min_button_width, button_width),
        .height = container.height,
    };

    if (GuiButton(related_videos_button_bounds, "Related Videos")) {
        *launch_search = true;
        query->attr = QUERY_ATTR_REPLACE;
        query->type = QUERY_TYPE_VIEW_RELATED;
        strncpy(query->focused_id, selected_video->info.id, sizeof(query->focused_id) - 1);
        query->focused_id[sizeof(query->focused_id) - 1] = '\0';
        SetWindowTitle(TextFormat("[Related:%s(loading)] - metube", query->focused_id));                
    }
    
    const Rectangle users_videos_button_bounds = {
        .x = related_videos_button_bounds.x + related_videos_button_bounds.width + padding,
        .y = container.y,
        .width = fmax(min_button_width, button_width),
        .height = container.height,
    };

    if (GuiButton(users_videos_button_bounds, "User's Videos")) {
        *load_channel_info = true;
        query->attr = QUERY_ATTR_REPLACE;
        query->type = QUERY_TYPE_VIEW_CHANNEL;

        strncpy(query->focused_id, selected_video->info.authorId, sizeof(query->focused_id) - 1);
        query->focused_id[sizeof(query->focused_id) - 1] = '\0';
        
        SetWindowTitle(TextFormat("[User %s Videos(loading)] - metube", query->focused_id));
    }

    GuiSetState(STATE_NORMAL);
}

void draw_user_data_buttons(const Rectangle container, Query* query, bool* view_subscribed_channels, bool* view_liked_videos, bool* view_watch_history)
{
    const int padding = 5;
    const int nbuttons = 3;
    
    const int min_button_width = 5;
    const float button_width = (container.width - (padding * (nbuttons - 1))) / nbuttons;

    const Rectangle subscriptions_button_bounds = {
        .x = container.x,
        .y = container.y,
        .width = fmax(min_button_width, button_width),
        .height = container.height,
    };

    if (GuiButton(subscriptions_button_bounds, "Subscribed Channels")) {
        *view_subscribed_channels = true;
        query->attr = QUERY_ATTR_REPLACE;
        query->type = QUERY_TYPE_VIEW_SUBSCRIBED_CHANNELS;
    }

    const Rectangle liked_videos_button_bounds = {
        .x = subscriptions_button_bounds.x + subscriptions_button_bounds.width + padding,
        .y = container.y,
        .width = fmax(min_button_width, button_width),
        .height = container.height,
    };

    if (GuiButton(liked_videos_button_bounds, "Liked Videos")) {
        *view_liked_videos = true;
    }
    
    const Rectangle watch_history_button_bounds = {
        .x = liked_videos_button_bounds.x + liked_videos_button_bounds.width + padding,
        .y = container.y,
        .width = fmax(min_button_width, button_width),
        .height = container.height,
    };

    if (GuiButton(watch_history_button_bounds, "Watch History")) {
        *view_watch_history = true;
        query->attr = QUERY_ATTR_REPLACE;
        query->type = QUERY_TYPE_VIEW_WATCH_HISTORY;
    }
}

void draw_load_more_button(const Rectangle container, const Font font, Query* query, QueryType last_query, bool* load_more)
{
    const int spacing = 2;
    const int font_size = 50;
    const Color text_color = BLACK;
    const char* text = "<< LOAD MORE >>";
    const Vector2 text_dimensions = MeasureTextEx(font, text, font_size, spacing);

    const Vector2 text_pos = {
        .x = container.x + ((container.width - text_dimensions.x) / 2.0f),
        .y = container.y + (text_dimensions.y / 2.0f)
    };

    DrawTextEx(font, text, text_pos, font_size, spacing, text_color);
    if ((CheckCollisionPointRec(GetMousePosition(), container)) && 
        (IsMouseButtonPressed(MOUSE_BUTTON_LEFT))) {
        *load_more = true;
        query->type = last_query;
        query->attr = QUERTY_ATTR_APPEND;
        SetWindowTitle("[Appending] - metube");
    }
}

// split programs up 
    // list/nodes 
    // user data management
    // youtube data json parsing
    // bug with getting user's videos, shows for half a second, then dissapears
int main()
{
    List thumbnail_queue = list_init();
    List task_queue = list_init();
    List results = list_init();

    TextureCache texture_cache = NULL;

    pthread_t thread_pool[MAX_THREADS]; 
    init_thread_pool(MAX_THREADS, thread_pool, worker_thread_funct, &task_queue);
    
    ConnectionPool youtube_pool = connection_pool_init("www.youtube.com", HTTPS_PORT, N_CONN);
    ConnectionPool video_thumbnail_pool = connection_pool_init(media_type_to_thumbnail_host(MEDIA_TYPE_VIDEO), HTTPS_PORT, N_CONN);
    ConnectionPool channel_thumbnail_pool = connection_pool_init(media_type_to_thumbnail_host(MEDIA_TYPE_CHANNEL), HTTPS_PORT, N_CONN);

    bool load_video_information = false;
    Vector2 description_scrollbar = {0};
    HighlightedVideo highlighted_video = {0};
    
    bool load_channel_information = false;
    HighlightedChannel highlighted_channel = {0};

    Query query = {
        .string = "",
        .media = MEDIA_TYPE_ANY,
        .sort = SORT_TYPE_RELEVANCE,
        .attr = QUERY_ATTR_REPLACE,
        .type = QUERY_TYPE_USER_INPUT,
        .allow_youtube_shorts = true,
    };

    cJSON* liked_videos_json = file_exists(LIKED_VIDEOS_FILE) ? parse_json_file(LIKED_VIDEOS_FILE) : create_empty_array_object(ARRAY_NAME);
    bool view_liked_videos = false;
    
    cJSON* watch_history_json = file_exists(WATCH_HISTORY_FILE) ? parse_json_file(WATCH_HISTORY_FILE) : create_empty_array_object(ARRAY_NAME);
    bool view_watch_history = false;
    
    cJSON* subscribed_channels_json = file_exists(SUBSCRIPTIONS_FILE) ? parse_json_file(SUBSCRIPTIONS_FILE) : create_empty_array_object(ARRAY_NAME);
    bool view_subscribed_channels = false;

    if ((liked_videos_json == NULL) || (watch_history_json == NULL) || (subscribed_channels_json == NULL)) {
        printf("CRITICAL: failed to create json object(s)\n");
        goto cleanup;
    }

    ssl_ctx = SSL_CTX_new(TLS_client_method());
    if (ssl_ctx == NULL) {
        printf("CRITICAL: 'ssl_ctx' is null'\n");
        goto cleanup;
    } 

    bool show_filter_window = false;

    bool text_box_focused = false;

    bool launch_search = false;

    QueryType last_search_type = -1;
    char last_search_query[512] = {0};

    Vector2 result_scrollbar = {0};

    init_app();

    Ui ui = {
        .font = GetFontDefault(),
        .padding = 5,
        .spacing = 2,
        .word_wrap = true,
    };
    
    while (!WindowShouldClose())
    {
        if (HASH_COUNT(texture_cache) > 0) {
            texture_cache_remove_expried_entries(&texture_cache);
        }

        if (thumbnail_queue.count > 0) {
            pthread_mutex_lock(&thumbnail_queue.mutex);
            process_thumbnail_queue(&thumbnail_queue, &texture_cache);
            pthread_mutex_unlock(&thumbnail_queue.mutex);
        }

        if (load_video_information) {
            load_video_information = false;

            Connection* conn = &youtube_pool.connections[youtube_pool.current_conn];
            HttpsRequest req = configure_post_request(query, conn->host, api_key, continuation_token);
            if (post_request_is_ready(req)) {
                FocusedInfoArgs* targs = init_focused_info_args(req, conn, &highlighted_video);
                if (targs) {
                    if (launch_task(&task_queue, targs, get_video_description) == false) {
                        printf("failed to launch task: 'get_video_description'\n");
                        free(targs); targs = NULL;
                    }
                }
            }
        }

        if (launch_search) {
            launch_search = false;

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
            targs->conn = &youtube_pool.connections[youtube_pool.current_conn];

            if (launch_task(&task_queue, targs, get_results_from_query) == false) {
                printf("failed to launch task: 'get_results_from_query'\n");
                free(targs); targs = NULL;
            }
        }

        if (view_liked_videos) {
            view_user_data(&results, liked_videos_json, &continuation_token, "[Liked Videos] - metube", &view_liked_videos, QUERY_TYPE_VIEW_LIKED_VIDEOS);
        }

        if (view_watch_history) {
            view_user_data(&results, watch_history_json, &continuation_token, "[History] - metube", &view_watch_history, QUERY_TYPE_VIEW_WATCH_HISTORY);
        }

        if (view_subscribed_channels) {
            view_user_data(&results, subscribed_channels_json, &continuation_token, "[Subscriptions] - metube", &view_subscribed_channels, QUERY_TYPE_VIEW_SUBSCRIBED_CHANNELS);
        }

        if (load_channel_information) {
            load_channel_information = false;
            last_search_type = query.type;

            ParseChannelArgs* targs = malloc(sizeof(ParseChannelArgs));
            if (targs) {
                targs->query = query;
                targs->results = &results;
                targs->channel = &highlighted_channel;
                targs->texture_cache = &texture_cache;
                targs->raw_thumbnail_queue = &thumbnail_queue;
                targs->subscribed_channels_json = subscribed_channels_json;
                targs->results_conn = &youtube_pool.connections[youtube_pool.current_conn];
                targs->thumbnail_conn = &channel_thumbnail_pool.connections[channel_thumbnail_pool.current_conn];
                
                if (launch_task(&task_queue, targs, parse_channel) == false) {
                    printf("failed to launch parse_channel task\n");
                    free(targs); targs = NULL;
                }
            }
        }

        BeginDrawing();

            ClearBackground(RAYWHITE);

            const float BAR_HEIGHT = 25.0;

            const Rectangle trending_button_bounds = {
                .x = ui.padding,
                .y = ui.padding,
                .width = 20,
                .height = BAR_HEIGHT,
            };

            if (GuiButton(trending_button_bounds, "T")) {
                launch_search = true;
                query.attr = QUERY_ATTR_REPLACE;
                query.type = QUERY_TYPE_VIEW_TRENDING;
                SetWindowTitle("[Trending] - metube");
            }

            const Rectangle search_bar_bounds = {
                .x = trending_button_bounds.x + trending_button_bounds.width + ui.padding, 
                .y = ui.padding, 
                .width = 325, 
                .height = BAR_HEIGHT 
            };

            if (GuiTextBox(search_bar_bounds, query.string, sizeof(query.string), text_box_focused)) {
                text_box_focused = !text_box_focused;
            }

            const Rectangle search_button_bounds = {
                .x = search_bar_bounds.x + search_bar_bounds.width + ui.padding, 
                .y = ui.padding, 
                .width = 25, 
                .height = BAR_HEIGHT
            };

            if (GuiButton(search_button_bounds, "S") || IsKeyPressed(KEY_ENTER)) {
                if (trim_whitespace(query.string) > 0) {
                    launch_search = true;
                    query.attr = QUERY_ATTR_REPLACE;
                    query.type = QUERY_TYPE_USER_INPUT;
                    SetWindowTitle(TextFormat("[%s(loading)] - metube", query.string));
                }
            }

            const Rectangle filter_button_bounds = {
                .x = search_button_bounds.x + search_button_bounds.width + ui.padding,
                .y = ui.padding,
                .width = 25,
                .height = BAR_HEIGHT,
            };

            if (GuiButton(filter_button_bounds, "Fil")) {
                show_filter_window = !show_filter_window;
            }

            const Rectangle filter_window_bounds = {
                    .x = ui.padding,
                    .y = BAR_HEIGHT + (ui.padding * 2), 
                    .width = trending_button_bounds.width + search_bar_bounds.width + search_button_bounds.width + filter_button_bounds.width + (ui.padding * 3), 
                    .height = 75
            };

            if (show_filter_window) {
                draw_filter_window(filter_window_bounds, ui, &query);
            }
            
            const float focused_channel_height = 80;
            const Rectangle focused_channel_bounds = {
                .x = ui.padding,
                .y = GetScreenHeight() - focused_channel_height - ui.padding,
                .width = trending_button_bounds.width + search_bar_bounds.width + search_button_bounds.width + filter_button_bounds.width + (ui.padding * 3), 
                .height = focused_channel_height,
            };
           
            const Rectangle scroll_window_bounds = { 
                .x = ui.padding, 
                .y = search_bar_bounds.y + search_bar_bounds.height + (show_filter_window ? (filter_window_bounds.height + ui.padding) : 0) + ui.padding, 
                .width = focused_channel_bounds.width, 
                .height = GetScreenHeight() - scroll_window_bounds.y - focused_channel_height - (ui.padding * 2), 
            };

            const bool load_more_button_visible = (continuation_token) && (continuation_token[0] != '\0');
            const size_t results_len = results.count + load_more_button_visible;

            const int container_height = 80;
            const Rectangle content_area = {
                .x = scroll_window_bounds.x,
                .y = scroll_window_bounds.y,
                .width = scroll_window_bounds.width,
                .height = container_height * results_len,
            };

            const int SCROLLBAR_WIDTH = 13;
            const bool vertical_scrollbar_visible = content_area.height > scroll_window_bounds.height;

            GuiScrollPanel(scroll_window_bounds, NULL, content_area, &result_scrollbar, NULL, true);

            const Rectangle scissor_rect = padded_rectangle(1, scroll_window_bounds);

            BeginScissorMode(scissor_rect.x, scissor_rect.y, scissor_rect.width, scissor_rect.height);

            int i = 0;
            float container_y = scroll_window_bounds.y;
            Rectangle container = { 
                .x = scroll_window_bounds.x, 
                .y = container_y, 
                .width = scroll_window_bounds.width - (vertical_scrollbar_visible ? SCROLLBAR_WIDTH: 0),
                .height = container_height 
            };

            pthread_mutex_lock(&results.mutex);

            for (Node* node = results.head; node; node = node->next, i++, container_y += container_height) {
                SearchResult* search_result = (SearchResult*) node->content;

                container.y = container_y + result_scrollbar.y;

                if (CheckCollisionRecs(scissor_rect, container) == false) {
                    continue;
                }

                const bool result_is_highlighted = strcmp(search_result->id, highlighted_video.info.id) == 0;
                const Color container_color = result_is_highlighted ? BLUE : ((i % 2) ? WHITE : RAYWHITE);

                Texture2D thumbnail = (Texture2D){0};

                TextureCacheEntry* cached = texture_cache_find_entry(&texture_cache, search_result->id);
                if (texture_cache_entry_is_ready(cached)) {
                    thumbnail = cached->texture;
                    timer_start(&cached->timer, CACHED_TEXTURE_LIFETIME); // refresh lifetime
                }

                else if ((search_result->thumbnail_loaded == false) && (search_result->thumbnail_path[0] != '\0')) {
                    search_result->thumbnail_loaded = true;

                    ConnectionPool* pool = search_result->media_type == MEDIA_TYPE_CHANNEL ? &channel_thumbnail_pool : &video_thumbnail_pool;
                    if (pool) {
                        Connection* conn = &pool->connections[pool->current_conn];
                        
                        if (queue_thumbnail_load(&task_queue, &thumbnail_queue, conn, search_result->id, search_result->thumbnail_path, search_result->media_type) == false) {
                            cycle_connection(pool);
                        }
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
                                query.type = QUERY_TYPE_VIEW_VIDEO;
                                memcpy(&highlighted_video.info, search_result, sizeof(SearchResult));
                                add_user_data(watch_history_json, search_result);
                            }
                            break;
                        case MEDIA_TYPE_PLAYLIST:
                            launch_search = true;
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
            
            if (load_more_button_visible) {
                const Rectangle load_more_button_bounds = {
                    .x = container.x,
                    .y = container.y + container_height,
                    .width = container.width,
                    .height = container_height,
                };

                draw_load_more_button(load_more_button_bounds, ui.font, &query, last_search_type, &launch_search);
            }
            
            EndScissorMode();

            const Rectangle TOP_RIGHT_PANEL = {
                .x = filter_button_bounds.x + filter_button_bounds.width + ui.padding,
                .y = ui.padding,
                .width = GetScreenWidth() - TOP_RIGHT_PANEL.x - ui.padding,
                .height = BAR_HEIGHT, 
            };

            draw_video_management_buttons(TOP_RIGHT_PANEL, &query, &highlighted_video, liked_videos_json, &launch_search, &load_channel_information);

            if ((highlighted_channel.info.thumbnail_loaded == false) && (highlighted_channel.info.thumbnail_path[0] != '\0')) { 
                highlighted_channel.info.thumbnail_loaded = true;
                highlighted_channel.cached = texture_cache_find_entry(&texture_cache, highlighted_channel.info.id);
            }

            draw_highlighted_channel(focused_channel_bounds, &ui, subscribed_channels_json, &highlighted_channel);

            const Rectangle BOTTOM_RIGHT_PANEL = {
                .x = focused_channel_bounds.x + focused_channel_bounds.width + ui.padding,
                .y = GetScreenHeight() - BAR_HEIGHT - ui.padding,
                .width = TOP_RIGHT_PANEL.width,
                .height = BAR_HEIGHT,
            };

            draw_user_data_buttons(BOTTOM_RIGHT_PANEL, &query, &view_subscribed_channels, &view_liked_videos, &view_watch_history);
            
            const Rectangle focused_video_bounds = {
                .x = scroll_window_bounds.x + scroll_window_bounds.width + ui.padding,
                .y = filter_window_bounds.y,
                .width = GetScreenWidth() - focused_video_bounds.x,
                .height = GetScreenHeight() - focused_video_bounds.y - BAR_HEIGHT - (ui.padding * 2),
            };
            
            draw_highlighted_video(focused_video_bounds, ui, &description_scrollbar, &highlighted_video);
            
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
        texture_cache_free(&texture_cache);
        
        if (watch_history_json) {
            write_json_to_file(watch_history_json, WATCH_HISTORY_FILE);
            cJSON_Delete(watch_history_json); watch_history_json = NULL;
        }

        if (subscribed_channels_json) {
            write_json_to_file(subscribed_channels_json, SUBSCRIPTIONS_FILE);
            cJSON_Delete(subscribed_channels_json); subscribed_channels_json = NULL;
        }

        if (liked_videos_json) {
            write_json_to_file(liked_videos_json, LIKED_VIDEOS_FILE);
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
        connection_pool_free(&youtube_pool);
        connection_pool_free(&video_thumbnail_pool);
        connection_pool_free(&channel_thumbnail_pool);
        
        CloseWindow();
        
        return 0;
}

// stuff to do:
    // able to add videos to created playlist
    // fonts for L.O.T.E.
    // handle connecticity issues (no wifi on startup, changing connections, etc.)
    // thumbnail frames from video click
    // better create_results_from_json?
    // move ui stuff together
    // update highlighted channel anytime you press a video
    // issue with pressing user's videos button, sometimes channel shows for half second, then dissapears
    // remove raylib dependency in query.h