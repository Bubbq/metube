#ifndef QUERY_H
#define QUERY_H

#include <stdbool.h>

typedef enum
{
    SEARCH_RESULT_TYPE_LIVE,
    SEARCH_RESULT_TYPE_SHORT,
    SEARCH_RESULT_TYPE_VIDEO,
    SEARCH_RESULT_TYPE_CHANNEL,
    SEARCH_RESULT_TYPE_PLAYLIST,
    SEARCH_RESULT_TYPE_ANY,
    SEARCH_RESULT_TYPE_UNDF,
} SearchResultType;

const char* search_result_type_to_text(const SearchResultType search_result_type);
const char* search_result_type_to_search_param(SearchResultType search_result_type);
const char* search_result_type_to_thumbnail_host(const SearchResultType search_result_type);
const float search_result_type_to_thumbnail_width(const SearchResultType search_result_type);
const float search_result_type_to_thumbnail_height(const SearchResultType search_result_type);

typedef enum 
{
    SORT_TYPE_RATING,
    SORT_TYPE_RELEVANCE,
    SORT_TYPE_VIEW_COUNT,
    SORT_TYPE_UPLOAD_DATE,
} SortType; 

const char* sort_type_to_search_param(const SortType sort_type);
const char* sort_type_to_text(const SortType sort_type);

typedef enum
{
    QUERY_ATTR_REPLACE,
    QUERY_ATTR_APPEND,
} QueryAttribute;

const char* query_attr_to_text(const QueryAttribute query_attr);

typedef enum
{
    QUERY_TYPE_USER_INPUT,  
    QUERY_TYPE_VIEW_VIDEO,
    QUERY_TYPE_VIEW_RELATED,  
    QUERY_TYPE_VIEW_CHANNEL,
    QUERY_TYPE_VIEW_PLAYLIST,
    QUERY_TYPE_VIEW_WATCH_HISTORY,
    QUERY_TYPE_VIEW_LIKED_VIDEOS,
    QUERY_TYPE_VIEW_SUBSCRIBED_CHANNELS,
} QueryType;

const char* query_type_to_endpoint(const QueryType query_type);
const char* query_type_to_text(const QueryType query_type);

typedef struct
{
    char string[256];        
    char focused_id[64];     
    SortType sort;
    QueryType type;
    SearchResultType media;          
    QueryAttribute attr;    
    bool allow_youtube_shorts; 
} Query;

#endif