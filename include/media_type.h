#ifndef MEDIA_TYPE_H
#define MEDIA_TYPE_H

typedef enum
{
    MEDIA_TYPE_LIVE,
    MEDIA_TYPE_SHORT,
    MEDIA_TYPE_VIDEO,
    MEDIA_TYPE_CHANNEL,
    MEDIA_TYPE_PLAYLIST,
    MEDIA_TYPE_ANY,
    MEDIA_TYPE_UNDF,
} MediaType;

const char* media_type_to_search_param(MediaType media_type);
const char* media_type_to_thumbnail_host(const MediaType media_type);
const char* media_type_to_text(const MediaType media_type);
const float media_type_to_thumbnail_width(const MediaType media_type);
const float media_type_to_thumbnail_height(const MediaType media_type);

#endif