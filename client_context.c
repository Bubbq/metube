#include "include/client_context.h"

ClientContext client_context_init(const size_t nconns)
{
    ClientContext ctx = {
        .youtube_api_pool = connection_pool_init("www.youtube.com", HTTPS_PORT, nconns),
        .api_key = "AIzaSyAO_FJ2SlqU8Q4STEHLGCilw_Y9_11qcW8", // web inner api key of youtube, public
        .continuation_token = NULL,
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

    if (client->continuation_token) {
        free(client->continuation_token); client->continuation_token = NULL;
    }

    pthread_mutex_destroy(&client->token_mutex);
    pthread_mutex_destroy(&client->pool_mutex);
}