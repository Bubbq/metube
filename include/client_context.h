#ifndef CLIENT_CONTEXT_H
#define CLIENT_CONTEXT_H

#include "media_type.h"
#include "connection.h"

typedef struct
{
    pthread_mutex_t token_mutex; 
    pthread_mutex_t pool_mutex; 
    ConnectionPool youtube_api_pool;
    char* continuation_token;
    char* api_key;
} ClientContext;

ClientContext client_context_init(const size_t nconns);
void client_context_free(ClientContext* client);

#endif