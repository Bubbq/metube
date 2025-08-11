#include "include/query.h"

#include <stdio.h>
#include <stdlib.h>

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

const char* sort_type_to_search_param(const SortType sort_type)
{
    switch (sort_type) {
        case SORT_TYPE_RATING: return "CAE";
        case SORT_TYPE_RELEVANCE: return "CAA";
        case SORT_TYPE_VIEW_COUNT: return "CAM";
        case SORT_TYPE_UPLOAD_DATE: return "CAI";
        default:
            fprintf(stderr, "sort_type_to_search_param: invalid SortType\n");
            return NULL;
    }
}

const char* sort_type_to_text(const SortType sort_type)
{
    switch (sort_type) {
        case SORT_TYPE_RATING: return "RATING";
        case SORT_TYPE_VIEW_COUNT: return "VIEWS"; 
        case SORT_TYPE_RELEVANCE: return "RELEVENCE";
        case SORT_TYPE_UPLOAD_DATE: return "UPLOAD DATE";
        default:
            fprintf(stderr, "sort_type_to_text: invalid SortType\n");
            return NULL;
    }
}

const char* query_attr_to_text(const QueryAttribute query_attr)
{
    switch (query_attr) {
        case QUERY_ATTR_REPLACE: return "NEW";
        case QUERTY_ATTR_APPEND: return "APPENDING";
        default:
            fprintf(stderr, "query_attr_to_text: QueryAttribute %d is invalid\n", query_attr);
            return NULL;
    }
}

const char* query_type_to_endpoint(const QueryType search_type)
{
    switch (search_type) {
        case QUERY_TYPE_USER_INPUT: return "search";
        case QUERY_TYPE_VIEW_CHANNEL:
        case QUERY_TYPE_VIEW_PLAYLIST:
        case QUERY_TYPE_VIEW_TRENDING: return "browse";
        case QUERY_TYPE_VIEW_VIDEO: return "player";
        case QUERY_TYPE_VIEW_RELATED: return "next";
        default:    
            fprintf(stderr, "query_type_to_endpoint: invalid QueryType\n");
            return NULL;
    }
}

const char* query_type_to_text(const QueryType query_type)
{
    switch (query_type) {
        case QUERY_TYPE_USER_INPUT: return "QUERIED";
        case QUERY_TYPE_VIEW_RELATED: return "RELATED";
        case QUERY_TYPE_VIEW_TRENDING: return "TRENDING";
        case QUERY_TYPE_VIEW_VIDEO: return "VIDEO FOCUS";
        case QUERY_TYPE_VIEW_CHANNEL: return "VIEW CHANNEL";
        case QUERY_TYPE_VIEW_PLAYLIST: return "VIEW PLAYLIST";
        default:
            fprintf(stderr, "query_type_to_text: QueryType %d is invalid\n", query_type);
            return NULL;
    }
}