#ifndef THUMBNAILS_H
#define THUMBNAILS_H

#include "buffer.h"
#include "connection.h"
#include "linked_list.h"
#include "texture_cache.h"

#define YOUTUBE_THUMBNAIL_EXTENSION ".jpeg"

typedef enum {
    MEDIA_VIDEO,
    MEDIA_CHANNEL,
    MEDIA_PLAYLIST,
    MEDIA_COUNT,
} MediaType ;

char * media_to_thumbnail_host (const MediaType media) ;

typedef struct {
    float width;
    float height;
} ThumbnailDimension;

ThumbnailDimension media_type_to_dimensions (const MediaType media) ;

typedef struct RawThumbnail
{
    char id[64] ;     
    Buffer image_data ;              
    MediaType media_type ;
} RawThumbnail ;

RawThumbnail * raw_thumbnail_init     () ;
void           raw_thumbnail_free     (RawThumbnail * raw_thumbnail) ;
bool           raw_thumbnail_is_ready (const RawThumbnail* raw) ;

typedef struct
{
    LinkedList thumbail_queue ;   
    ConnectionPool video_thumbnail_pool ;
    ConnectionPool channel_thumbnail_pool ;
} ThumbnailLoader ;

bool thumbnail_loader_init (ThumbnailLoader * thumbnail_loader, const size_t nconns) ;
void thumbnail_loader_free (ThumbnailLoader * loader) ;
void thumbnail_loader_process_raw_images(ThumbnailLoader * loader, TextureCache * texture_cache) ;

typedef struct 
{
    char path[256] ;
    char id[64] ;
    SSL_CTX * ssl_ctx ;
    ThumbnailLoader * loader ;
    MediaType media_type ;
} LoadThumbnailArgs ;

LoadThumbnailArgs * create_load_thumbnail_args (const char * path, const char * id, SSL_CTX * ssl, ThumbnailLoader * loader, const MediaType type) ;

void * load_thumbnail(void * args) ;

#endif