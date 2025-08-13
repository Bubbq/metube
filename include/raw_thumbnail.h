#ifndef RAW_THUMBNAIL_H
#define RAW_THUMBNAIL_H

#include "buffer.h"
#include "media_type.h"

typedef struct RawThumbnail
{
    char id[256];     
    Buffer data;              
    struct RawThumbnail *next;
    MediaType media_type;
} RawThumbnail;

RawThumbnail* raw_thumbnail_init();
void raw_thumbnail_free(RawThumbnail* raw_thumbnail);

#endif