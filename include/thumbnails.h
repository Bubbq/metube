#ifndef THUMBNAILS_H
#define THUMBNAILS_H

#include "query.h"
#include "buffer.h"
#include "connection.h"
#include "linked_list.h"
#include "texture_cache.h"

#define YOUTUBE_THUMBNAIL_EXTENSION ".jpeg"

typedef struct RawThumbnail
{
    char id[64] ;     
    Buffer image_data ;              
    SearchResultType search_result_type ;
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
    SearchResultType search_result_type ;
} LoadThumbnailArgs ;

void * load_thumbnail(void * args) ;

#endif