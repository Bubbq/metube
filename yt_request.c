#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <cjson/cJSON.h>

typedef enum
{
    MEDIA_TYPE_LIVE,
    MEDIA_TYPE_SHORT,
    MEDIA_TYPE_VIDEO,
    MEDIA_TYPE_CHANNEL,
    MEDIA_TYPE_PLAYLIST,
    MEDIA_TYPE_ANY,
} MediaType;

const char* media_type_to_search_param(MediaType media_type)
{
    switch (media_type) {
        case MEDIA_TYPE_SHORT:
        case MEDIA_TYPE_VIDEO: return "SAhAB";
        case MEDIA_TYPE_CHANNEL: return "SAhAC";
        case MEDIA_TYPE_PLAYLIST: return "SAhAD";
        case MEDIA_TYPE_LIVE: return "SBBABQAE";
        case MEDIA_TYPE_ANY: return "%253D";
    }

    printf("media_type_to_search_param: invalid media_type passed\n");
    return NULL;
}

char* media_type_to_thumbnail_host(const MediaType media_type)
{
    switch (media_type) {
        case MEDIA_TYPE_LIVE:
        case MEDIA_TYPE_SHORT:
        case MEDIA_TYPE_VIDEO: 
        case MEDIA_TYPE_PLAYLIST: return "i.ytimg.com";
        case MEDIA_TYPE_CHANNEL: return "yt3.ggpht.com";
        case MEDIA_TYPE_ANY:
          break;
    }

    printf("media_type_to_thumbnail_host: invalid media_type passed\n");
    return NULL;
}

typedef enum
{
    SORT_TYPE_RELEVANCE,
    SORT_TYPE_UPLOAD_DATE,
    SORT_TYPE_VIEW_COUNT,
    SORT_TYPE_RATING,
} SortType;

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

typedef enum
{
    QUERY_TYPE_USER_INPUT,  
    QUERY_TYPE_RELATED,  
    QUERY_TYPE_TRENDING, 
    QUERY_TYPE_VIDEO_FOCUS,
    QUERY_TYPE_VIEW_PLAYLIST,
    QUERY_TYPE_VIEW_CHANNEL,
} QueryType;

typedef struct
{
    char user_input[256];        
    char focused_id[64];     
    char* continuation_token;
    QueryType type;
    MediaType media;          
    SortType sort;
    bool allow_youtube_shorts; 
} Query;

const char* query_type_to_endpoint(const QueryType search_type)
{
    switch (search_type) {
        case QUERY_TYPE_USER_INPUT: return "search";
        case QUERY_TYPE_TRENDING:
        case QUERY_TYPE_VIEW_CHANNEL:
        case QUERY_TYPE_VIEW_PLAYLIST: return "browse";
        case QUERY_TYPE_VIDEO_FOCUS: return "player";
        case QUERY_TYPE_RELATED: return "next";
    }

    printf("search_type_to_endpoint: invalid type passed\n");
    return NULL;
}

bool configure_api_path(char* dest, const size_t dest_size, QueryType query_type, const char* key)
{
    const char* endpoint = query_type_to_endpoint(query_type);

    if ((endpoint == NULL) || (key == NULL) || (dest == NULL)) return false;

    const size_t written = snprintf(dest, dest_size, "/youtubei/v1/%s?key=%s", endpoint, key);

    return (written > 0) && (written < dest_size);
}

typedef struct
{
    char header[512];
    char path[256];
    char* payload; // NULL for GET
} HttpsRequest;

#define CLIENT_NAME "WEB"
#define CLIENT_VER "2.20250730"
#define YT_API_PLAYLIST_BROWSE_ID_PREFIX "VL"    
#define YT_API_TRENDING_BROWSE_ID "FEtrending" 
#define YT_API_CHANNEL_VIDEOS_PARAMS "EgZ2aWRlb3PyBgQKAjoA"  
#define USER_AGENT "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/130.0.0.0 Safari/537.36"

bool configure_thumbnail_get_header(char* dest, const size_t dest_size, const char* host, const char* path)
{
    if ((dest == NULL) || (host == NULL) || (path == NULL)) return false;

    const size_t len =  snprintf(dest, dest_size,
                "GET %s HTTP/1.1\r\n"
                        "Host: %s\r\n"
                        "User-Agent: " USER_AGENT "\r\n"
                        "Connection: keep-alive\r\n"
                        "\r\n",
                        path, host);
    
    return (len > dest_size) && (len < dest_size);
}

bool configure_post_header(char* dest, const size_t dest_size, const char *host, const char *path, const size_t payload_size)
{
    if ((dest == NULL) || (host == NULL) || (path == NULL)) return false;

    const size_t len = snprintf(dest, dest_size,
                        "POST %s HTTP/1.1\r\n"
                        "Host: %s\r\n"
                        "User-Agent: " USER_AGENT "\r\n"
                        "Content-Type: application/json\r\n"
                        "Accept: application/json\r\n"
                        "Content-Length: %zu\r\n"
                        "Connection: keep-alive\r\n"
                        "\r\n",
                        path, host, payload_size);
    
    return (len > 0) && (len < dest_size);
}

bool add_queried_search_payload(cJSON* root, const Query* q)
{
    if ((root == NULL) || (q == NULL) || (q->user_input[0] == '\0')) {
        printf("add_queried_search_payload: invalid input\n");
        return false;
    }

    const char* sort_param = sort_type_to_search_param(q->sort);
    const char* media_param = media_type_to_search_param(q->media);

    char params[16];
    const int len = snprintf(params, sizeof(params), "%s%s",  sort_param, media_param);
    if ((len < 0) || (len >= sizeof(params))) {
        printf("add_queried_search_payload: snprintf returned %d\n", len);
        return false;
    }

    if (cJSON_AddStringToObject(root, "query", q->user_input) == NULL) {
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
        printf("add_continuation_payload: failed to add 'continuation'\n");
        return false;
    }

    return true;
}

cJSON* build_base_request_payload()
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

    cJSON* root = build_base_request_payload();
    if (root == NULL) {
        printf("configure_payload: failed to create base payload\n");
        return NULL;
    }

    bool (*payload_funct)(cJSON*,const Query*) = NULL;

    switch (query->type) {
        case QUERY_TYPE_RELATED:    
        case QUERY_TYPE_VIDEO_FOCUS:   payload_funct = add_view_related_videos_payload;  break;
        case QUERY_TYPE_USER_INPUT:    payload_funct = add_queried_search_payload;       break;
        case QUERY_TYPE_TRENDING:      payload_funct = add_view_trending_videos_payload; break;
        case QUERY_TYPE_VIEW_PLAYLIST: payload_funct = add_view_playlist_videos_payload; break;
        case QUERY_TYPE_VIEW_CHANNEL:  payload_funct = add_view_channel_videos_payload;  break;
    }

    if (payload_funct(root, query) == false) {
        printf("configure_payload: failed to add payload\n");
        cJSON_Delete(root); root = NULL;
    }

    return root;
}

HttpsRequest configure_post_request(const Query* query, const char* internal_api_key, const char* host)
{
    if ((query == NULL) || (internal_api_key == NULL) || (host == NULL)) return (HttpsRequest){0};

    HttpsRequest req = {0};

    if (configure_api_path(req.path, sizeof(req.path), query->type, internal_api_key) == false) {
        printf("configure_post_request: failed to configure api path\n");
        return (HttpsRequest){0};
    }

    cJSON* payload = configure_payload(query);
    if (payload == NULL) {
        printf("configure_post_request: failed to configure payload\n");
        return (HttpsRequest){0};
    }

    req.payload = cJSON_Print(payload);

    if (configure_post_header(req.header, sizeof(req.header), host, req.path, strlen(req.payload)) == false) {
        printf("configure_post_request: failed to configure header\n");
        cJSON_Delete(payload); payload = NULL;
        free(req.payload);
        return (HttpsRequest){0};
    }

    cJSON_Delete(payload); payload = NULL;

    return req;
}