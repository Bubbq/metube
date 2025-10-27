#include "include/query.h"

#include "../include/utils.h"
#include "../include/json_utils.h"

const char * filter_to_text (const FilterBy filter) 
{
    switch (filter) {
        case FILTER_BY_VIDEO: return "video" ;
        case FILTER_BY_CHANNEL: return "channel" ;
        case FILTER_BY_PLAYLIST: return "playlist" ;
        case FILTER_BY_ANY: return "any" ;
        default:
            return NULL ;
    }
}

const char * filter_to_search_param (const FilterBy filter)
{
    switch (filter) {
        case FILTER_BY_VIDEO:  return "SAhAB" ;
        case FILTER_BY_LIVE_VIDEO: return "SBBABQAE" ;
        case FILTER_BY_CHANNEL: return "SAhAC" ; 
        case FILTER_BY_PLAYLIST: return "SAhAD" ;
        case FILTER_BY_ANY: return "%253D" ;
        default:
            return NULL ;
    }
}

const char * filter_to_thumbnail_host (const FilterBy filter)
{
    switch (filter) {
        case FILTER_BY_VIDEO:
        case FILTER_BY_LIVE_VIDEO:
        case FILTER_BY_PLAYLIST: return "i.ytimg.com" ;
        case FILTER_BY_CHANNEL:  return "yt3.ggpht.com" ;
        default:
            return NULL ;
    }
}

const char * sort_to_text (const SortBy sort)
{
    switch (sort) {
        case SORT_BY_RATING: return "rating" ;
        case SORT_BY_RELEVANCE: return "relevance" ;
        case SORT_BY_VIEW_COUNT: return "views" ; 
        case SORT_BY_UPLOAD_DATE: return "newest" ;
        default:
            return NULL ;
    }
}

const char * sort_to_search_param (const SortBy sort)
{
    switch (sort) {
        case SORT_BY_RATING: return "CAE" ;
        case SORT_BY_RELEVANCE: return "CAA" ;
        case SORT_BY_VIEW_COUNT: return "CAM" ;
        case SORT_BY_UPLOAD_DATE: return "CAI" ;
        default:
            return NULL ;
    }
}

const char * query_type_to_text (const QueryType type)
{
    switch (type) {
        case QUERY_TYPE_REPLACE: return "REPLACE" ;
        case QUERY_TYPE_APPEND: return "APPEND" ;
        default:
            return NULL ;
    }
}

const char * query_action_to_endpoint (const QueryAction action)
{
    switch (action) {
        case QUERY_ACTION_CHANNEL_PRESS:
        case QUERY_ACTION_PLAYLIST_PRESS: return "browse" ;
        case QUERY_ACTION_SEARCH: return "search" ;
        case QUERY_ACTION_VIDEO_PRESS: return "player" ;
        case QUERY_ACTION_VIEW_RELATED: return "next" ;
        default:
            return NULL ;
    }
}

const char * query_action_to_text (const QueryAction action)
{
    switch (action) {
        case QUERY_ACTION_SEARCH: return "SEARCH" ;
        case QUERY_ACTION_VIDEO_PRESS: return "VIDEO_PRESS" ;
        case QUERY_ACTION_CHANNEL_PRESS: return "CHANNEL_PRESS" ;
        case QUERY_ACTION_PLAYLIST_PRESS: return "VIEW_PLAYLIST" ;
        case QUERY_ACTION_VIEW_RELATED: return "VIEW_RELATED" ;
        case QUERY_ACTION_VIEW_HISTORY: return "VIEW_HISTORY" ;
        case QUERY_ACTION_VIEW_LIKES: return "VIEW_LIKES" ;
        case QUERY_ACTION_VIEW_SUBSCRIPTIONS: return "VIEW_SUBSCRIPTIONS" ;
        default:
            return NULL ;
    }
}

static json * configure_client_object ()
{
    json * client = json_create_object() ;
    
    if ( !client) {
        fprintf(stderr, "configure_client_object: failed to create object\n") ;
        return NULL ;
    }

    if ( !json_add_string_to_object(client, "clientName", CLIENT_NAME)) {
        fprintf(stderr, "configure_client_object: failed to add 'clientName'\n") ;
        json_free(client) ;
        return NULL ;
    }

    if ( !json_add_string_to_object(client, "clientVersion", CLIENT_VER)) {
        fprintf(stderr, "configure_client_object: failed to add 'clientVersion'\n") ;
        json_free(client) ;
        return NULL ;
    }

    return client;
}

static json * configure_base_payload ()
{
    json * client = configure_client_object() ;

    if ( !client) {
        fprintf(stderr, "configure_base_payload: 'client' is NULL\n") ;
        return NULL ;
    }

    json * context = json_create_object() ;
    
    if ( !context) {
        fprintf(stderr, "configure_base_payload: 'context' is NULL\n") ;
        json_free(client) ;
        return NULL ;
    }
    
    if ( !json_add_item_to_object(context, "client", client)) {
        fprintf(stderr, "configure_base_payload: failed to add 'client' to 'context'\n") ;
        json_free(client) ;
        json_free(context) ; 
        return NULL ;
    }  
    
    json * root = json_create_object() ;
    
    if ( !root) {
        fprintf(stderr, "configure_base_payload: 'root' is NULL\n");
        json_free(context) ; 
        return NULL ;
    }

    if ( !json_add_item_to_object(root, "context", context)) {
        fprintf(stderr, "configure_base_payload: failed to add 'context' to 'root'\n") ;
        json_free(context) ; 
        json_free(root) ;
    }  

    return root; 
}

static bool add_continuation_payload (json * root, const char * continuation_token)
{
    return root && 
           valid_string(continuation_token) && 
           json_add_string_to_object(root, "continuation", continuation_token) ;
}

static bool add_view_user_input_payload (json * root, const char * query, const SortBy sort, const FilterBy filter)
{
    if ( !root || !valid_string(query))
        return false ;

    // const char * sort_param = sort_type_to_search_param(sort_type) ;
    const char * sort_param = sort_to_search_param(sort) ;

    if ( !sort_param) {
        fprintf(stderr, "add_view_user_input_payload: SortType %d is invalid\n", sort) ;
        return false ;
    }

    const char * media_param = filter_to_search_param(filter) ;
    
    if ( !media_param) {
        fprintf(stderr, "add_view_user_input_payload: SearchResultType %d is invalid\n", filter) ;
        return false ;
    }

    char params[32] = {0} ;
    
    const int written = snprintf(params, sizeof(params), "%s%s", sort_param, media_param) ;

    if ( (written < 0) || (written >= sizeof(params))) 
        return false ;

    return json_add_string_to_object(root, "query", query) && json_add_string_to_object(root, "params", params) ;
}

static bool add_video_id_payload (json * root, const char * video_id)
{
    return root && 
           valid_string(video_id) && 
           json_add_string_to_object(root, "videoId", video_id) ;
}

static bool add_view_channel_videos_payload (json * root, const char * channel_id)
{
    return root && 
           valid_string(channel_id) && 
           json_add_string_to_object(root, "browseId", channel_id) && 
           json_add_string_to_object(root, "params", YT_API_CHANNEL_VIDEOS_PARAMS) ;
}

static bool add_view_playlist_videos_payload (json * root, const char * playlist_id)
{
    if ( !root || !valid_string(playlist_id))
        return false ;

    char browseId[64] = {0} ;

    const int written = snprintf(browseId, sizeof(browseId),"%s%s", YT_API_PLAYLIST_BROWSE_ID_PREFIX, playlist_id) ;

    return (0 < written) && 
           (written < sizeof(browseId)) && 
           json_add_string_to_object(root, "browseId", browseId) ;
}

static json * configure_post_payload (const Query * query, const char * continuation_token)
{
    if ( !query) 
        return NULL ;

    json * root = configure_base_payload() ;
    
    if ( !root) {
        fprintf(stderr, "configure_post_payload: failed to create base payload\n") ;
        return NULL ;
    }

    bool success ;

    if (query->type == QUERY_TYPE_REPLACE) {
        switch (query->action) {
            case QUERY_ACTION_VIDEO_PRESS:
            case QUERY_ACTION_VIEW_RELATED:   success = add_video_id_payload(root, query->focused_id) ; break ;
            case QUERY_ACTION_SEARCH:         success = add_view_user_input_payload(root, query->string, query->sort, query->filter) ; break ;
            case QUERY_ACTION_CHANNEL_PRESS:  success = add_view_channel_videos_payload(root, query->focused_id) ; break ;
            case QUERY_ACTION_PLAYLIST_PRESS: success = add_view_playlist_videos_payload(root, query->focused_id) ; break ;
            default:
                fprintf(stderr, "configure_post_payload: QueryAction %d not supported\n", query->action) ;
        }
    }

    else if (query->type == QUERY_TYPE_APPEND) 
        success = add_continuation_payload(root, continuation_token) ;

    if ( !success) {
        fprintf(stderr, "configure_post_payload: failed to add payload with QueryAction %d and QueryType %d\n", query->action, query->type) ;
        json_free(root) ; 
    }

    return success ? root : NULL ;
}

static bool configure_innertube_api_path (char * dest, const size_t dest_size, QueryAction action, const char * key)
{
    if ( !dest || !valid_string(key))
        return false ;

    const char * endpoint = query_action_to_endpoint(action) ;

    if ( !endpoint) {
        fprintf(stderr, "configure_innertube_api_path: QueryType %d is invalid\n", action) ;
        return false ;
    }

    const int written = snprintf(dest, dest_size, "/youtubei/v1/%s?key=%s", endpoint, key);

    return (written > 0) && (written < dest_size) ;
}

HttpsRequest configure_innertube_request (const Query query, const char * host, const char * api_key, const char * continuation_token)
{
    HttpsRequest req = {0} ;

    if ( !valid_string(host) || !valid_string(api_key))
        return req ;

    if ( !configure_innertube_api_path(req.path, sizeof(req.path), query.action, api_key)) {
        fprintf(stderr, "configure_post_request: failed to resolve innertube api path\n") ;
        return req ;
    }

    json * payload = configure_post_payload(&query, continuation_token) ;

    if ( !payload) {
        fprintf(stderr, "configure_post_request: 'payload' is NULL\n") ;
        return req ;
    }

    req.payload = json_print(payload) ;

    json_free(payload) ;

    if ( !configure_post_header(req.header, sizeof(req.header), host, req.path, strlen(req.payload))) {
        fprintf(stderr, "configure_post_request: failed to resolve header\n");
        if (req.payload) {
            free(req.payload) ; req.payload = NULL ;
        }
    }

    return req ;
}