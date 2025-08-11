#include "utils.h"
#include "yt_client.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

cJSON* configure_base_payload()
{
    cJSON* client = configure_client_object();
    if (client == NULL) {
        fprintf(stderr, "configure_base_payload: client is null\n");
        return NULL;
    }

    cJSON* context = cJSON_CreateObject();
    if (context == NULL) {
        fprintf(stderr, "configure_base_payload: context is null\n");
        cJSON_Delete(client); client = NULL;
        return NULL;
    }
    
    if (cJSON_AddItemToObject(context, "client", client) == false) {
        fprintf(stderr, "configure_base_payload: failed to add client to context object\n");
        cJSON_Delete(client); client = NULL;
        cJSON_Delete(context); context = NULL;
        return NULL;
    }  
    
    cJSON* root = cJSON_CreateObject();
    if (root == NULL) {
        fprintf(stderr, "configure_base_payload: root is null\n");
        cJSON_Delete(context); context = NULL;
        return NULL;
    }

    if (cJSON_AddItemToObject(root, "context", context) == false) {
        fprintf(stderr, "configure_base_payload: failed to add context object to root\n");
        cJSON_Delete(context); context = NULL;
        cJSON_Delete(root); root = NULL;
    }  

    return root; 
}

cJSON* configure_client_object()
{
    cJSON* client = cJSON_CreateObject();
    if (client == NULL) {
        fprintf(stderr, "configure_client_object: failed to create object\n");
        return NULL;
    }

    if ((cJSON_AddStringToObject(client, "clientName", CLIENT_NAME) == NULL) || 
        (cJSON_AddStringToObject(client, "clientVersion", CLIENT_VER) == NULL)) {
        fprintf(stderr, "configure_client_object: failed to add client elements\n");
        cJSON_Delete(client); client = NULL;
    }

    return client;
}

cJSON* configure_post_payload(const Query* query, const char* continuation_token)
{
    if (query == NULL) return NULL;

    cJSON* root = configure_base_payload();
    if (root == NULL) {
        printf("configure_post_payload: failed to create base payload\n");
        return NULL;
    }

    if ((query->attr == QUERTY_ATTR_APPEND) && (add_continuation_payload(root, continuation_token) == false))  {
        fprintf(stderr, "configure_post_payload: failed to add continuation payload\n");
        cJSON_Delete(root); root = NULL;
    }

    else if (query->attr == QUERY_ATTR_REPLACE) {
        bool success = false;

        switch (query->type) {
            case QUERY_TYPE_VIEW_VIDEO: 
            case QUERY_TYPE_VIEW_RELATED:  success = add_view_related_videos_payload(root, query->focused_id); break;    
            case QUERY_TYPE_USER_INPUT:    success = add_view_user_input_payload(root, query->string, query->sort, query->media); break;
            case QUERY_TYPE_VIEW_CHANNEL:  success = add_view_channel_videos_payload(root, query->focused_id);  break;
            case QUERY_TYPE_VIEW_PLAYLIST: success = add_view_playlist_videos_payload(root, query->focused_id); break;
            case QUERY_TYPE_VIEW_TRENDING: success = add_view_trending_videos_payload(root); break;
            default:    
                fprintf(stderr, "configure_post_payload: QueryType %d does not have a payload\n", query->type);
        }

        if (success == false) {
            fprintf(stderr, "configure_post_payload: failed to add payload\n");
            cJSON_Delete(root); root = NULL;
        }
    }

    return root;
}

bool add_view_trending_videos_payload(cJSON* root)
{
    if (root == NULL) return false;

    return cJSON_AddStringToObject(root, "browseId", YT_API_TRENDING_BROWSE_ID);
}

bool add_view_related_videos_payload(cJSON* root, const char* video_id)
{
    if ((root == NULL) || (valid_string(video_id) == false)) return false;

    return cJSON_AddStringToObject(root, "videoId", video_id);
}

bool add_view_channel_videos_payload(cJSON* root, const char* channel_id)
{
    if ((root == NULL) || (valid_string(channel_id) == false)) return false;

    return cJSON_AddStringToObject(root, "browseId", channel_id) &&
           cJSON_AddStringToObject(root, "params", YT_API_CHANNEL_VIDEOS_PARAMS);
}

bool add_continuation_payload(cJSON* root, const char* continuation_token)
{
    if ((root == NULL) || (valid_string(continuation_token) == false)) return false;

    return cJSON_AddStringToObject(root, "continuation", continuation_token);
}

bool add_view_playlist_videos_payload(cJSON* root, const char* playlist_id)
{
    if ((root == NULL) || (valid_string(playlist_id) == false)) return false;

    char browse_id[64] = {0};

    const int written = snprintf(browse_id, sizeof(browse_id),"%s%s", YT_API_PLAYLIST_BROWSE_ID_PREFIX, playlist_id);
    if ((written < 0) || (written >= sizeof(browse_id))) {
        fprintf(stderr, "add_view_playlist_videos_payload: snprintf returned %d\n", written);
        return false;
    }

    return cJSON_AddStringToObject(root, "browseId", browse_id);
}

bool add_view_user_input_payload(cJSON* root, const char* user_input, const SortType sort_type, const MediaType media_type)
{
    if ((root == NULL) || (valid_string(user_input) == false)) return false;

    const char* sort_param = sort_type_to_search_param(sort_type);
    const char* media_param = media_type_to_search_param(media_type);

    if ((sort_param == NULL) || (media_param == NULL)) {
        fprintf(stderr, "add_view_user_input_payload: SortType (%d) or MediaType (%d) returned invalid search param\n", sort_type, media_type);
        return false;
    }

    char params[32] = {0};
    
    const int written = snprintf(params, sizeof(params), "%s%s", sort_param, media_param);
    if ((written < 0) || (written >= sizeof(params))) {
        fprintf(stderr, "add_view_user_input_payload: snprintf returned %d\n", written);
        return false;
    }

    return cJSON_AddStringToObject(root, "query", user_input) && 
           cJSON_AddStringToObject(root, "params", params);
}

bool configure_youtube_internal_api_path(char* dest, const size_t dest_size, QueryType query_type, const char* key)
{
    const char* endpoint = query_type_to_endpoint(query_type);

    if ((endpoint == NULL) || (dest == NULL) || (valid_string(key) == false)) return false;

    const size_t written = snprintf(dest, dest_size, "/youtubei/v1/%s?key=%s", endpoint, key);

    return (written > 0) && (written < dest_size);
}

HttpsRequest configure_post_request(const Query query, const char* host, const char* api_key, const char* continuation_token)
{
    if (valid_string(host) == false) return (HttpsRequest){0};

    HttpsRequest post = (HttpsRequest){0};

    if (configure_youtube_internal_api_path(post.path, sizeof(post.path), query.type, api_key) == false) {
        fprintf(stderr, "configure_post_request: failed to resolve path\n");
        return post;
    }

    cJSON* payload = configure_post_payload(&query, continuation_token);

    if (payload == NULL) {
        fprintf(stderr, "configure_post_request: payload is null\n");
        return post;
    }

    post.payload = cJSON_Print(payload);

    if (configure_post_header(post.header, sizeof(post.header), host, post.path, USER_AGENT, CONNECTION_STATUS, strlen(post.payload), HTTP_PROTOCOL_VER) == false) {
        fprintf(stderr, "configure_post_request: failed to resolve header\n");
        free(post.payload); post.payload = NULL;
    }
    
    cJSON_Delete(payload); payload = NULL;
    
    return post;
}