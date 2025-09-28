#include "include/thumbnails.h"

#include "include/utils.h"
#include "include/https_utils.h"

RawThumbnail * raw_thumbnail_init ()
{
    RawThumbnail* raw_thumbnail = malloc(sizeof(RawThumbnail)) ;
    
    if ( !raw_thumbnail) 
        return NULL ;

    raw_thumbnail->image_data = buffer_init() ;
    raw_thumbnail->search_result_type = SEARCH_RESULT_TYPE_UNDF ;
    memset(raw_thumbnail->id, 0, sizeof(raw_thumbnail->id)) ;

    return raw_thumbnail ;
}

void raw_thumbnail_free (RawThumbnail * raw_thumbnail)
{
    if ( !raw_thumbnail)
        return ;

    if (buffer_is_ready(&raw_thumbnail->image_data)) 
        buffer_free(&raw_thumbnail->image_data) ;

    free(raw_thumbnail); raw_thumbnail = NULL;
}

bool raw_thumbnail_is_ready (const RawThumbnail * raw_thumbnail)
{
    return (raw_thumbnail) && 
           (valid_string(raw_thumbnail->id)) && 
           (buffer_is_ready(&raw_thumbnail->image_data)) && 
           (raw_thumbnail->search_result_type != SEARCH_RESULT_TYPE_UNDF) ;
}

bool thumbnail_loader_init (ThumbnailLoader * thumbnail_loader, const size_t nconns)
{
    if ( !thumbnail_loader)
        return false ;

    const char * video_thumb_host = search_result_type_to_thumbnail_host(SEARCH_RESULT_TYPE_VIDEO) ;
    const char * channel_thumb_host = search_result_type_to_thumbnail_host(SEARCH_RESULT_TYPE_CHANNEL) ;
    
    thumbnail_loader->thumbail_queue = linked_list_init() ;
    
    if ( !connection_pool_init(&thumbnail_loader->video_thumbnail_pool, video_thumb_host, HTTPS_PORT, nconns) || 
         !connection_pool_init(&thumbnail_loader->channel_thumbnail_pool, channel_thumb_host, HTTPS_PORT, nconns))
        return false ;

    return true ;
}

void thumbnail_loader_free (ThumbnailLoader * loader)
{
    if ( !loader) 
        return ;

    linked_list_free(&loader->thumbail_queue) ;
    connection_pool_free(&loader->video_thumbnail_pool) ;
    connection_pool_free(&loader->channel_thumbnail_pool) ;
}

static const Texture load_texture_from_memory (const Buffer * image_buffer, const char * image_extension, const float width, const float height)
{
    Texture texture = {0} ;

    if ( !buffer_is_ready(image_buffer) || !valid_string(image_extension)) 
        return texture ;

    Image image = LoadImageFromMemory(image_extension, (const unsigned char*) image_buffer->data, image_buffer->size) ;
    if (IsImageReady(image)) {
        ImageResize(&image, width, height) ;
        texture = LoadTextureFromImage(image) ;
        UnloadImage(image) ;
    }

    return texture ;
}

static void process_raw_thumbnail (RawThumbnail * raw_thumbnail, TextureCache * texture_cache)
{
    if ( !raw_thumbnail_is_ready(raw_thumbnail))
        return ;   

    const float thumbnail_w = search_result_type_to_thumbnail_width(raw_thumbnail->search_result_type) ;
    const float thumbnail_h = search_result_type_to_thumbnail_height(raw_thumbnail->search_result_type) ;

    Texture thumbnail = load_texture_from_memory(&raw_thumbnail->image_data, YOUTUBE_THUMBNAIL_EXTENSION, thumbnail_w, thumbnail_h) ;

    TextureCacheEntry * entry = texture_cache_entry_init(thumbnail, raw_thumbnail->id) ;
    if (texture_cache_entry_is_ready(entry)) 
        texture_cache_add_entry(texture_cache, entry) ;
}

void thumbnail_loader_process_raw_images (ThumbnailLoader * loader, TextureCache * texture_cache)
{
    if ( !loader) 
        return;

    LinkedList * queue = &loader->thumbail_queue ;

    pthread_mutex_lock(&queue->mutex) ;
    
    while (queue->head) {
        Node * node = linked_list_dequeue(queue) ;
        RawThumbnail * raw_thumbnail = (RawThumbnail*) node->data ;
        process_raw_thumbnail(raw_thumbnail, texture_cache) ;
        node_free(node) ;
    }

    pthread_mutex_unlock(&queue->mutex) ;
}

void * load_thumbnail (void * args)
{
    LoadThumbnailArgs * targs = (LoadThumbnailArgs*) args ;
    if ((!targs) || 
        (!targs->loader) || 
        (!targs->ssl_ctx) || 
        (!valid_string(targs->path) || 
        (!valid_string(targs->id)))) {
        fprintf(stderr, "load_thumbnail: invalid args\n") ;
        return NULL ;
    }
    
    ConnectionPool * pool = targs->search_result_type == SEARCH_RESULT_TYPE_CHANNEL ? 
                                                        &targs->loader->channel_thumbnail_pool : 
                                                        &targs->loader->video_thumbnail_pool ;
    
    Connection * thumbnail_conn = connection_pool_get_current_conn(pool) ;
    if ( !thumbnail_conn) {
        fprintf(stderr, "load_thumbnail: thumbnail connection is null\n") ;
        return NULL ;
    }

    HttpsRequest req = {0} ;
    if ( !configure_get_header(req.header, sizeof(req.header), thumbnail_conn->host, targs->path)) {
        fprintf(stderr, "load_thumbnail: failed to resolve request header\n") ;
        return NULL ;
    }

    Buffer image_data = get_https_response(req, targs->ssl_ctx, thumbnail_conn, HTTP_PROTOCOL_VER) ;
    if (!buffer_is_ready(&image_data)) {
        fprintf(stderr, "load_thumbnail: image data is invalid\n") ;
        return NULL ;
    }
    
    RawThumbnail * raw_thumbnail = raw_thumbnail_init() ;
    if ( !raw_thumbnail) {
        buffer_free(&image_data) ;
        return NULL ;
    }

    raw_thumbnail->image_data = image_data ;
    raw_thumbnail->search_result_type = targs->search_result_type ;
    strncpy(raw_thumbnail->id, targs->id, sizeof(raw_thumbnail->id)) ;
    
    Node * node = node_init(raw_thumbnail, sizeof(RawThumbnail), (void*) raw_thumbnail_free, NULL) ;
    if ( !node) {
        raw_thumbnail_free(raw_thumbnail) ;
        buffer_free(&image_data) ;
        return NULL ;
    }

    LinkedList * thumbail_queue = &targs->loader->thumbail_queue ;

    pthread_mutex_lock(&thumbail_queue->mutex) ;
    linked_list_append(thumbail_queue, node) ;
    pthread_mutex_unlock(&thumbail_queue->mutex) ;

    return NULL ;
}