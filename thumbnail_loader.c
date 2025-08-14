#include "include/thumbnail_loader.h"

#include "include/raw_thumbnail.h"
#include "include/utils.h"
#include "include/threads.h"
#include "include/request_config.h"
#include <string.h>

ThumbnailLoader thumbnail_loader_init(const size_t nconns)
{
    const char* video_thumb_host = media_type_to_thumbnail_host(MEDIA_TYPE_VIDEO);
    const char* channel_thumb_host = media_type_to_thumbnail_host(MEDIA_TYPE_CHANNEL);
    
    ThumbnailLoader loader = {
        .thumbail_queue = init_list(),
        .video_thumbnail_pool = connection_pool_init(video_thumb_host, HTTPS_PORT, nconns),
        .channel_thumbnail_pool = connection_pool_init(channel_thumb_host, HTTPS_PORT, nconns),
    };

    pthread_mutex_init(&loader.pool_mutex, NULL);
    
    return loader;
}

void thumbnail_loader_free(ThumbnailLoader* loader)
{
    if (loader == NULL) return;

    free_list(&loader->thumbail_queue);
    pthread_mutex_destroy(&loader->pool_mutex);
    connection_pool_free(&loader->video_thumbnail_pool);
    connection_pool_free(&loader->channel_thumbnail_pool);
}

Connection* thumbnail_loader_get_connection(ThumbnailLoader* loader, const MediaType media_type)
{
    if (loader == NULL)
        return NULL;

    pthread_mutex_lock(&loader->pool_mutex);
    
    ConnectionPool* pool = (media_type == MEDIA_TYPE_CHANNEL) ? 
                           &loader->channel_thumbnail_pool : 
                           &loader->video_thumbnail_pool;

    Connection* conn = &pool->connections[pool->current_conn];

    cycle_connection(pool);

    pthread_mutex_unlock(&loader->pool_mutex);

    return conn;
}

Texture load_texture_from_memory(const Buffer* image_buffer, const char* image_extension, const float width, const float height)
{
    Texture texture = {0};

    if (!buffer_is_ready(image_buffer) || !valid_string(image_extension)) 
        return texture;

    Image image = LoadImageFromMemory(image_extension, (const unsigned char*) image_buffer->data, image_buffer->size);
    if (IsImageReady(image)) {
        ImageResize(&image, width, height);
        texture = LoadTextureFromImage(image);
        UnloadImage(image);
    }

    return texture;
}

void thumbnail_loader_process_raw_images(ThumbnailLoader* loader, TextureCache* texture_cache)
{
    if (loader == NULL) 
        return;

    List* queue = &loader->thumbail_queue;

    pthread_mutex_lock(&queue->mutex);
    
    while(queue->head) {
        Node* node = dequeue_list(queue);

        RawThumbnail* raw = (RawThumbnail*) node->content;

        const float thumbnail_w = media_type_to_thumbnail_width(raw->media_type);
        const float thumbnail_h = media_type_to_thumbnail_height(raw->media_type);

        const Texture thumbnail = load_texture_from_memory(&raw->data, ".jpeg", thumbnail_w, thumbnail_h);

        TextureCacheEntry* entry = texture_cache_entry_init(thumbnail, raw->id);
        if (texture_cache_entry_is_ready(entry)) 
            texture_cache_add_entry(texture_cache, entry);

        free_node(node);
    }

    pthread_mutex_unlock(&queue->mutex);
}

bool queue_thumbnail_load(SSL_CTX* ssl_ctx, ThumbnailLoader* loader, List* task_queue, MediaType media_type, const char* id, const char* path)
{
    if ((ssl_ctx == NULL) || 
        (loader == NULL) || 
        (task_queue == NULL) || 
        (!valid_string(id)) || 
        (!valid_string(path))) {
        fprintf(stderr, "queue_thumbnail_load: invalid args\n");
        return false;
    }

    LoadThumbnailArgs* targs = malloc(sizeof(LoadThumbnailArgs));
    if (targs == NULL) {
        fprintf(stderr, "queue_thumbnail_load: malloc returned null\n");
        return false;
    }

    targs->ssl_ctx = ssl_ctx;
    targs->media_type = media_type;
    targs->loader = loader;
    strncpy(targs->id, id, sizeof(targs->id));
    strncpy(targs->path, path, sizeof(targs->path));

    return launch_task(task_queue, targs, load_thumbnail);
}

void* load_thumbnail(void* args)
{
    LoadThumbnailArgs* targs = (LoadThumbnailArgs*) args;
    if ((targs == NULL) || 
        (targs->loader == NULL) || 
        (targs->ssl_ctx == NULL) || 
        (!valid_string(targs->path) || 
        (!valid_string(targs->id)))) {
        fprintf(stderr, "load_thumbnail: invalid args\n");
        return NULL;
    }
    
    Connection* thumb_conn = thumbnail_loader_get_connection(targs->loader, targs->media_type);
    if (thumb_conn == NULL) {
        fprintf(stderr, "load_thumbnail: thumbnail connection is null\n");
        return NULL;
    }

    HttpsRequest req = {0};
    if (!configure_get_header(req.header, sizeof(req.header), thumb_conn->host, targs->path)) {
        fprintf(stderr, "load_thumbnail: failed to resolve request header\n");
        return NULL;
    }

    Buffer image_data = get_https_response(req, targs->ssl_ctx, thumb_conn, HTTP_PROTOCOL_VER);
    if (!buffer_is_ready(&image_data)) {
        fprintf(stderr, "load_thumbnail: image data is invalid\n");
        return NULL;
    }
    
    Node* node = init_node((void*) raw_thumbnail_init, NULL, (void*) raw_thumbnail_free, NULL);
    if (!node) {
        fprintf(stderr, "load_thumbnail: failed to initalize node\n");
        buffer_free(&image_data);
        return NULL;
    }

    RawThumbnail* raw_image = (RawThumbnail*) node->content;

    raw_image->next = NULL;
    raw_image->data = image_data;
    raw_image->media_type = targs->media_type;
    strncpy(raw_image->id, targs->id, sizeof(raw_image->id));

    List* thumbail_queue = &targs->loader->thumbail_queue;

    pthread_mutex_lock(&thumbail_queue->mutex);
    append_list(thumbail_queue, node);
    pthread_mutex_unlock(&thumbail_queue->mutex);

    return NULL;
}