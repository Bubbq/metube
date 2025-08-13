#ifndef CLIENT_CONTEXT_H
#define CLIENT_CONTEXT_H

#include "media_type.h"
#include "connection.h"

#define HTTPS_PORT "443"

typedef struct
{
    pthread_mutex_t token_mutex; 
    pthread_mutex_t pool_mutex; 
    ConnectionPool youtube_api_pool;
    ConnectionPool channel_thumb;
    ConnectionPool video_thumb;
    char* continuation_token;
    char* api_key;
    SSL_CTX* ssl_ctx;
} ClientContext;

ClientContext client_context_init();
void client_context_free(ClientContext* client);
Connection* client_context_get_thumbnail_connection(ClientContext* client, const MediaType media_type);

#endif