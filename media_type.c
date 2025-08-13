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

const float media_type_to_thumbnail_width(const MediaType media_type)
{
    switch (media_type) {
        case MEDIA_TYPE_LIVE:
        case MEDIA_TYPE_SHORT:
        case MEDIA_TYPE_VIDEO:
        case MEDIA_TYPE_PLAYLIST: return 150.0f;
        case MEDIA_TYPE_CHANNEL: return 75.0f;
        default:
            fprintf(stderr, "media_type_to_thumbnail_width: MediaType %d is not valid\n", media_type);
            return 0;
    }
}

const float media_type_to_thumbnail_height(const MediaType media_type)
{
    switch (media_type) {
        case MEDIA_TYPE_LIVE:
        case MEDIA_TYPE_SHORT:
        case MEDIA_TYPE_VIDEO:
        case MEDIA_TYPE_PLAYLIST: return 80.0f;
        case MEDIA_TYPE_CHANNEL: return 70.0f;
        default:
            fprintf(stderr, "media_type_to_thumbnail_height: MediaType %d is not valid\n", media_type);
            return 0;
    }
}