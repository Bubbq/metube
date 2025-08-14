#ifndef QUERY_OPS_H
#define QUERY_OPS_H

#include "list.h"
#include "query.h"
#include "https_utils.h"
#include "client_context.h"
#include "thumbnail_loader.h"
#include "highlighted_video.h"
#include "highlighted_channel.h"

typedef struct
{
    HttpsRequest req;
    SSL_CTX* ssl_ctx;
    ClientContext* client_ctx;
    HighlightedVideo* highlighted_video;
} VideoMetadataArgs;

void* get_video_metadata(void* args);

typedef struct
{
    Query query;
    List* results;
    SSL_CTX* ssl_ctx;
    ClientContext* client_ctx;
} SearchThreadArgs;

void* get_results_from_query(void* args);

typedef struct
{
    Query query;
    ThumbnailLoader* thumbnail_loader;
    cJSON* subscribed_channels_json;
    HighlightedChannel* channel;
    ClientContext* client_ctx;
    SSL_CTX* ssl_ctx;
    List* results;
} ChannelMetadataArgs;

void* get_channel_metadata(void* args);

#endif