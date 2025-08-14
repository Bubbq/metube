#include "include/query_ops.h"

#include "include/user_data.h"
#include "include/yt_parse.h"
#include "include/request_config.h"

void* get_video_metadata(void* args)
{
    VideoMetadataArgs* targs = (VideoMetadataArgs*) args;
    if ((targs == NULL) || 
        (!post_request_is_ready(targs->req)) ||
        (targs->ssl_ctx == NULL) || 
        (targs->client_ctx == NULL) || 
        (targs->highlighted_video == NULL)) {
        fprintf(stderr, "get_video_metadata: invalid args\n");
        return NULL;
    }

    ClientContext* client_ctx = targs->client_ctx;

    Connection* youtube_conn = &client_ctx->youtube_api_pool.connections[client_ctx->youtube_api_pool.current_conn];

    cJSON* res = get_json_response(&targs->req, targs->ssl_ctx, youtube_conn, HTTP_PROTOCOL_VER);
    
    free(targs->req.payload); targs->req.payload = NULL;

    if (res == NULL) {
        fprintf(stderr, "get_video_metadata: failed to resolve json response\n");
        return NULL;
    }

    if (targs->highlighted_video->description) {
        free(targs->highlighted_video->description); targs->highlighted_video->description = NULL;
    }

    targs->highlighted_video->description = get_video_description(res);

    cJSON_Delete(res); res = NULL;

    return NULL;
}

void* get_results_from_query(void* args)
{
    SearchThreadArgs* targs = (SearchThreadArgs*) args;
    if ((targs == NULL) || 
        (targs->results == NULL) || 
        (targs->ssl_ctx == NULL) || 
        (targs->client_ctx == NULL)) {
        fprintf(stderr, "get_results_from_query: invalid args\n");
        return NULL;
    }

    ClientContext* client_ctx = targs->client_ctx;
    
    Connection* youtube_conn = &client_ctx->youtube_api_pool.connections[client_ctx->youtube_api_pool.current_conn];
    char** continuation_token = &targs->client_ctx->continuation_token;
    const char* api_key = targs->client_ctx->api_key;

    HttpsRequest req = configure_post_request(targs->query, youtube_conn->host, api_key, (*continuation_token));
    if (post_request_is_ready(req) == false) {
        fprintf(stderr, "get_results_from_query: failed to resolve request\n");
        return NULL;
    }

    cJSON* res = get_json_response(&req, targs->ssl_ctx, youtube_conn, HTTP_PROTOCOL_VER);

    free(req.payload); req.payload = NULL;

    if (res == NULL) {
        fprintf(stderr, "get_results_from_query: failed to resolve json response\n");
        return NULL;
    }

    const QueryType query_type = targs->query.type;
    const QueryAttribute query_attr = targs->query.attr;
    
    pthread_mutex_lock(&targs->results->mutex);
    create_results_from_json(res, targs->results, query_type, query_attr, targs->query.allow_youtube_shorts);
    pthread_mutex_unlock(&targs->results->mutex);

    pthread_mutex_lock(&targs->client_ctx->token_mutex);
    get_continuation_token(res, continuation_token, query_type, query_attr);
    pthread_mutex_unlock(&targs->client_ctx->token_mutex);
    
    cJSON_Delete(res); res = NULL;

    return NULL;
}

void* get_channel_metadata(void* args)
{
    ChannelMetadataArgs* targs = (ChannelMetadataArgs*) args;
    if ((targs == NULL) || 
        (targs->subscribed_channels_json == NULL) || 
        (targs->channel == NULL) || 
        (targs->client_ctx == NULL) || 
        (targs->ssl_ctx == NULL) || 
        (targs->thumbnail_loader == NULL) || 
        (targs->results == NULL)) {
        fprintf(stderr, "get_channel_metadata: invalid args\n");
        return NULL;
    }

    ClientContext* client_ctx = targs->client_ctx;
    Connection* youtube_conn = &client_ctx->youtube_api_pool.connections[client_ctx->youtube_api_pool.current_conn];
    char** continuation_token = &client_ctx->continuation_token; 
    const char* api_key = client_ctx->api_key;

    HttpsRequest results_req = configure_post_request(targs->query, youtube_conn->host, api_key, (*continuation_token));
    if (!post_request_is_ready(results_req)) {
        fprintf(stderr, "get_channel_metadata: failed to resolve results request\n");
        return NULL;
    }

    cJSON* res = get_json_response(&results_req, targs->ssl_ctx, youtube_conn, HTTP_PROTOCOL_VER);

    free(results_req.payload); results_req.payload = NULL;
    
    if (res == NULL) {
        fprintf(stderr, "get_channel_metadata: failed to resolve json response\n");
        return NULL;
    }

    const QueryType query_type = targs->query.type;
    const QueryAttribute query_attr = targs->query.attr;
    
    pthread_mutex_lock(&targs->results->mutex);
    create_results_from_json(res, targs->results, query_type, query_attr, false);
    pthread_mutex_unlock(&targs->results->mutex);

    pthread_mutex_lock(&client_ctx->token_mutex);
    get_continuation_token(res, continuation_token, query_type, query_attr);
    pthread_mutex_unlock(&client_ctx->token_mutex);

    if (query_attr == QUERY_ATTR_REPLACE) {
        const bool channel_parse_status = parse_highlighted_channel(res, &targs->channel->info);
        
        cJSON_Delete(res); res = NULL;
        
        if (!channel_parse_status) {
            fprintf(stderr, "get_channel_metadata: failed to parse channel information\n");
            targs->channel->info.thumbnail_loaded = false;
            targs->channel->info.thumbnail_path[0] = '\0';
            targs->channel->cached = NULL;
            return NULL;
        }

        targs->channel->is_subscribed = is_subbed_to_channel(targs->subscribed_channels_json, targs->channel->info.id);
   
        LoadThumbnailArgs* thumb_args = malloc(sizeof(LoadThumbnailArgs));
        if (thumb_args == NULL) {
            fprintf(stderr, "get_channel_metadata: malloc returned null\n");
            targs->channel->info.thumbnail_loaded = false;
            targs->channel->info.thumbnail_path[0] = '\0';
            targs->channel->cached = NULL;
            return NULL;
        }
        
        Connection* channel_conn = thumbnail_loader_get_connection(targs->thumbnail_loader, MEDIA_TYPE_CHANNEL);
        
        if (channel_conn == NULL) {
            fprintf(stderr, "get_channel_metadata: failed to resolve channel thumbnail connection\n");
            targs->channel->info.thumbnail_loaded = false;
            targs->channel->info.thumbnail_path[0] = '\0';
            targs->channel->cached = NULL;
            return NULL;
        }

        strlcpy(thumb_args->id, targs->channel->info.id, sizeof(thumb_args->id));
        strlcpy(thumb_args->path, targs->channel->info.thumbnail_path, sizeof(thumb_args->path));
        thumb_args->loader = targs->thumbnail_loader;
        thumb_args->media_type = MEDIA_TYPE_CHANNEL;
        thumb_args->ssl_ctx = targs->ssl_ctx;

        load_thumbnail(thumb_args);

        targs->channel->info.thumbnail_loaded = false;
    }

    return NULL;
}