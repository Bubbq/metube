#include "include/client_context.h"

#include "include/threads.h"

ClientContext client_context_init()
{
    const char* youtube_host = "www.youtube.com";
    const char* channel_host = media_type_to_thumbnail_host(MEDIA_TYPE_CHANNEL);
    const char* video_host = media_type_to_thumbnail_host(MEDIA_TYPE_VIDEO);

    ClientContext ctx = {
        .youtube_api_pool = connection_pool_init(youtube_host, HTTPS_PORT, MAX_THREADS),
        .channel_thumb = connection_pool_init(channel_host, HTTPS_PORT, MAX_THREADS),
        .video_thumb = connection_pool_init(video_host, HTTPS_PORT, MAX_THREADS),
        .continuation_token = NULL,
        .api_key = "AIzaSyAO_FJ2SlqU8Q4STEHLGCilw_Y9_11qcW8",
        .ssl_ctx = SSL_CTX_new(TLS_client_method()),
    };

    pthread_mutex_init(&ctx.pool_mutex, NULL);
    pthread_mutex_init(&ctx.token_mutex, NULL);

    return ctx;
}

void client_context_free(ClientContext* client)
{
    if (client == NULL)
        return;

    connection_pool_free(&client->youtube_api_pool);
    connection_pool_free(&client->channel_thumb);
    connection_pool_free(&client->video_thumb);

    if (client->continuation_token) {
        free(client->continuation_token); client->continuation_token = NULL;
    }

    if (client->ssl_ctx) {
        SSL_CTX_free(client->ssl_ctx); client->ssl_ctx = NULL;
    }

    pthread_mutex_destroy(&client->token_mutex);
    pthread_mutex_destroy(&client->pool_mutex);
}

Connection* client_context_get_thumbnail_connection(ClientContext* client, const MediaType media_type)
{
    if (client == NULL)
        return NULL;

    ConnectionPool* pool = media_type == MEDIA_TYPE_CHANNEL ? 
                       &client->channel_thumb : 
                       &client->video_thumb;
    
    pthread_mutex_lock(&client->pool_mutex);

    Connection* conn = &pool->connections[pool->current_conn];

    cycle_connection(pool);

    pthread_mutex_unlock(&client->pool_mutex);

    return conn;
}