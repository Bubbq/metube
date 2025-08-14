#ifndef THUMBNAIL_LOADER_H
#define THUMBNAIL_LOADER_H

#include "list.h"
#include "buffer.h"
#include "connection.h"
#include "texture_cache.h"

#include "raylib.h"

#include <openssl/types.h>

typedef struct
{
    List thumbail_queue;   
    pthread_mutex_t pool_mutex; // MIGHT NOT NEED, TEST FURTHER
    ConnectionPool video_thumbnail_pool;
    ConnectionPool channel_thumbnail_pool;
} ThumbnailLoader;

ThumbnailLoader thumbnail_loader_init(const size_t nconns);
void thumbnail_loader_free(ThumbnailLoader* loader);
void thumbnail_loader_process_raw_images(ThumbnailLoader* loader, TextureCache* texture_cache);
Connection* thumbnail_loader_get_connection(ThumbnailLoader* loader, const MediaType media_type);
Texture load_texture_from_memory(const Buffer* image_buffer, const char* image_extension, const float width, const float height);

typedef struct 
{
    char path[256];
    char id[64];
    ThumbnailLoader* loader;
    SSL_CTX* ssl_ctx;
    MediaType media_type;
} LoadThumbnailArgs;

bool queue_thumbnail_load(SSL_CTX* ssl_ctx, ThumbnailLoader* loader, List* task_queue, MediaType media_type, const char* id, const char* path);
void* load_thumbnail(void* args);

#endif