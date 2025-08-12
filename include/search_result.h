#ifndef SEARCH_RESULT_H
#define SEARCH_RESULT_H

#include "media_type.h"

typedef struct
{
    char thumbnail_path[256];    
    char title[256];                    
    char authorId[64];         
    char id[64];                                
    char subscriber_count[32];  
    char date_published[32];      
    char video_count[32];            
    char view_count[16];              
    char duration[16];                   
    MediaType media_type;        
    bool thumbnail_loaded;
} SearchResult;

SearchResult* search_result_init();
void search_result_free(SearchResult* search_result);

#endif