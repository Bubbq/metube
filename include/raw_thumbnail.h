#ifndef RAW_THUMBNAIL_H
#define RAW_THUMBNAIL_H

#include "buffer.h"
#include "media_type.h"
#include "texture_cache.h"

typedef struct RawThumbnail
{
    char id[256];     
    Buffer data;              
    struct RawThumbnail *next;
    MediaType media_type;
} RawThumbnail;

RawThumbnail* raw_thumbnail_init();
void raw_thumbnail_free(RawThumbnail* raw_thumbnail);
void process_raw_thumbnail(RawThumbnail* raw_thumbnail, TextureCache* texture_cache);
Texture load_texture_from_memory(const Buffer buffer, const float width, const float height);

#endif