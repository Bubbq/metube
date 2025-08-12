#ifndef QUERY_H
#define QUERY_H

#include "media_type.h"

#include "raylib.h"

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
    QUERTY_ATTR_APPEND,
} QueryAttribute;

const char* query_attr_to_text(const QueryAttribute query_attr);

typedef enum
{
    QUERY_TYPE_USER_INPUT,  
    QUERY_TYPE_VIEW_VIDEO,
    QUERY_TYPE_VIEW_RELATED,  
    QUERY_TYPE_VIEW_CHANNEL,
    QUERY_TYPE_VIEW_PLAYLIST,
    QUERY_TYPE_VIEW_TRENDING, 
    QUERY_TYPE_VIEW_WATCH_HISTORY,
    QUERY_TYPE_VIEW_LIKED_VIDEOS,
    QUERY_TYPE_VIEW_SUBSCRIBED_CHANNELS,
} QueryType;

const char* query_type_to_endpoint(const QueryType search_type);
const char* query_type_to_text(const QueryType query_type);

typedef struct
{
    char string[256];        
    char focused_id[64];     
    SortType sort;
    QueryType type;
    MediaType media;          
    QueryAttribute attr;    
    bool allow_youtube_shorts; 
} Query;

#endif