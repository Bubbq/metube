#include "include/data.h"

char * result_type_to_text (const SearchResultType type)
{
    switch (type) {
        case SEARCH_RESULT_TYPE_VIDEO: return "VIDEO" ;
        case SEARCH_RESULT_TYPE_LIVE_VIDEO: return "LIVE_VIDEO" ;
        case SEARCH_RESULT_TYPE_PLAYLIST_VIDEO: return "PLAYLIST_VIDEO" ;
        case SEARCH_RESULT_TYPE_HIGHLIGHTED_VIDEO: return "HIGHLIGHTED_VIDEO" ;
        case SEARCH_RESULT_TYPE_CHANNEL: return "CHANNEL" ;
        case SEARCH_RESULT_TYPE_PLAYLIST: return "PLAYLIST" ;
        case SEARCH_RESULT_TYPE_UNDF: return "UNDF" ;
        default:
            return NULL ;
    }
}

YoutubeSearchResult * youtube_search_result_init ()
{
    YoutubeSearchResult * result = calloc(1, sizeof(YoutubeSearchResult)) ;

    if (result)
        result->type = SEARCH_RESULT_TYPE_UNDF ;
    
    return result ;
}

void youtube_search_result_free (YoutubeSearchResult * result)
{
    if (result) {
        if (result->description) 
            free(result->description) ;

        free(result) ; result = NULL ;
    }
}

void youtube_search_result_print (const YoutubeSearchResult * result)
{
    if (result) {
        printf("ID: %s, TITLE: %s, AUTHOR_ID: %s, VIEW_COUNT: %d, PLAYLIST_LENGTH: %d, UPLOAD_DATE: %s, THUMBNAIL_PATH: %s, SUBSCRIBER_COUNT: %d, DURATION: %d\n", 
            result->id, 
            result->title, 
            result->author_id,
            result->view_count,
            result->playlist_length,
            result->upload_date,
            result->thumbnail_path,
            result->subscriber_count,
            result->duration) ;
    }
}

char * field_to_text (const YoutubeResultField field)
{
    switch (field) {
        case FIELD_ID: return "ID" ;
        case FIELD_TITLE: return "TITLE" ;
        case FIELD_DURATION: return "DURATION" ;
        case FIELD_AUTHOR_ID: return "AUTHOR_ID" ;
        case FIELD_VIEW_COUNT: return "VIEW_COUNT" ;
        case FIELD_LIVE_VIEW_COUNT: return "LIVE_VIEW_COUNT" ;
        case FIELD_PLAYLIST_LENGTH: return "PLAYLIST_LENGTH" ;
        case FIELD_UPLOAD_DATE: return "UPLOAD_DATE" ;
        case FIELD_THUMBNAIL_PATH: return "THUMBNAIL_PATH" ;
        case FIELD_SUBSCRIBER_COUNT: return "SUBSCRIBER_COUNT" ;
        case FIELD_DESCRIPTION: return "DESCRIPTION" ;
        default:
            return NULL ;
    }
}

char * response_type_to_text (const YoutubeJSONResponse type)
{
    switch (type) {
        case RESPONSE_RICH_ITEM_RENDERER: return "richItemRenderer" ;
        case RESPONSE_VIDEO_RENDERER: return "videoRenderer" ;
        case RESPONSE_CHANNEL_RENDERER: return "channelRenderer" ;
        case RESPONSE_LOCKUP_VIEW_MODEL: return "lockupViewModel" ;
        case RESPONSE_PLAYLIST_VIDEO_RENDERER: return "playlistVideoRenderer" ;
        case RESPONSE_HIGHLIGHTED_VIDEO_RENDERER: return "highlightedVideoRenderer" ;
        case RESPONSE_HIGHLIGHTED_CHANNEL_RENDERER: return "highlightedChannelRenderer" ;
        default:
            return NULL ;    
    }
}