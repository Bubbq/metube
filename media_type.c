#include "include/media_type.h"

#include <stdio.h>

const char* media_type_to_search_param(MediaType media_type)
{
    switch (media_type) {
        case MEDIA_TYPE_SHORT:
        case MEDIA_TYPE_VIDEO: return "SAhAB";
        case MEDIA_TYPE_CHANNEL: return "SAhAC";
        case MEDIA_TYPE_PLAYLIST: return "SAhAD";
        case MEDIA_TYPE_LIVE: return "SBBABQAE";
        case MEDIA_TYPE_ANY: return "%253D";
        default:
            fprintf(stderr, "media_type_to_search_param: invalid MediaType\n");
            return NULL;
    }
}

const char* media_type_to_thumbnail_host(const MediaType media_type)
{
    switch (media_type) {
        case MEDIA_TYPE_LIVE:
        case MEDIA_TYPE_SHORT:
        case MEDIA_TYPE_VIDEO: 
        case MEDIA_TYPE_PLAYLIST: return "i.ytimg.com";
        case MEDIA_TYPE_CHANNEL: return "yt3.ggpht.com";
        default:
            fprintf(stderr, "media_type_to_thumbnail_host: invalid MediaType\n");
            return NULL;
    }
}

const char* media_type_to_text(const MediaType media_type)
{
    switch (media_type) {
        case MEDIA_TYPE_ANY: return "ANY";
        case MEDIA_TYPE_UNDF: return "UNDF";
        case MEDIA_TYPE_LIVE: return "LIVE";
        case MEDIA_TYPE_SHORT: return "SHORT";
        case MEDIA_TYPE_VIDEO: return "VIDEO";
        case MEDIA_TYPE_CHANNEL: return "CHANNEL";
        case MEDIA_TYPE_PLAYLIST: return "PLAYLIST";
        default:
            fprintf(stderr, "media_type_to_text: invalid MediaType\n");
            return NULL;
    }
}

const Vector2 media_type_to_thumbnail_dim(const MediaType media_type)
{
    switch (media_type) {
        case MEDIA_TYPE_LIVE:
        case MEDIA_TYPE_SHORT:
        case MEDIA_TYPE_VIDEO:
        case MEDIA_TYPE_PLAYLIST: return (Vector2) { 150, 80 };
        case MEDIA_TYPE_CHANNEL:  return (Vector2) { 75, 70 };
        case MEDIA_TYPE_UNDF:
        case MEDIA_TYPE_ANY:      return (Vector2) { 0 }; 
    }
}