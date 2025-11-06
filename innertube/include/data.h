#ifndef DATA_H
#define DATA_H

#include "query.h"

typedef enum {
    SEARCH_RESULT_TYPE_VIDEO,
    SEARCH_RESULT_TYPE_LIVE_VIDEO,
    SEARCH_RESULT_TYPE_PLAYLIST_VIDEO,
    SEARCH_RESULT_TYPE_HIGHLIGHTED_VIDEO,
    SEARCH_RESULT_TYPE_CHANNEL,
    SEARCH_RESULT_TYPE_PLAYLIST,
    SEARCH_RESULT_TYPE_UNDF,
    SEARCH_RESULT_TYPE_COUNT,
} SearchResultType;

char * result_type_to_text (const SearchResultType type) ;

typedef struct {
    char title[128];                
    char thumbnail_path[128];      
    char id[64];                   
    char author_id[64];            
    char upload_date[32];  
    char * description;        
    SearchResultType type;       
    int playlist_length;          
    int subscriber_count;         
    int view_count;              
    int duration;                 
    bool is_thumbnail_loaded;   
    bool is_subscribed;   
    bool is_liked;
} YoutubeSearchResult;

YoutubeSearchResult * youtube_search_result_init  () ;
void  youtube_search_result_free  (YoutubeSearchResult * result) ;
void  youtube_search_result_print (const YoutubeSearchResult * result) ;
bool  youtube_search_result_equals (const YoutubeSearchResult * a, const YoutubeSearchResult * b) ;

typedef enum {
    FIELD_ID,
    FIELD_TITLE,
    FIELD_DURATION,
    FIELD_AUTHOR_ID,
    FIELD_VIEW_COUNT,
    FIELD_LIVE_VIEW_COUNT,
    FIELD_PLAYLIST_LENGTH,
    FIELD_UPLOAD_DATE,
    FIELD_THUMBNAIL_PATH,
    FIELD_SUBSCRIBER_COUNT,
    FIELD_DESCRIPTION,
    FIELD_SEARCH_RESULT_TYPE,
    FIELD_COUNT,
} YoutubeResultField ;

char * field_to_text (const YoutubeResultField field) ;

typedef enum {
    RESPONSE_RICH_ITEM_RENDERER,
    RESPONSE_VIDEO_RENDERER,
    RESPONSE_CHANNEL_RENDERER, 
    RESPONSE_LOCKUP_VIEW_MODEL,
    RESPONSE_PLAYLIST_VIDEO_RENDERER,
    RESPONSE_HIGHLIGHTED_VIDEO_RENDERER,
    RESPONSE_HIGHLIGHTED_CHANNEL_RENDERER,
    RESPONSE_USER_DATA,
    RESPONSE_COUNT,
} YoutubeJSONResponse ;

char * response_type_to_text (const YoutubeJSONResponse type) ;

#endif