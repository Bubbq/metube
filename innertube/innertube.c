#include "include/innertube.h"

#include "../include/utils.h"
#include "include/data.h"
#include <pthread.h>
#include <stdio.h>

SearchThreadArgs * create_search_thread_args (Query query, SSL_CTX * ssl, LinkedList * results, ClientContext * client, YoutubeParseContext * parse) 
{
    SearchThreadArgs * args = calloc(1, sizeof(SearchThreadArgs)) ;

    if (args) {
        args->query = query ;
        args->ssl = ssl;
        args->results = results ;
        args->client = client ;
        args->parse = parse ;
    }

    return args ;
}

VideoMetadataArgs * create_video_metadata_args (SSL_CTX * ssl, const char * video_id, ClientContext * client, YoutubeSearchResult * dest, YoutubeParseContext * parse, LinkedList * current_likes)
{
    if ( !ssl || !valid_string(video_id) || !client || !parse || !dest)
        return NULL ;

    VideoMetadataArgs * args = calloc(1, sizeof(VideoMetadataArgs)) ;

    if (args) {
        args->ssl = ssl ;
        args->video_id = video_id ;
        args->client = client ;
        args->dest = dest ;
        args->parse  = parse ;
        args->current_likes = current_likes ;
    }

    return args ;
}

ChannelMetadataArgs * create_channel_metadata_args (SearchThreadArgs * sargs, YoutubeSearchResult * dest, LinkedList * current_subscriptions)
{
    if ( !sargs || !dest || !current_subscriptions)
        return NULL ;

    ChannelMetadataArgs * targs = calloc(1, sizeof(ChannelMetadataArgs)) ;

    if (targs) {
        targs->sargs = sargs ;
        targs->dest = dest ;
        targs->current_subscriptions = current_subscriptions ;
    }

    return targs ;
}

void free_channel_metadata_args (ChannelMetadataArgs * cargs)
{
    if (cargs) {
        if (cargs->sargs)
            free(cargs->sargs) ;

        free(cargs) ;
    }
}

static json * get_json_response (const HttpsRequest * req, SSL_CTX * ssl, Connection * conn, const char * protocol_ver)
{
    if ( !req || !ssl || !conn || !valid_string(protocol_ver)) 
        return NULL ;

    Buffer res = get_https_response((*req), ssl, conn, protocol_ver) ;

    if ( !buffer_is_ready(&res)) {
        fprintf(stderr, "get_json_response: invalid response recived\n") ;
        return NULL;
    }

    json * ret = json_create_from_mem(res.data) ;

    buffer_free(&res) ;
    
    return ret ;
}

json * get_innertube_response (SSL_CTX * ssl, ClientContext * client, Query * query)
{
    if ( !ssl || !client || !query)
        return NULL ;

    Connection * conn = connection_pool_get_current_conn(&client->conn_pool) ;

    if ( !conn) {
        fprintf(stderr, "get_innertube_response: failed to retrieve connection\n") ;
        return NULL ;
    }

    const char * host = conn->host ;
    const char * api_key = client->api_key ;
    const char * continuation_token = client->continuation_token ;

    HttpsRequest req = configure_innertube_request((*query), host, api_key, continuation_token) ;

    if ( !post_request_is_ready(req)) {
        fprintf(stderr, "get_innertube_response: failed to create POST request\n") ;
        return NULL ;
    }

    json * response = get_json_response(&req, ssl, conn, HTTP_PROTOCOL_VER) ;

    if (req.payload) {
        free(req.payload) ; req.payload = NULL ;
    }

    return response ;
}

void * get_results_from_query (void * args)
{
    SearchThreadArgs * targs = (SearchThreadArgs*) args ;

    if ( !targs || !targs->ssl || !targs->results || !targs->client || !targs->parse)
        return NULL ;

    json * response = get_innertube_response(targs->ssl, targs->client, &targs->query) ;

    if ( !response) {
        fprintf(stderr, "get_results_from_query: failed to retrieve youtube response\n") ;
        return NULL ;
    }

    pthread_mutex_t * token_mutex = &targs->client->token_mutex ;
    
    pthread_mutex_lock(token_mutex) ;

    get_youtube_search_results(response, targs->parse, targs->results, &targs->client->continuation_token, targs->query.type, targs->query.action) ;

    pthread_mutex_unlock(token_mutex) ;

    json_free(response) ; 

    return NULL ;
}

void * get_channel_metadata (void * args)
{
    ChannelMetadataArgs * targs = (ChannelMetadataArgs*) args ;

    if ( !targs || !targs->sargs || !targs->dest)
        return NULL ;

    json * response = get_innertube_response(targs->sargs->ssl, targs->sargs->client, &targs->sargs->query) ;

    if ( !response) {
        fprintf(stderr, "get_channel_metadata: failed to retrieve youtube response\n") ;
        return NULL ;
    }

    pthread_mutex_t * token_mutex = &targs->sargs->client->token_mutex ;
    
    pthread_mutex_lock(token_mutex) ;

    get_youtube_search_results(response, targs->sargs->parse, targs->sargs->results, &targs->sargs->client->continuation_token, targs->sargs->query.type, targs->sargs->query.action) ;

    pthread_mutex_unlock(token_mutex) ;

    PathTemplate * template = find_path_template(&targs->sargs->parse->template_list, response) ;

    if ( !template) {
        json * unknown_object = response->child ;
        char * name = unknown_object ? unknown_object->string : "" ;
        fprintf(stderr, "get_channel_metadata: unknown json result object: \"%s\"\n", name) ;
        json_free(response) ;
        return NULL ;
    }

    YoutubeSearchResult * dest = targs->dest ;

    if ( !parse_youtube_search_result(dest, response, template)) {
        fprintf(stderr, "get_channel_metadata: failed to parse highlighted channel\n") ;
        json_free(response) ; 
        return NULL ;
    }

    strcpy(dest->id, targs->sargs->query.focused_id) ;

    youtube_search_result_print(dest) ;

    pthread_mutex_lock(&targs->current_subscriptions->mutex) ;
    dest->is_subscribed = linked_list_find(targs->current_subscriptions, dest, (void*) youtube_search_result_equals) ;
    pthread_mutex_unlock(&targs->current_subscriptions->mutex) ;

    json_free(response) ; 

    return NULL ;
}

void * get_video_metadata (void * args)
{
    VideoMetadataArgs * targs = (VideoMetadataArgs*) args ;

    if ( !targs || !targs->ssl || !valid_string(targs->video_id) || !targs->client || !targs->parse || !targs->dest)
        return NULL ;

    Query query = {0} ;
    query.type = QUERY_TYPE_REPLACE ;
    query.action = QUERY_ACTION_VIDEO_PRESS ;

    strncpy(query.focused_id, targs->video_id, sizeof(query.focused_id)) ;
    
    query.focused_id[sizeof(query.focused_id) - 1] = '\0' ;

    json * response = get_innertube_response(targs->ssl, targs->client, &query) ;

    if ( !response) {
        fprintf(stderr, "get_video_metadata: failed to retrieve innertube response\n") ;
        return NULL ;
    }

    PathTemplateList * list = &targs->parse->template_list ;

    PathTemplate * template = find_path_template(list, response) ;

    if ( !template) {
        json * unknown_object = response->child ;
        char * name = unknown_object ? unknown_object->string : "" ;
        fprintf(stderr, "parse_youtube_search_result: unknown json result object: \"%s\"\n", name) ;
        json_free(response) ;
        return NULL ;
    }

    YoutubeSearchResult * dest = targs->dest ;

    if (dest->description) {
        free(dest->description) ; dest->description = NULL ;
    }

    if ( !parse_youtube_search_result(dest, response, template)) 
        fprintf(stderr, "get_video_metadata: failed to parse highligted video\n") ;

    pthread_mutex_lock(&targs->current_likes->mutex) ;
    dest->is_liked = linked_list_find(targs->current_likes, dest, (void*) youtube_search_result_equals) ;
    pthread_mutex_unlock(&targs->current_likes->mutex) ;

    json_free(response) ;
    
    return NULL ;
}