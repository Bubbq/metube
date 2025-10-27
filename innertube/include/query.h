#ifndef QUERY_H
#define QUERY_H

#include "../../include/ssl_utils.h"

typedef enum {
    FILTER_BY_VIDEO,
    FILTER_BY_LIVE_VIDEO,
    FILTER_BY_CHANNEL,
    FILTER_BY_PLAYLIST,
    FILTER_BY_ANY,
    FILTER_BY_COUNT,
} FilterBy ;

const char * filter_to_text (const FilterBy filter) ;
const char * filter_to_search_param (const FilterBy filter) ;
const char * filter_to_thumbnail_host (const FilterBy filter) ;

typedef enum  {
    SORT_BY_RATING,
    SORT_BY_RELEVANCE,
    SORT_BY_VIEW_COUNT,
    SORT_BY_UPLOAD_DATE,
    SORT_BY_COUNT,
} SortBy; 

const char * sort_to_text (const SortBy sort) ;
const char * sort_to_search_param (const SortBy sort) ;

typedef enum {
    QUERY_TYPE_REPLACE,
    QUERY_TYPE_APPEND,
    QUERY_TYPE_COUNT,
} QueryType;

const char * query_type_to_text (const QueryType type) ;

typedef enum {
    QUERY_ACTION_SEARCH,  
    QUERY_ACTION_VIDEO_PRESS,
    QUERY_ACTION_CHANNEL_PRESS,
    QUERY_ACTION_PLAYLIST_PRESS,
    QUERY_ACTION_VIEW_RELATED,  
    QUERY_ACTION_VIEW_HISTORY,
    QUERY_ACTION_VIEW_LIKES,
    QUERY_ACTION_VIEW_SUBSCRIPTIONS,
    QUERY_ACTION_PLAY_VIDEO,
    QUERY_ACTION_COUNT,
} QueryAction;

const char * query_action_to_endpoint (const QueryAction action) ;
const char * query_action_to_text (const QueryAction action) ;

typedef struct {
    char string[256] ;        
    char focused_id[64] ;     
    SortBy sort ;
    FilterBy filter ;          
    QueryType type ;
    QueryAction action ;    
} Query;

// macros for innertube POST requests

#define CLIENT_NAME "WEB"
#define CLIENT_VER "2.20250730"
#define YT_API_PLAYLIST_BROWSE_ID_PREFIX "VL"    
#define YT_API_CHANNEL_VIDEOS_PARAMS "EgZ2aWRlb3PyBgQKAjoA"  
#define INTERNAL_YOUTUBE_API_KEY "AIzaSyAO_FJ2SlqU8Q4STEHLGCilw_Y9_11qcW8"

HttpsRequest configure_innertube_request (const Query query, const char * host, const char * api_key, const char * continuation_token) ;

#endif