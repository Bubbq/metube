#ifndef INNERTUBE_H
#define INNERTUBE_H

#include "query.h"
#include "parse.h"

typedef struct {
    Query query;
    SSL_CTX * ssl;
    LinkedList * results;
    ClientContext * client;
    YoutubeParseContext * parse;
} SearchThreadArgs;

typedef struct {
    SSL_CTX * ssl;
    const char * video_id;
    ClientContext * client;
    YoutubeSearchResult * dest;
    YoutubeParseContext * parse;
} VideoMetadataArgs;

typedef struct {
    SearchThreadArgs * sargs;
    YoutubeSearchResult * dest;
} ChannelMetadataArgs;

SearchThreadArgs * create_search_thread_args (Query query, SSL_CTX * ssl, LinkedList * results, ClientContext * client, YoutubeParseContext * parse) ;
VideoMetadataArgs * create_video_metadata_args (SSL_CTX * ssl, const char * video_id, ClientContext * client, YoutubeSearchResult * dest, YoutubeParseContext * parse) ;
ChannelMetadataArgs * create_channel_metadata_args (SearchThreadArgs * sargs, YoutubeSearchResult * dest) ;
void free_channel_metadata_args (ChannelMetadataArgs * cargs) ;

cJSON * get_innertube_response (SSL_CTX * ssl, ClientContext * client, Query * query) ;
void  * get_results_from_query (void * args) ;
void  * get_channel_metadata (void * args) ;
void  * get_video_metadata (void * args) ;

#endif