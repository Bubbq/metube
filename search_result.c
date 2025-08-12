#include "include/search_result.h"

#include <stdio.h>
#include <stdlib.h>

SearchResult* search_result_init()
{
    SearchResult* search_result = calloc(1, sizeof(SearchResult));
    if (search_result == NULL) {
        fprintf(stderr, "search_result_init: malloc returned null\n");
        return NULL;
    }

    search_result->media_type = MEDIA_TYPE_UNDF;
    search_result->thumbnail_loaded = false;
    
    return search_result;
}

void search_result_free(SearchResult* search_result)
{
    if (search_result == NULL) return;
 
    free(search_result); search_result = NULL;
}