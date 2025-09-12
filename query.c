#include "include/query.h"

#include <stdlib.h>

const char* search_result_type_to_text(const SearchResultType search_result_type)
{
    switch (search_result_type) {
        case SEARCH_RESULT_TYPE_ANY: 
            return "ANY";
        case SEARCH_RESULT_TYPE_UNDF: 
            return "UNDF";
        case SEARCH_RESULT_TYPE_LIVE: 
            return "LIVE";
        case SEARCH_RESULT_TYPE_SHORT: 
            return "SHORT";
        case SEARCH_RESULT_TYPE_VIDEO: 
            return "VIDEO";
        case SEARCH_RESULT_TYPE_CHANNEL: 
            return "CHANNEL";
        case SEARCH_RESULT_TYPE_PLAYLIST: 
            return "PLAYLIST";
        default:
            return NULL;
    }
}

const char* search_result_type_to_search_param(SearchResultType search_result_type)
{
    switch (search_result_type) {
        case SEARCH_RESULT_TYPE_SHORT:
        case SEARCH_RESULT_TYPE_VIDEO: 
            return "SAhAB";
        case SEARCH_RESULT_TYPE_CHANNEL: 
            return "SAhAC";
        case SEARCH_RESULT_TYPE_PLAYLIST: 
            return "SAhAD";
        case SEARCH_RESULT_TYPE_LIVE: 
            return "SBBABQAE";
        case SEARCH_RESULT_TYPE_ANY: 
            return "%253D";
        default:
            return NULL;
    }
}

const char* search_result_type_to_thumbnail_host(const SearchResultType search_result_type)
{
    switch (search_result_type) {
        case SEARCH_RESULT_TYPE_LIVE:
        case SEARCH_RESULT_TYPE_SHORT:
        case SEARCH_RESULT_TYPE_VIDEO: 
        case SEARCH_RESULT_TYPE_PLAYLIST: 
            return "i.ytimg.com";
        case SEARCH_RESULT_TYPE_CHANNEL: 
            return "yt3.ggpht.com";
        default:
            return NULL;
    }
}

const float search_result_type_to_thumbnail_width(const SearchResultType search_result_type)
{
    switch (search_result_type) {
        case SEARCH_RESULT_TYPE_LIVE:
        case SEARCH_RESULT_TYPE_SHORT:
        case SEARCH_RESULT_TYPE_VIDEO:
        case SEARCH_RESULT_TYPE_PLAYLIST: 
            return 150.0f;
        case SEARCH_RESULT_TYPE_CHANNEL: 
            return 75.0f;
        default:
            return -1.0f;
    }
}

const float search_result_type_to_thumbnail_height(const SearchResultType search_result_type)
{
    switch (search_result_type) {
        case SEARCH_RESULT_TYPE_LIVE:
        case SEARCH_RESULT_TYPE_SHORT:
        case SEARCH_RESULT_TYPE_VIDEO:
        case SEARCH_RESULT_TYPE_PLAYLIST: 
            return 80.0f;
        case SEARCH_RESULT_TYPE_CHANNEL: 
            return 70.0f;
        default:
            return -1.0f;
    }
}

const char* sort_type_to_search_param(const SortType sort_type)
{
    switch (sort_type) {
        case SORT_TYPE_RATING: 
            return "CAE";
        case SORT_TYPE_RELEVANCE: 
            return "CAA";
        case SORT_TYPE_VIEW_COUNT: 
            return "CAM";
        case SORT_TYPE_UPLOAD_DATE: 
            return "CAI";
        default:
            return NULL;
    }
}

const char* sort_type_to_text(const SortType sort_type)
{
    switch (sort_type) {
        case SORT_TYPE_RATING: 
            return "rating";
        case SORT_TYPE_VIEW_COUNT: 
            return "views"; 
        case SORT_TYPE_RELEVANCE: 
            return "relevance";
        case SORT_TYPE_UPLOAD_DATE: 
            return "newest";
        default:
            return NULL;
    }
}

const char* query_attr_to_text(const QueryAttribute query_attr)
{
    switch (query_attr) {
        case QUERY_ATTR_REPLACE: 
            return "replace";
        case QUERY_ATTR_APPEND: 
            return "append";
        default:
            return NULL;
    }
}

const char* query_type_to_endpoint(const QueryType query_type)
{
    switch (query_type) {
        case QUERY_TYPE_USER_INPUT: 
            return "search";
        case QUERY_TYPE_VIEW_VIDEO: 
            return "player";
        case QUERY_TYPE_VIEW_RELATED: 
            return "next";
        case QUERY_TYPE_VIEW_CHANNEL:
        case QUERY_TYPE_VIEW_PLAYLIST:
            return "browse";
        default:    
            return NULL;
    }
}

const char* query_type_to_text(const QueryType query_type)
{
    switch (query_type) {
        case QUERY_TYPE_USER_INPUT: 
            return "user_input";
        case QUERY_TYPE_VIEW_RELATED: 
            return "view_related";
        case QUERY_TYPE_VIEW_VIDEO: 
            return "view_video";
        case QUERY_TYPE_VIEW_CHANNEL: 
            return "view_channel";
        case QUERY_TYPE_VIEW_PLAYLIST: 
            return "view_playlist";
        default:
            return NULL;
    }
}