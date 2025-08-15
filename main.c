#include "include/list.h"
#include "include/timer.h"
#include "include/utils.h"
#include "include/json_utils.h"
#include "include/https_utils.h"
#include "include/thread_utils.h"
#include "include/texture_cache.h"
#include <bits/pthreadtypes.h>
#include <cjson/cJSON.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RAYGUI_IMPLEMENTATION
#include "include/raygui.h"

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

const char* search_result_type_to_search_param(SearchResultType search_result_type)
{
    switch (search_result_type) {
        case SEARCH_RESULT_TYPE_SHORT:
        case SEARCH_RESULT_TYPE_VIDEO: 
            return "SAhAB";
        case SEARCH_RESULT_TYPE_CHANNEL: 
            return "SAhAC";
        case SEARCH_RESULT_TYPE_PLAYLIST: 
            return "SAhAD";
        case SEARCH_RESULT_TYPE_LIVE: 
            return "SBBABQAE";
        case SEARCH_RESULT_TYPE_ANY: 
            return "%253D";
        default:
            fprintf(stderr, "search_result_type_to_search_param: SearchResultType %d is invalid\n", search_result_type);
            return NULL;
    }
}

const char* search_result_type_to_thumbnail_host(const SearchResultType search_result_type)
{
    switch (search_result_type) {
        case SEARCH_RESULT_TYPE_LIVE:
        case SEARCH_RESULT_TYPE_SHORT:
        case SEARCH_RESULT_TYPE_VIDEO: 
        case SEARCH_RESULT_TYPE_PLAYLIST: 
            return "i.ytimg.com";
        case SEARCH_RESULT_TYPE_CHANNEL: 
            return "yt3.ggpht.com";
        default:
            fprintf(stderr, "search_result_type_to_thumbnail_host: SearchResultType %d is invalid\n", search_result_type);
            return NULL;
    }
}

const char* search_result_type_to_text(const SearchResultType search_result_type)
{
    switch (search_result_type) {
        case SEARCH_RESULT_TYPE_ANY: 
            return "ANY";
        case SEARCH_RESULT_TYPE_UNDF: 
            return "UNDF";
        case SEARCH_RESULT_TYPE_LIVE: 
            return "LIVE";
        case SEARCH_RESULT_TYPE_SHORT: 
            return "SHORT";
        case SEARCH_RESULT_TYPE_VIDEO: 
            return "VIDEO";
        case SEARCH_RESULT_TYPE_CHANNEL: 
            return "CHANNEL";
        case SEARCH_RESULT_TYPE_PLAYLIST: 
            return "PLAYLIST";
        default:
            fprintf(stderr, "search_result_type_to_text: SearchResultType %d is invalid\n", search_result_type);
            return NULL;
    }
}

const float search_result_type_to_thumbnail_width(const SearchResultType search_result_type)
{
    switch (search_result_type) {
        case SEARCH_RESULT_TYPE_LIVE:
        case SEARCH_RESULT_TYPE_SHORT:
        case SEARCH_RESULT_TYPE_VIDEO:
        case SEARCH_RESULT_TYPE_PLAYLIST: 
            return 150.0f;
        case SEARCH_RESULT_TYPE_CHANNEL: 
            return 75.0f;
        default:
            fprintf(stderr, "search_result_type_to_thumbnail_width: SearchResultType %d is invalid\n", search_result_type);
            return 0;
    }
}

const float search_result_type_to_thumbnail_height(const SearchResultType search_result_type)
{
    switch (search_result_type) {
        case SEARCH_RESULT_TYPE_LIVE:
        case SEARCH_RESULT_TYPE_SHORT:
        case SEARCH_RESULT_TYPE_VIDEO:
        case SEARCH_RESULT_TYPE_PLAYLIST: 
            return 80.0f;
        case SEARCH_RESULT_TYPE_CHANNEL: 
            return 70.0f;
        default:
            fprintf(stderr, "search_result_type_to_thumbnail_height: SearchResultType %d is invalid\n", search_result_type);
            return 0;
    }
}

typedef struct
{
    char thumbnail_path[256];    
    char title[256];                    
    char authorId[64];         
    char id[64];                                
    char subscriber_count[32];  
    char date_published[32];      
    char video_count[32];            
    char view_count[32];              
    char duration[16];                   
    SearchResultType search_result_type;        
    bool thumbnail_loaded;
} SearchResult;

SearchResult* search_result_init()
{
    SearchResult* search_result = calloc(1, sizeof(SearchResult));
    if (!search_result) {
        fprintf(stderr, "search_result_init: malloc returned null\n");
        return NULL;
    }

    search_result->search_result_type = SEARCH_RESULT_TYPE_UNDF;
    
    return search_result;
}

void search_result_free(SearchResult* search_result)
{
    if (!search_result) 
        return;
 
    free(search_result); search_result = NULL;
}

typedef enum 
{
    SORT_TYPE_RATING,
    SORT_TYPE_RELEVANCE,
    SORT_TYPE_VIEW_COUNT,
    SORT_TYPE_UPLOAD_DATE,
} SortType; 

const char* sort_type_to_search_param(const SortType sort_type)
{
    switch (sort_type) {
        case SORT_TYPE_RATING: 
            return "CAE";
        case SORT_TYPE_RELEVANCE: 
            return "CAA";
        case SORT_TYPE_VIEW_COUNT: 
            return "CAM";
        case SORT_TYPE_UPLOAD_DATE: 
            return "CAI";
        default:
            fprintf(stderr, "sort_type_to_search_param: SortType %d is invalid\n", sort_type);
            return NULL;
    }
}

const char* sort_type_to_text(const SortType sort_type)
{
    switch (sort_type) {
        case SORT_TYPE_RATING: 
            return "RATING";
        case SORT_TYPE_VIEW_COUNT: 
            return "VIEWS"; 
        case SORT_TYPE_RELEVANCE: 
            return "RELEVENCE";
        case SORT_TYPE_UPLOAD_DATE: 
            return "UPLOAD DATE";
        default:
            fprintf(stderr, "sort_type_to_text: SortType %d is invalid\n", sort_type);
            return NULL;
    }
}

typedef enum
{
    QUERY_ATTR_REPLACE,
    QUERY_ATTR_APPEND,
} QueryAttribute;

const char* query_attr_to_text(const QueryAttribute query_attr)
{
    switch (query_attr) {
        case QUERY_ATTR_REPLACE: 
            return "NEW";
        case QUERY_ATTR_APPEND: 
            return "APPENDING";
        default:
            fprintf(stderr, "query_attr_to_text: QueryAttribute %d is invalid\n", query_attr);
            return NULL;
    }
}

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

const char* query_type_to_endpoint(const QueryType query_type)
{
    switch (query_type) {
        case QUERY_TYPE_USER_INPUT: 
            return "search";
        case QUERY_TYPE_VIEW_VIDEO: 
            return "player";
        case QUERY_TYPE_VIEW_RELATED: 
            return "next";
        case QUERY_TYPE_VIEW_CHANNEL:
        case QUERY_TYPE_VIEW_PLAYLIST:
        case QUERY_TYPE_VIEW_TRENDING: 
            return "browse";
        default:    
            fprintf(stderr, "query_type_to_endpoint: QueryType %d is invalid\n", query_type);
            return NULL;
    }
}

const char* query_type_to_text(const QueryType query_type)
{
    switch (query_type) {
        case QUERY_TYPE_USER_INPUT: 
            return "QUERIED";
        case QUERY_TYPE_VIEW_RELATED: 
            return "RELATED";
        case QUERY_TYPE_VIEW_TRENDING: 
            return "TRENDING";
        case QUERY_TYPE_VIEW_VIDEO: 
            return "VIDEO FOCUS";
        case QUERY_TYPE_VIEW_CHANNEL: 
            return "VIEW CHANNEL";
        case QUERY_TYPE_VIEW_PLAYLIST: 
            return "VIEW PLAYLIST";
        default:
            fprintf(stderr, "query_type_to_text: QueryType %d is invalid\n", query_type);
            return NULL;
    }
}

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

// youtube parsing

#define MEDIUM_THUMBNAIL_VIDEO_RESOLUTION "mqdefault"

bool assign_video_thumbnail_path(const char* video_id, char* dest, const size_t dest_size)
{
    if (!valid_string(video_id) || !dest) 
        return false;

    const size_t written = snprintf(dest, dest_size, "/vi/%s/" MEDIUM_THUMBNAIL_VIDEO_RESOLUTION ".jpg", video_id);
    
    return (0 < written) && (written < dest_size);
}

char* get_video_description(cJSON* videoDetails)
{
    const char* desc_path = ".videoDetails.shortDescription";

    const cJSON* description_item = cjson_pointer_get(videoDetails, desc_path);

    return json_string_is_valid(description_item) ? 
           strdup(description_item->valuestring) : 
           NULL;
}

bool video_is_youtube_short(cJSON *videoRenderer) 
{
    const char* path = ".navigationEndpoint.commandMetadata.webCommandMetadata.url";
    const cJSON* url = cjson_pointer_get(videoRenderer, path); 
    if (json_string_is_valid(url)) 
        return strstr(url->valuestring, "/shorts");

    return false;
}

bool video_is_live(cJSON* videoRenderer)
{
    const char* path = ".badges[0].metadataBadgeRenderer.label";
    const cJSON* label = cjson_pointer_get(videoRenderer, path);
    if (json_string_is_valid(label)) 
        return (strcmp("LIVE", label->valuestring) == 0);

    return false;
}

void parse_playlist_video(cJSON* playlistVideoRenderer, SearchResult* playlist_vid)
{
    if ((playlistVideoRenderer == NULL) || (playlist_vid == NULL)) return;

    const char* id_path = ".videoId";

    if (!assign_string_from_path(playlistVideoRenderer, id_path, playlist_vid->id, sizeof(playlist_vid->id))) {
        printf("parse_playlist_video: id assign fail (json path: \"%s\")\n", id_path);
        playlist_vid->search_result_type = SEARCH_RESULT_TYPE_UNDF;
        return;
    }

    playlist_vid->search_result_type = SEARCH_RESULT_TYPE_VIDEO;

    if (!assign_video_thumbnail_path(playlist_vid->id, playlist_vid->thumbnail_path, sizeof(playlist_vid->thumbnail_path))) {
        printf("parse_playlist_video: thumbnail path fail\n");
    }

    const char* title_path = ".title.runs[0].text";

    if (!assign_string_from_path(playlistVideoRenderer, title_path, playlist_vid->title, sizeof(playlist_vid->title))) {
        printf("parse_playlist_video: title assign fail (json path: \"%s\")\n", title_path);
    }

    const char* author_id_path = ".shortBylineText.runs[0].navigationEndpoint.browseEndpoint.browseId";

    if (assign_string_from_path(playlistVideoRenderer, author_id_path, playlist_vid->authorId, sizeof(playlist_vid->authorId)) == false) {
        printf("parse_playlist_video: author id assign fail (json path: %s)\n", author_id_path);
    }

    const char* length_path = ".lengthText.simpleText";

    if (!assign_string_from_path(playlistVideoRenderer, length_path, playlist_vid->duration, sizeof(playlist_vid->duration))) {
        printf("parse_playlist_video: duration assign fail (json path: \"%s\")\n", length_path);
    }

    const char* views_path = ".videoInfo.runs[0].text";

    if (assign_string_from_path(playlistVideoRenderer, views_path, playlist_vid->view_count, sizeof(playlist_vid->view_count))) {
        char* end = strstr(playlist_vid->view_count, " views");
        if (end == NULL) strncat(playlist_vid->view_count, " views", sizeof(playlist_vid->view_count) - strlen(playlist_vid->view_count) - 1);
    }

    else printf("parse_playlist_video: views assign fail (json path: \"%s\")\n", views_path);

    const char* publish_date_path = ".videoInfo.runs[2].text";

    if (!assign_string_from_path(playlistVideoRenderer, publish_date_path, playlist_vid->date_published, sizeof(playlist_vid->date_published))) {
        printf("parse_playlist_video: date published assign fail (json path: \"%s\")\n", publish_date_path);
    }
}

void parse_video(cJSON* videoRenderer, const char* author_id_override, const bool allow_youtube_shorts, SearchResult* video)
{
    if ((videoRenderer == NULL) || (author_id_override == NULL) || (video == NULL)) return;

    if (video_is_youtube_short(videoRenderer)) {
        if (allow_youtube_shorts == false){
            video->search_result_type = SEARCH_RESULT_TYPE_UNDF;
            return;
        }

        else video->search_result_type = SEARCH_RESULT_TYPE_SHORT;
    }

    const char* id_path = ".videoId";

    if (assign_string_from_path(videoRenderer, id_path, video->id, sizeof(video->id)) == false) {
        printf("parse_video: id assign fail (json path: \"%s\")\n", id_path);
        video->search_result_type = SEARCH_RESULT_TYPE_UNDF;
        return;
    }

    video->search_result_type = SEARCH_RESULT_TYPE_VIDEO;

    if (assign_video_thumbnail_path(video->id, video->thumbnail_path, sizeof(video->thumbnail_path)) == false) {
        printf("parse_video: thumbnail path fail\n");
    }
    
    const char* title_path = ".title.runs[0].text";
    
    if (assign_string_from_path(videoRenderer, title_path, video->title, sizeof(video->title)) == false) {
        printf("parse_video: title assign fail (json path: \"%s\")\n", title_path);
    }

    if (author_id_override[0] != '\0') {
        strncpy(video->authorId, author_id_override, sizeof(video->authorId) - 1);
        video->authorId[sizeof(video->authorId) - 1] = '\0';
    }

    else {
        const char* author_id_path = author_id_override[0] != '\0' ? author_id_override : ".longBylineText.runs[0].navigationEndpoint.browseEndpoint.browseId";
        
        if (assign_string_from_path(videoRenderer, author_id_path, video->authorId, sizeof(video->authorId)) == false) {
            printf("parse_video: author id assign fail (json path: %s)\n", author_id_path);
        }
    }

    if (video_is_live(videoRenderer)) {
        video->search_result_type = SEARCH_RESULT_TYPE_LIVE;

        const char* live_viewers_path = ".viewCountText.runs[0].text";

        if (assign_string_from_path(videoRenderer, live_viewers_path, video->view_count, sizeof(video->view_count)) == false) {
            video->view_count[0] = '0';
        }

        return;
    }

    const char* view_count_path = ".viewCountText.simpleText";

    if (assign_string_from_path(videoRenderer, view_count_path, video->view_count, sizeof(video->view_count))) {
        format_view_count(video->view_count, sizeof(video->view_count));
    }
    
    else snprintf(video->view_count, sizeof(video->view_count), "no views");

    const char* video_age_path = ".publishedTimeText.simpleText";

    if (assign_string_from_path(videoRenderer, video_age_path, video->date_published, sizeof(video->date_published)) == false) {
        printf("parse_video: date published assign fail (json path: \"%s\")\n", video_age_path);
    }

    const char* length_path = ".lengthText.simpleText";

    if (assign_string_from_path(videoRenderer, length_path, video->duration, sizeof(video->duration)) == false) {
        printf("parse_video: length assign fail (json path: \"%s\")\n", length_path);
    }
}

void parse_related_video(cJSON* lockupViewModel, SearchResult* related_vid)
{
    if ((lockupViewModel == NULL) || (related_vid == NULL)) return;

    const char* id_path = ".contentId";

    if (!assign_string_from_path(lockupViewModel, id_path, related_vid->id, sizeof(related_vid->id))) {
        printf("parse_related_video: id assign fail (json path: \"%s\")\n", id_path);
        related_vid->search_result_type = SEARCH_RESULT_TYPE_UNDF;
        return;
    }

    related_vid->search_result_type = SEARCH_RESULT_TYPE_VIDEO;

    if (!assign_video_thumbnail_path(related_vid->id, related_vid->thumbnail_path, sizeof(related_vid->thumbnail_path))) {
        printf("parse_related_video: thumbnail path fail\n");
    } 

    const char* title_path = ".metadata.lockupMetadataViewModel.title.content";

    if (!assign_string_from_path(lockupViewModel, title_path, related_vid->title, sizeof(related_vid->title))) {
        printf("parse_related_video: title assign fail (json path: \"%s\")\n", title_path);
    }

    const char* author_id_path = ".metadata.lockupMetadataViewModel.image.decoratedAvatarViewModel.rendererContext.commandContext.onTap.innertubeCommand.browseEndpoint.browseId";

    if (assign_string_from_path(lockupViewModel, author_id_path, related_vid->authorId, sizeof(related_vid->authorId)) == false) {
        printf("parse_related_video: author id assign fail\n");
    }

    const char* duration_path = ".contentImage.thumbnailViewModel.overlays[0].thumbnailOverlayBadgeViewModel.thumbnailBadges[0].thumbnailBadgeViewModel.text";

    if (!assign_string_from_path(lockupViewModel, duration_path, related_vid->duration, sizeof(related_vid->duration))) {
        printf("parse_related_video: duration assign fail (json path: \"%s\")\n", duration_path);
    }

    const char* view_count_path = ".metadata.lockupMetadataViewModel.metadata.contentMetadataViewModel.metadataRows[1].metadataParts[0].text.content";

    if (!assign_string_from_path(lockupViewModel, view_count_path, related_vid->view_count, sizeof(related_vid->view_count))) {
        printf("parse_related_video: view count assign fail (json path: \"%s\")\n", view_count_path);
    }

    const char* date_published_path = ".metadata.lockupMetadataViewModel.metadata.contentMetadataViewModel.metadataRows[1].metadataParts[1].text.content";

    if (!assign_string_from_path(lockupViewModel, date_published_path, related_vid->date_published, sizeof(related_vid->date_published))) {
        printf("parse_related_video: duration assign fail (json path: \"%s\")\n", date_published_path);
    }
}

void parse_channel_result(cJSON* channelRenderer, SearchResult* channel)
{
    if ((channelRenderer == NULL) || (channel == NULL)) return;

    const char* id_path = ".channelId";

    if (!assign_string_from_path(channelRenderer, id_path, channel->id, sizeof(channel->id))) {
        printf("parse_channel: id assign fail (json path: \"%s\")\n", id_path);
        channel->search_result_type = SEARCH_RESULT_TYPE_UNDF;
        return;
    }

    channel->search_result_type = SEARCH_RESULT_TYPE_CHANNEL;

    const char* title_path = ".title.simpleText";

    if (!assign_string_from_path(channelRenderer, title_path, channel->title, sizeof(channel->title))) {
        printf("parse_channel: title assign fail (json path: \"%s\")\n", title_path);
    }

    const char* sub_count_path = ".videoCountText.simpleText";

    if (!assign_string_from_path(channelRenderer, sub_count_path, channel->subscriber_count, sizeof(channel->subscriber_count))) {
        printf("parse_channel: subscriber count assign fail (json path: \"%s\")\n", sub_count_path);
    }

    const cJSON* channelThumbnailLink = cjson_pointer_get(channelRenderer, ".thumbnail.thumbnails[0].url");
    if (json_string_is_valid(channelThumbnailLink)) {
        // the path either starts with '/ytc', or just '/'
        const char* path1 = strstr(channelThumbnailLink->valuestring, "/ytc");
        const char* path2 = strrchr(channelThumbnailLink->valuestring, '/');
        snprintf(channel->thumbnail_path, sizeof(channel->thumbnail_path), "%s", (path1 ? path1 : path2));
    }
}

bool parse_highlighted_channel(cJSON* json, SearchResult* channel)
{
    if ((json == NULL) || (channel == NULL)) return false;

    const char* id_path = ".contents.twoColumnBrowseResultsRenderer.tabs[1].tabRenderer.endpoint.browseEndpoint.browseId";
    
    if (assign_string_from_path(json, id_path, channel->id, sizeof(channel->id)) == false) {
        fprintf(stderr, "parse_highlighted_channel: failed to assign id\n");
        return false;
    }

    channel->search_result_type = SEARCH_RESULT_TYPE_CHANNEL;

    const char* title_path = ".header.pageHeaderRenderer.pageTitle";
    
    if (assign_string_from_path(json, title_path, channel->title, sizeof(channel->title)) == false) {
        fprintf(stderr, "parse_highlighted_channel: failed to assign channel title\n");
        return false;
    }

    const char* subscriber_count_path = ".header.pageHeaderRenderer.content.pageHeaderViewModel.metadata.contentMetadataViewModel.metadataRows[1].metadataParts[0].text.content";
    
    if (assign_string_from_path(json, subscriber_count_path, channel->subscriber_count, sizeof(channel->subscriber_count)) == false) {
        fprintf(stderr, "parse_highlighted_channel: failed to assign sub count\n");
        return false;
    }
    
    const char* thumbnail_path = ".header.pageHeaderRenderer.content.pageHeaderViewModel.image.decoratedAvatarViewModel.avatar.avatarViewModel.image.sources[0].url";
    
    const cJSON* thumbnail_url_item = cjson_pointer_get(json, thumbnail_path);
    if (json_string_is_valid(thumbnail_url_item) == false) {
        fprintf(stderr, "parse_highlighted_channel: failed to assign thumbnail path\n");
        return false;
    }

    const char* path1 = strstr(thumbnail_url_item->valuestring, "/ytc");
    const char* path2 = strrchr(thumbnail_url_item->valuestring, '/');

    if ((path1 == NULL) && (path2 == NULL)) {
        fprintf(stderr, "parse_highlighted_channel: channel thumbnail path does not start with '/' or '/ytc'\n");
        return false;
    }

    snprintf(channel->thumbnail_path, sizeof(channel->thumbnail_path), "%s", path1 ? path1 : path2);

    return true;
}

void parse_playlist_result(cJSON *lockupViewModel, SearchResult *playlist)
{
    if ((lockupViewModel == NULL) || (playlist == NULL)) return;

    const char* id_path = ".contentId";

    if (!assign_string_from_path(lockupViewModel, id_path, playlist->id, sizeof(playlist->id))) {
        printf("parse_playlist: id assign fail (json path: \"%s\")\n", id_path);
        playlist->search_result_type = SEARCH_RESULT_TYPE_UNDF;
        return;
    }
    
    playlist->search_result_type = SEARCH_RESULT_TYPE_PLAYLIST;

    const char* title_path = ".metadata.lockupMetadataViewModel.title.content";

    if (!assign_string_from_path(lockupViewModel, title_path, playlist->title, sizeof(playlist->title))) {
        printf("parse_playlist: title assign fail (json path: \"%s\")\n", title_path);
    }

    const char* first_video_id_path = ".rendererContext.commandContext.onTap.innertubeCommand.watchEndpoint.videoId";

    char video_id[16];
    if (assign_string_from_path(lockupViewModel, first_video_id_path, video_id, sizeof(video_id))) {
        if (!assign_video_thumbnail_path(video_id, playlist->thumbnail_path, sizeof(playlist->thumbnail_path))) {
            printf("parse_playlist: thumbnail path assign fail\n");
        }
    }
    
    else printf("parse_playlist: video id assign fail (json path: \"%s\")\n", first_video_id_path);

    const char* video_count_path = ".contentImage.collectionThumbnailViewModel.primaryThumbnail.thumbnailViewModel.overlays[0].thumbnailOverlayBadgeViewModel.thumbnailBadges[0].thumbnailBadgeViewModel.text";

    if (!assign_string_from_path(lockupViewModel, video_count_path, playlist->video_count, sizeof(playlist->video_count))) {
        printf("parse_playlist: video count assign fail (json path: \"%s\")\n", video_count_path);
    }
}

const char* get_results_list_path(const QueryType query_type, const QueryAttribute query_attr)
{
    switch (query_type) {
        case QUERY_TYPE_USER_INPUT: 
            if (query_attr == QUERY_ATTR_REPLACE) 
                return ".contents.twoColumnSearchResultsRenderer.primaryContents.sectionListRenderer.contents[0].itemSectionRenderer.contents";
            if (query_attr == QUERY_ATTR_APPEND) 
                return ".onResponseReceivedCommands[0].appendContinuationItemsAction.continuationItems[0].itemSectionRenderer.contents";
        case QUERY_TYPE_VIEW_RELATED: 
            if (query_attr == QUERY_ATTR_REPLACE) 
                return "contents.twoColumnWatchNextResults.secondaryResults.secondaryResults.results";
            if (query_attr == QUERY_ATTR_APPEND) 
                return ".onResponseReceivedEndpoints[0].appendContinuationItemsAction.continuationItems";
        case QUERY_TYPE_VIEW_PLAYLIST: 
            if (query_attr == QUERY_ATTR_REPLACE) 
                return ".contents.twoColumnBrowseResultsRenderer.tabs[0].tabRenderer.content.sectionListRenderer.contents[0].itemSectionRenderer.contents[0].playlistVideoListRenderer.contents";
            if (query_attr == QUERY_ATTR_APPEND) 
                return ".onResponseReceivedActions[0].appendContinuationItemsAction.continuationItems";
        case QUERY_TYPE_VIEW_CHANNEL: 
            if (query_attr == QUERY_ATTR_REPLACE) 
                return ".contents.twoColumnBrowseResultsRenderer.tabs[1].tabRenderer.content.richGridRenderer.contents";
            if (query_attr == QUERY_ATTR_APPEND) 
                return ".onResponseReceivedActions[0].appendContinuationItemsAction.continuationItems";
        case QUERY_TYPE_VIEW_TRENDING:             
            return ".contents.twoColumnBrowseResultsRenderer.tabs[0].tabRenderer.content.sectionListRenderer.contents[2].itemSectionRenderer.contents[0].shelfRenderer.content.expandedShelfContentsRenderer.items";
       
        default:
            fprintf(stderr, "get_results_list_path: QueryType %d is not supported\n", query_type);
            return NULL;
    }
}

int create_results_from_json(cJSON* json, List* results, const QueryType query_type, const QueryAttribute query_attr, const bool allow_shorts)
{
    if (!json || !results)
        return -1;

    const char* path = get_results_list_path(query_type, query_attr); 

    cJSON* results_array = cjson_pointer_get(json, path);
    if (cJSON_IsArray(results_array) == false) {
        printf("create_results_from_json: invalid results array from path %s\n", path);
        write_json_to_file(json, "results.json");
        return -1;
    }

    char author_id[64] = {0};
    if (query_type == QUERY_TYPE_VIEW_CHANNEL) {
        const char* author_id_path = (query_attr == QUERY_ATTR_REPLACE) 
                                     ? ".contents.twoColumnBrowseResultsRenderer.tabs[0].tabRenderer.endpoint.browseEndpoint.browseId"
                                     : ".responseContext.serviceTrackingParams[0].params[3].value";

        if (assign_string_from_path(json, author_id_path, author_id, sizeof(author_id)) == false) {
            printf("create_results_from_json: failed to parse author id from the path %s\n", author_id_path);
        }
    }

    int elements_added = 0;
    const int old_size = results->count;

    cJSON *item;
    cJSON_ArrayForEach (item, results_array) {
        Node* node = node_init((void*) search_result_init, NULL, (void*) search_result_free, NULL);
        if (node == NULL) {
            fprintf(stderr, "create_results_from_json: failed to create node\n");
            return 0;
        }

        SearchResult* search_result = (SearchResult*) node->content;
        
        cJSON* videoRenderer = cjson_pointer_get(item, ".videoRenderer");     
        cJSON* richItemRenderer = cjson_pointer_get(item, ".richItemRenderer.content.videoRenderer");
        cJSON* playlistVideoRenderer = cjson_pointer_get(item, ".playlistVideoRenderer");
        cJSON* channelRenderer = cjson_pointer_get(item, ".channelRenderer");  
        cJSON* lockupViewModel = cjson_pointer_get(item, ".lockupViewModel");   
        
        if (videoRenderer)             
            parse_video(videoRenderer, author_id, allow_shorts, search_result);
        
        else if (richItemRenderer)
            parse_video(richItemRenderer, author_id, allow_shorts,search_result);
        
        else if (playlistVideoRenderer)     
            parse_playlist_video(playlistVideoRenderer, search_result);

        else if (channelRenderer)
            parse_channel_result(channelRenderer, search_result);
        
        else if (lockupViewModel) {
            if (query_type == QUERY_TYPE_VIEW_RELATED) 
                parse_related_video(lockupViewModel, search_result);
            else 
                parse_playlist_result(lockupViewModel, search_result);
        }

        if (search_result->search_result_type != SEARCH_RESULT_TYPE_UNDF) {
            elements_added++; 
            list_append(results, node);
        }

        else 
            node_free(node);
    }

    if (query_attr == QUERY_ATTR_REPLACE) {
        for (int i = 0; results->head && (i < old_size); i++) {
            node_free(list_dequeue(results));
        }
    }

    return elements_added;
}

const char* get_continuation_token_path(const QueryType search_type, const QueryAttribute search_attr)
{
    switch (search_type) {
        case QUERY_TYPE_USER_INPUT:
            if (search_attr == QUERY_ATTR_REPLACE) 
                return ".contents.twoColumnSearchResultsRenderer.primaryContents.sectionListRenderer.contents[1].continuationItemRenderer.continuationEndpoint.continuationCommand.token";
            if (search_attr == QUERY_ATTR_APPEND) 
                return ".onResponseReceivedCommands[0].appendContinuationItemsAction.continuationItems[1].continuationItemRenderer.continuationEndpoint.continuationCommand.token";
        case QUERY_TYPE_VIEW_RELATED:
            if (search_attr == QUERY_ATTR_REPLACE) 
                return ".contents.twoColumnWatchNextResults.secondaryResults.secondaryResults.results[-1].continuationItemRenderer.continuationEndpoint.continuationCommand.token";
            if (search_attr == QUERY_ATTR_APPEND) 
                return ".onResponseReceivedEndpoints[0].appendContinuationItemsAction.continuationItems[-1].continuationItemRenderer.continuationEndpoint.continuationCommand.token";
        case QUERY_TYPE_VIEW_PLAYLIST:
            if (search_attr == QUERY_ATTR_REPLACE) 
                return ".contents.twoColumnBrowseResultsRenderer.tabs[0].tabRenderer.content.sectionListRenderer.contents[0].itemSectionRenderer.contents[0].playlistVideoListRenderer.contents[-1].continuationItemRenderer.continuationEndpoint.commandExecutorCommand.commands[1].continuationCommand.token";
            if (search_attr == QUERY_ATTR_APPEND) 
                return ".onResponseReceivedActions[0].appendContinuationItemsAction.continuationItems[-1].continuationItemRenderer.continuationEndpoint.continuationCommand.token";
        case QUERY_TYPE_VIEW_CHANNEL:
            if (search_attr == QUERY_ATTR_REPLACE) 
                return ".contents.twoColumnBrowseResultsRenderer.tabs[1].tabRenderer.content.richGridRenderer.contents[-1].continuationItemRenderer.continuationEndpoint.continuationCommand.token";
            if (search_attr == QUERY_ATTR_APPEND) 
                return ".onResponseReceivedActions[0].appendContinuationItemsAction.continuationItems[-1].continuationItemRenderer.continuationEndpoint.continuationCommand.token";
        default: 
            return NULL;
    }
}

void get_continuation_token(cJSON* json, char** dest, const QueryType query_type, const QueryAttribute query_attr)
{
    if (!json || !dest)
        return;

    const char* continuation_path = get_continuation_token_path(query_type, query_attr);
    
    if (*dest) {
        free(*dest); (*dest) = NULL;
    }

    const cJSON* token_obj = cjson_pointer_get(json, continuation_path);
    if (json_string_is_valid(token_obj) == false) {
        fprintf(stderr, "get_continuation_token: invalid json string parsed (path: %s)\n", continuation_path);
        return;
    }

    if (((*dest) = strdup(token_obj->valuestring)) == NULL) 
        fprintf(stderr, "get_continuation_token: strdup returned null\n");
}

// youtube request configuration

#define HTTP_PROTOCOL_VER "1.1"
#define CONNECTION_STATUS "keep-alive"
#define USER_AGENT "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/130.0.0.0 Safari/537.36"

#define CLIENT_NAME "WEB"
#define CLIENT_VER "2.20250730"
#define YT_API_PLAYLIST_BROWSE_ID_PREFIX "VL"    
#define YT_API_TRENDING_BROWSE_ID "FEtrending" 
#define YT_API_CHANNEL_VIDEOS_PARAMS "EgZ2aWRlb3PyBgQKAjoA"  

cJSON* configure_client_object()
{
    cJSON* client = cJSON_CreateObject();
    if (!client) {
        fprintf(stderr, "configure_client_object: failed to create object\n");
        return NULL;
    }

    if ((cJSON_AddStringToObject(client, "clientName", CLIENT_NAME) == NULL) || 
        (cJSON_AddStringToObject(client, "clientVersion", CLIENT_VER) == NULL)) {
        fprintf(stderr, "configure_client_object: failed to add client elements\n");
        cJSON_Delete(client); client = NULL;
    }

    return client;
}

cJSON* configure_base_payload()
{
    cJSON* client = configure_client_object();
    if (!client) {
        fprintf(stderr, "configure_base_payload: client is null\n");
        return NULL;
    }

    cJSON* context = cJSON_CreateObject();
    if (!context) {
        fprintf(stderr, "configure_base_payload: context is null\n");
        cJSON_Delete(client); client = NULL;
        return NULL;
    }
    
    if (!cJSON_AddItemToObject(context, "client", client)) {
        fprintf(stderr, "configure_base_payload: failed to add client to context object\n");
        cJSON_Delete(client); client = NULL;
        cJSON_Delete(context); context = NULL;
        return NULL;
    }  
    
    cJSON* root = cJSON_CreateObject();
    if (!root) {
        fprintf(stderr, "configure_base_payload: root is null\n");
        cJSON_Delete(context); context = NULL;
        return NULL;
    }

    if (!cJSON_AddItemToObject(root, "context", context)) {
        fprintf(stderr, "configure_base_payload: failed to add context object to root\n");
        cJSON_Delete(context); context = NULL;
        cJSON_Delete(root); root = NULL;
    }  

    return root; 
}

bool add_view_trending_videos_payload(cJSON* root)
{
    if (root == NULL) return false;

    return cJSON_AddStringToObject(root, "browseId", YT_API_TRENDING_BROWSE_ID);
}

bool add_view_related_videos_payload(cJSON* root, const char* video_id)
{
    if ((root == NULL) || (valid_string(video_id) == false)) return false;

    return cJSON_AddStringToObject(root, "videoId", video_id);
}

bool add_view_channel_videos_payload(cJSON* root, const char* channel_id)
{
    if ((root == NULL) || (valid_string(channel_id) == false)) return false;

    return cJSON_AddStringToObject(root, "browseId", channel_id) &&
           cJSON_AddStringToObject(root, "params", YT_API_CHANNEL_VIDEOS_PARAMS);
}

bool add_continuation_payload(cJSON* root, const char* continuation_token)
{
    if ((root == NULL) || (valid_string(continuation_token) == false)) return false;

    return cJSON_AddStringToObject(root, "continuation", continuation_token);
}

bool add_view_playlist_videos_payload(cJSON* root, const char* playlist_id)
{
    if ((root == NULL) || (valid_string(playlist_id) == false)) return false;

    char browse_id[64] = {0};

    const int written = snprintf(browse_id, sizeof(browse_id),"%s%s", YT_API_PLAYLIST_BROWSE_ID_PREFIX, playlist_id);
    if ((written < 0) || (written >= sizeof(browse_id))) {
        fprintf(stderr, "add_view_playlist_videos_payload: snprintf returned %d\n", written);
        return false;
    }

    return cJSON_AddStringToObject(root, "browseId", browse_id);
}

bool add_view_user_input_payload(cJSON* root, const char* user_input, const SortType sort_type, const SearchResultType search_result_type)
{
    if ((root == NULL) || (valid_string(user_input) == false)) return false;

    const char* sort_param = sort_type_to_search_param(sort_type);
    const char* media_param = search_result_type_to_search_param(search_result_type);

    if ((sort_param == NULL) || (media_param == NULL)) {
        fprintf(stderr, "add_view_user_input_payload: SortType (%d) or SearchResultType (%d) returned invalid search param\n", sort_type, search_result_type);
        return false;
    }

    char params[32] = {0};
    
    const int written = snprintf(params, sizeof(params), "%s%s", sort_param, media_param);
    if ((written < 0) || (written >= sizeof(params))) {
        fprintf(stderr, "add_view_user_input_payload: snprintf returned %d\n", written);
        return false;
    }

    return cJSON_AddStringToObject(root, "query", user_input) && 
           cJSON_AddStringToObject(root, "params", params);
}

cJSON* configure_post_payload(const Query* query, const char* continuation_token)
{
    if (!query) 
        return NULL;

    cJSON* root = configure_base_payload();
    if (root == NULL) {
        printf("configure_post_payload: failed to create base payload\n");
        return NULL;
    }

    if ((query->attr == QUERY_ATTR_APPEND) && (add_continuation_payload(root, continuation_token) == false))  {
        fprintf(stderr, "configure_post_payload: failed to add continuation payload\n");
        cJSON_Delete(root); root = NULL;
    }

    else if (query->attr == QUERY_ATTR_REPLACE) {
        bool success = false;

        switch (query->type) {
            case QUERY_TYPE_VIEW_VIDEO: 
            case QUERY_TYPE_VIEW_RELATED:  success = add_view_related_videos_payload(root, query->focused_id); break;    
            case QUERY_TYPE_USER_INPUT:    success = add_view_user_input_payload(root, query->string, query->sort, query->media); break;
            case QUERY_TYPE_VIEW_CHANNEL:  success = add_view_channel_videos_payload(root, query->focused_id);  break;
            case QUERY_TYPE_VIEW_PLAYLIST: success = add_view_playlist_videos_payload(root, query->focused_id); break;
            case QUERY_TYPE_VIEW_TRENDING: success = add_view_trending_videos_payload(root); break;
            default:    
                fprintf(stderr, "configure_post_payload: QueryType %d does not have a payload\n", query->type);
        }

        if (success == false) {
            fprintf(stderr, "configure_post_payload: failed to add payload\n");
            cJSON_Delete(root); root = NULL;
        }
    }

    return root;
}

bool configure_youtube_internal_api_path(char* dest, const size_t dest_size, QueryType query_type, const char* key)
{
    const char* endpoint = query_type_to_endpoint(query_type);

    if ((endpoint == NULL) || (dest == NULL) || (valid_string(key) == false)) return false;

    const size_t written = snprintf(dest, dest_size, "/youtubei/v1/%s?key=%s", endpoint, key);

    return (written > 0) && (written < dest_size);
}

bool configure_get_header(char* dest, const size_t dest_size, const char* host, const char* path)
{
    if ((dest == NULL) || (!valid_string(host)) || (!valid_string(path)))
        return false;

    const int written =  snprintf(dest, dest_size,
                "GET %s HTTP/%s\r\n"
                        "Host: %s\r\n"
                        "User-Agent: %s\r\n"
                        "Connection: %s\r\n"
                        "\r\n",
                        path, HTTP_PROTOCOL_VER, host, USER_AGENT, CONNECTION_STATUS);
    
    return (written > 0) && (written < dest_size);
}

bool configure_post_header(char* dest, const size_t dest_size, const char* host, const char* path, const size_t content_length)
{
    if ((dest == NULL) || (!valid_string(host)) || (!valid_string(path)))
        return false;

    const int written = snprintf(dest, dest_size,
                        "POST %s HTTP/%s\r\n"
                        "Host: %s\r\n"
                        "User-Agent: %s\r\n"
                        "Content-Type: */*\r\n"
                        "Accept: */*\r\n"
                        "Content-Length: %zu\r\n"
                        "Connection: %s\r\n"
                        "\r\n",
                        path, HTTP_PROTOCOL_VER, host, USER_AGENT, content_length, CONNECTION_STATUS);
    
    return (written > 0) && (written < dest_size);
}

bool post_request_is_ready(const HttpsRequest post)
{
    return valid_string(post.header) && valid_string(post.payload);
}

HttpsRequest configure_post_request(const Query query, const char* host, const char* api_key, const char* continuation_token)
{
    if (valid_string(host) == false) return (HttpsRequest){0};

    HttpsRequest post = (HttpsRequest){0};

    if (configure_youtube_internal_api_path(post.path, sizeof(post.path), query.type, api_key) == false) {
        fprintf(stderr, "configure_post_request: failed to resolve path\n");
        return post;
    }

    cJSON* payload = configure_post_payload(&query, continuation_token);

    if (payload == NULL) {
        fprintf(stderr, "configure_post_request: payload is null\n");
        return post;
    }

    post.payload = cJSON_Print(payload);

    if (!configure_post_header(post.header, sizeof(post.header), host, post.path, strlen(post.payload))) {
        fprintf(stderr, "configure_post_request: failed to resolve header\n");
        free(post.payload); post.payload = NULL;
    }
    
    cJSON_Delete(payload); payload = NULL;
    
    return post;
}

// thumbnail loading

typedef struct RawThumbnail
{
    char id[64];     
    Buffer data;              
    struct RawThumbnail *next;
    SearchResultType search_result_type;
} RawThumbnail;

RawThumbnail* raw_thumbnail_init()
{
    RawThumbnail* raw_thumbnail = malloc(sizeof(RawThumbnail));
    if (!raw_thumbnail) {
        fprintf(stderr, "raw_thumbnail_init: malloc returned null\n");
        return NULL;
    }

    raw_thumbnail->next = NULL;
    raw_thumbnail->data = buffer_init();
    raw_thumbnail->search_result_type = SEARCH_RESULT_TYPE_UNDF;
    memset(raw_thumbnail->id, 0, sizeof(raw_thumbnail->id));

    return raw_thumbnail;
}

void raw_thumbnail_free(RawThumbnail* raw_thumbnail)
{
    if (!raw_thumbnail)
        return;

    if (buffer_is_ready(&raw_thumbnail->data)) 
        buffer_free(&raw_thumbnail->data);

    free(raw_thumbnail); raw_thumbnail = NULL;
}

bool raw_thumbnail_is_ready(const RawThumbnail* raw)
{
    return (raw) && (valid_string(raw->id)) && (buffer_is_ready(&raw->data)) && (raw->search_result_type != SEARCH_RESULT_TYPE_UNDF);
}

const Texture load_texture_from_memory(const Buffer* image_buffer, const char* image_extension, const float width, const float height)
{
    Texture texture = {0};

    if (!buffer_is_ready(image_buffer) || !valid_string(image_extension)) 
        return texture;

    Image image = LoadImageFromMemory(image_extension, (const unsigned char*) image_buffer->data, image_buffer->size);
    if (IsImageReady(image)) {
        ImageResize(&image, width, height);
        texture = LoadTextureFromImage(image);
        UnloadImage(image);
    }

    return texture;
}

void raw_thumbnail_process(RawThumbnail* raw_thumbnail, TextureCache* texture_cache)
{
    if (!raw_thumbnail_is_ready(raw_thumbnail))
        return;   

    const float thumbnail_w = search_result_type_to_thumbnail_width(raw_thumbnail->search_result_type);
    const float thumbnail_h = search_result_type_to_thumbnail_height(raw_thumbnail->search_result_type);

    Texture thumbnail = load_texture_from_memory(&raw_thumbnail->data, ".jpeg", thumbnail_w, thumbnail_h);

    TextureCacheEntry* entry = texture_cache_entry_init(thumbnail, raw_thumbnail->id);
    if (texture_cache_entry_is_ready(entry) ) 
        texture_cache_add_entry(texture_cache, entry);
}

typedef struct
{
    List thumbail_queue;   
    pthread_mutex_t pool_mutex; // MIGHT NOT NEED, TEST FURTHER
    ConnectionPool video_thumbnail_pool;
    ConnectionPool channel_thumbnail_pool;
} ThumbnailLoader;

bool thumbnail_loader_init(ThumbnailLoader* thumbnail_loader, const size_t nconns)
{
    if (!thumbnail_loader)
        return false;

    const char* video_thumb_host = search_result_type_to_thumbnail_host(SEARCH_RESULT_TYPE_VIDEO);
    const char* channel_thumb_host = search_result_type_to_thumbnail_host(SEARCH_RESULT_TYPE_CHANNEL);
    
    thumbnail_loader->thumbail_queue = list_init();
    
    if (!connection_pool_init(&thumbnail_loader->video_thumbnail_pool, video_thumb_host, HTTPS_PORT, nconns))
        return false;

    if (!connection_pool_init(&thumbnail_loader->channel_thumbnail_pool, channel_thumb_host, HTTPS_PORT, nconns))
        return false;

    pthread_mutex_init(&thumbnail_loader->pool_mutex, NULL);

    return true;
}

void thumbnail_loader_free(ThumbnailLoader* loader)
{
    if (!loader) 
        return;

    list_free(&loader->thumbail_queue);
    pthread_mutex_destroy(&loader->pool_mutex);
    connection_pool_free(&loader->video_thumbnail_pool);
    connection_pool_free(&loader->channel_thumbnail_pool);
}

void thumbnail_loader_process_raw_images(ThumbnailLoader* loader, TextureCache* texture_cache)
{
    if (!loader) 
        return;

    List* queue = &loader->thumbail_queue;

    pthread_mutex_lock(&queue->mutex);
    
    while(queue->head) {
        Node* node = list_dequeue(queue);
        RawThumbnail* raw = (RawThumbnail*) node->content;
        raw_thumbnail_process(raw, texture_cache);
        node_free(node);
    }

    pthread_mutex_unlock(&queue->mutex);
}

Connection* thumbnail_loader_get_connection(ThumbnailLoader* loader, const SearchResultType search_result_type)
{
    if (!loader)
        return NULL;

    pthread_mutex_lock(&loader->pool_mutex);
    
    ConnectionPool* pool = (search_result_type == SEARCH_RESULT_TYPE_CHANNEL) ? 
                           &loader->channel_thumbnail_pool : 
                           &loader->video_thumbnail_pool;

    Connection* conn = &pool->connections[pool->current_conn];

    cycle_connection(pool);

    pthread_mutex_unlock(&loader->pool_mutex);

    return conn;
}

typedef struct 
{
    char path[256];
    char id[64];
    ThumbnailLoader* loader;
    SSL_CTX* ssl_ctx;
    SearchResultType search_result_type;
} LoadThumbnailArgs;

void* load_thumbnail(ThreadArgs args)
{
    LoadThumbnailArgs* targs = (LoadThumbnailArgs*) args;
    if ((!targs) || 
        (!targs->loader) || 
        (!targs->ssl_ctx) || 
        (!valid_string(targs->path) || 
        (!valid_string(targs->id)))) {
        fprintf(stderr, "load_thumbnail: invalid args\n");
        return NULL;
    }
    
    Connection* thumb_conn = thumbnail_loader_get_connection(targs->loader, targs->search_result_type);
    if (!thumb_conn) {
        fprintf(stderr, "load_thumbnail: thumbnail connection is null\n");
        return NULL;
    }

    HttpsRequest req = {0};
    if (!configure_get_header(req.header, sizeof(req.header), thumb_conn->host, targs->path)) {
        fprintf(stderr, "load_thumbnail: failed to resolve request header\n");
        return NULL;
    }

    Buffer image_data = get_https_response(req, targs->ssl_ctx, thumb_conn, HTTP_PROTOCOL_VER);
    if (!buffer_is_ready(&image_data)) {
        fprintf(stderr, "load_thumbnail: image data is invalid\n");
        return NULL;
    }
    
    Node* node = node_init((void*) raw_thumbnail_init, NULL, (void*) raw_thumbnail_free, NULL);
    if (!node) {
        fprintf(stderr, "load_thumbnail: failed to initalize node\n");
        buffer_free(&image_data);
        return NULL;
    }

    RawThumbnail* raw_image = (RawThumbnail*) node->content;

    raw_image->next = NULL;
    raw_image->data = image_data;
    raw_image->search_result_type = targs->search_result_type;
    strncpy(raw_image->id, targs->id, sizeof(raw_image->id));

    List* thumbail_queue = &targs->loader->thumbail_queue;

    pthread_mutex_lock(&thumbail_queue->mutex);
    list_append(thumbail_queue, node);
    pthread_mutex_unlock(&thumbail_queue->mutex);

    return NULL;
}

bool queue_load_thumbnail(SSL_CTX* ssl_ctx, ThumbnailLoader* loader, List* task_queue, SearchResultType search_result_type, const char* id, const char* path)
{
    if ((!ssl_ctx) || 
        (!loader) || 
        (!task_queue) || 
        (!valid_string(id)) || 
        (!valid_string(path))) {
        fprintf(stderr, "queue_load_thumbnail: invalid args\n");
        return false;
    }

    LoadThumbnailArgs* targs = malloc(sizeof(LoadThumbnailArgs));
    if (!targs) {
        fprintf(stderr, "queue_load_thumbnail: malloc returned null\n");
        return false;
    }

    targs->loader = loader;
    targs->ssl_ctx = ssl_ctx;
    targs->search_result_type = search_result_type;
    strncpy(targs->id, id, sizeof(targs->id));
    strncpy(targs->path, path, sizeof(targs->path));

    return thread_task_launch(task_queue, targs, load_thumbnail);
}

// api manager

typedef struct
{
    pthread_mutex_t token_mutex; 
    pthread_mutex_t pool_mutex; 
    ConnectionPool youtube_api_pool;
    char* continuation_token;
    char* api_key;
} ClientContext;

bool client_context_init(ClientContext* client_context, const size_t nconns)
{
    if (!client_context)
        return false;

    client_context->continuation_token = NULL;
    client_context->api_key = "AIzaSyAO_FJ2SlqU8Q4STEHLGCilw_Y9_11qcW8";

    if (!connection_pool_init(&client_context->youtube_api_pool, "www.youtube.com", HTTPS_PORT, nconns))
        return false;

    pthread_mutex_init(&client_context->pool_mutex, NULL);
    pthread_mutex_init(&client_context->token_mutex, NULL);

    return true;
}

void client_context_free(ClientContext* client)
{
    if (!client)
        return;

    connection_pool_free(&client->youtube_api_pool);

    if (client->continuation_token) {
        free(client->continuation_token); client->continuation_token = NULL;
    }

    pthread_mutex_destroy(&client->token_mutex);
    pthread_mutex_destroy(&client->pool_mutex);
}

// user data management

#define ARRAY_NAME "array"

#define OBJ_ID_PATH           "id" 
#define OBJ_THUMBNAIL_PATH    "thumbnail_path"
#define OBJ_TITLE_PATH        "title"
#define OBJ_AUTHOR_ID_PATH    "authorId"
#define OBJ_SUB_COUNT_PATH    "subscriber_count"
#define OBJ_UPLOAD_DATE_PATH  "date_published"
#define OBJ_VIDEO_COUNT_PATH  "video_count"
#define OBJ_VIEW_COUNT_PATH   "view_count"
#define OBJ_VIDEO_LENGTH_PATH "duration"
#define OBJ_search_result_type_PATH   "search_result_type"

#define LIKED_VIDEOS_FILE  "liked_videos.json"
#define SUBSCRIPTIONS_FILE "subscriptions.json"
#define WATCH_HISTORY_FILE "watch_history.json"

typedef struct
{
    cJSON* liked_videos;
    cJSON* watched_videos;
    cJSON* subscribed_channels;
} UserData;

cJSON* create_user_data_object()
{
    cJSON* root = cJSON_CreateObject();
    if (root == NULL) {
        fprintf(stderr, "create_user_data_object: failed to create root\n");
        return NULL;
    }

    cJSON* array = cJSON_CreateArray();
    if (array == NULL) {
        fprintf(stderr, "create_user_data_object: failed to create array\n");
        cJSON_Delete(root); root = NULL;
        return NULL;
    }

    if (cJSON_AddItemToObject(root, ARRAY_NAME, array) == false) {
        fprintf(stderr, "create_user_data_object: failed to add array to root\n");
        cJSON_Delete(root); root = NULL;
    }

    return root;
}

bool user_data_init(UserData* user_data)
{
    if (!user_data)
        return false;

    user_data->liked_videos        = file_exists(LIKED_VIDEOS_FILE)   ? parse_json_file(LIKED_VIDEOS_FILE)   : create_user_data_object();
    user_data->watched_videos      = file_exists(WATCH_HISTORY_FILE)  ? parse_json_file(WATCH_HISTORY_FILE)  : create_user_data_object();
    user_data->subscribed_channels = file_exists(SUBSCRIPTIONS_FILE)  ? parse_json_file(SUBSCRIPTIONS_FILE)  : create_user_data_object();

    return (user_data->liked_videos || user_data->watched_videos || user_data->subscribed_channels);
}

void user_data_free(UserData* user_data)
{
    if (user_data == NULL) return;
    
    if (user_data->watched_videos) {
        write_json_to_file(user_data->watched_videos, WATCH_HISTORY_FILE);
        cJSON_Delete(user_data->watched_videos); user_data->watched_videos = NULL;
    }

    if (user_data->subscribed_channels) {
        write_json_to_file(user_data->subscribed_channels, SUBSCRIPTIONS_FILE);
        cJSON_Delete(user_data->subscribed_channels); user_data->subscribed_channels = NULL;
    }

    if (user_data->liked_videos) {
        write_json_to_file(user_data->liked_videos, LIKED_VIDEOS_FILE);
        cJSON_Delete(user_data->liked_videos); user_data->liked_videos = NULL;
    }
}

bool user_data_is_ready(UserData* user_data)
{
    return (user_data) && 
           (cJSON_IsObject(user_data->liked_videos)) && 
           (cJSON_IsObject(user_data->watched_videos)) && 
           (cJSON_IsObject(user_data->subscribed_channels));
}

int find_user_data_index(const cJSON* user_data, const char* id, const char* id_path)
{
    if ((cJSON_IsArray(user_data) == false) || (valid_string(id) == false) || (valid_string(id_path) == false)) return -1;

    int i = 0;
    
    cJSON* item;
    cJSON_ArrayForEach(item, user_data) {
        const cJSON* id_item = cjson_pointer_get(item, id_path);
        if (json_string_is_valid(id_item) && (strcmp(id_item->valuestring, id) == 0)) {
            return i;
        }

        i++;
    }

    return -1;
}

cJSON* init_search_result_json(const SearchResult* result)
{
    if (result == NULL) return NULL;
    
    cJSON* result_json = cJSON_CreateObject();
    if (result_json == NULL) {
        fprintf(stderr, "init_search_result_json: failed to create cJSON* object\n");
        return NULL;
    }

    cJSON_AddStringToObject(result_json, OBJ_ID_PATH, result->id);
    cJSON_AddStringToObject(result_json, OBJ_TITLE_PATH, result->title);
    cJSON_AddNumberToObject(result_json, OBJ_search_result_type_PATH, result->search_result_type);
    cJSON_AddStringToObject(result_json, OBJ_THUMBNAIL_PATH, result->thumbnail_path);
    
    switch (result->search_result_type) {
        case SEARCH_RESULT_TYPE_LIVE:
        case SEARCH_RESULT_TYPE_SHORT:
        case SEARCH_RESULT_TYPE_VIDEO:
            cJSON_AddStringToObject(result_json, OBJ_AUTHOR_ID_PATH, result->authorId);
            cJSON_AddStringToObject(result_json, OBJ_VIEW_COUNT_PATH, result->view_count);
            cJSON_AddStringToObject(result_json, OBJ_VIDEO_LENGTH_PATH, result->duration);
            cJSON_AddStringToObject(result_json, OBJ_UPLOAD_DATE_PATH, result->date_published);
            break;
        case SEARCH_RESULT_TYPE_CHANNEL:  
            cJSON_AddStringToObject(result_json, OBJ_SUB_COUNT_PATH, result->subscriber_count); 
            break;
        case SEARCH_RESULT_TYPE_PLAYLIST: 
            cJSON_AddStringToObject(result_json, OBJ_VIDEO_COUNT_PATH, result->video_count);
            break;
        default: 
            fprintf(stderr, "init_search_result_json: SearchResultType %d not supported\n", result->search_result_type);
            return NULL;
    }

    return result_json;
}

void delete_user_data(cJSON* user_data, const char* id)
{
    if ((user_data == NULL) || (valid_string(id) == false)) return;

    cJSON* array = cjson_pointer_get(user_data, ARRAY_NAME);
    if (cJSON_IsArray(array) == false) {
        fprintf(stderr, "delete_user_data: invalid array parsed\n");
        return;
    }

    const int i = find_user_data_index(array, id, OBJ_ID_PATH);
    if (i >= 0) {
        cJSON* delete = cJSON_DetachItemFromArray(array, i);
        cJSON_Delete(delete); delete = NULL;
    }
}

void add_user_data(cJSON* user_data, const SearchResult* interacted_result)
{
    if ((user_data == NULL) || (interacted_result == NULL)) return;

    cJSON* array = cjson_pointer_get(user_data, ARRAY_NAME);
    if (cJSON_IsArray(array) == false) {
        fprintf(stderr, "add_user_data: invalid array parsed\n");
        return;
    }

    cJSON* add = NULL;

    const int i = find_user_data_index(array, interacted_result->id, OBJ_ID_PATH);

    if (i >= 0) 
        add = cJSON_DetachItemFromArray(array, i);

    else 
        add = init_search_result_json(interacted_result);

    if (add)
        cJSON_InsertItemInArray(array, 0, add);
}

bool is_subbed_to_channel(cJSON* subscribed_channels_json, const char* id)
{
    if ((subscribed_channels_json == NULL) || (id == NULL)) return false;

    cJSON* subbed_channels = cjson_pointer_get(subscribed_channels_json, ARRAY_NAME);
    
    if (cJSON_IsArray(subbed_channels) == false) return false;

    const int found = find_user_data_index(subbed_channels, id, OBJ_ID_PATH);

    return (found >= 0);
}

void parse_user_data(cJSON* user_data, SearchResult* dest) 
{
    if ((user_data == NULL) || (dest == NULL)) return;

    assign_number_from_path(user_data, OBJ_search_result_type_PATH, (int*) &dest->search_result_type);
    if (dest->search_result_type == SEARCH_RESULT_TYPE_UNDF) 
        return;
    
    assign_string_from_path(user_data, OBJ_ID_PATH, dest->id, sizeof(dest->id));
    assign_string_from_path(user_data, OBJ_TITLE_PATH, dest->title, sizeof(dest->title));
    assign_string_from_path(user_data, OBJ_THUMBNAIL_PATH, dest->thumbnail_path, sizeof(dest->thumbnail_path));

    switch (dest->search_result_type) {
        case SEARCH_RESULT_TYPE_LIVE:
        case SEARCH_RESULT_TYPE_SHORT:
        case SEARCH_RESULT_TYPE_VIDEO:
            assign_string_from_path(user_data, OBJ_AUTHOR_ID_PATH, dest->authorId, sizeof(dest->authorId));
            assign_string_from_path(user_data, OBJ_VIDEO_LENGTH_PATH, dest->duration, sizeof(dest->duration));
            assign_string_from_path(user_data, OBJ_VIEW_COUNT_PATH, dest->view_count, sizeof(dest->view_count));
            assign_string_from_path(user_data, OBJ_UPLOAD_DATE_PATH, dest->date_published, sizeof(dest->date_published));
            break;
        case SEARCH_RESULT_TYPE_CHANNEL:
            assign_string_from_path(user_data, OBJ_SUB_COUNT_PATH, dest->subscriber_count, sizeof(dest->subscriber_count));
            break;
        case SEARCH_RESULT_TYPE_PLAYLIST:
            assign_string_from_path(user_data, OBJ_VIDEO_COUNT_PATH, dest->video_count, sizeof(dest->video_count));
            break;
        case SEARCH_RESULT_TYPE_ANY:
        case SEARCH_RESULT_TYPE_UNDF:
            break;
    }
}

void load_user_data(List* results, cJSON* user_data)
{
    if ((results == NULL) || (user_data == NULL)) return;

    cJSON* array = cjson_pointer_get(user_data, ARRAY_NAME);
    if (cJSON_IsArray(array) == false) {
        fprintf(stderr, "load_user_data: invalid array parsed\n");
        return;
    }

    pthread_mutex_lock(&results->mutex);
    
    const int old_size = results->count;

    cJSON* item;
    cJSON_ArrayForEach(item, array) {
        Node* node = node_init((void*) search_result_init, NULL, (void*) search_result_free, NULL);
        if (node) {
            SearchResult* dest = (SearchResult*) node->content;
            if (dest) {
                parse_user_data(item, dest);
                
                if (dest->search_result_type != SEARCH_RESULT_TYPE_UNDF) 
                    list_append(results, node);
                else 
                    node_free(node);
            }
        }
    }

    for (int i = 0; i < old_size; i++) {
        node_free(list_dequeue(results));
    }

    pthread_mutex_unlock(&results->mutex);
}

// query operations

typedef struct
{
    Query query;
    List* results;
    SSL_CTX* ssl_ctx;
    ClientContext* client_context;
} SearchThreadArgs;

void* get_results_from_query(ThreadArgs args)
{
    SearchThreadArgs* targs = (SearchThreadArgs*) args;
    if ((targs == NULL) || 
        (targs->results == NULL) || 
        (targs->ssl_ctx == NULL) || 
        (targs->client_context == NULL)) {
        fprintf(stderr, "get_results_from_query: invalid args\n");
        return NULL;
    }

    ClientContext* client_context = targs->client_context;
    
    Connection* youtube_conn = &client_context->youtube_api_pool.connections[client_context->youtube_api_pool.current_conn];
    char** continuation_token = &targs->client_context->continuation_token;
    const char* api_key = targs->client_context->api_key;

    HttpsRequest req = configure_post_request(targs->query, youtube_conn->host, api_key, (*continuation_token));
    if (post_request_is_ready(req) == false) {
        fprintf(stderr, "get_results_from_query: failed to resolve request\n");
        return NULL;
    }

    cJSON* res = get_json_response(&req, targs->ssl_ctx, youtube_conn, HTTP_PROTOCOL_VER);

    free(req.payload); req.payload = NULL;

    if (res == NULL) {
        fprintf(stderr, "get_results_from_query: failed to resolve json response\n");
        return NULL;
    }

    const QueryType query_type = targs->query.type;
    const QueryAttribute query_attr = targs->query.attr;
    
    pthread_mutex_lock(&targs->results->mutex);
    create_results_from_json(res, targs->results, query_type, query_attr, targs->query.allow_youtube_shorts);
    pthread_mutex_unlock(&targs->results->mutex);

    pthread_mutex_lock(&targs->client_context->token_mutex);
    get_continuation_token(res, continuation_token, query_type, query_attr);
    pthread_mutex_unlock(&targs->client_context->token_mutex);
    
    cJSON_Delete(res); res = NULL;

    return NULL;
}

void queue_search_task(SSL_CTX* ssl_ctx, ClientContext* client_context, List* task_queue, List* results, Query query)
{
    if (!ssl_ctx || !client_context || !task_queue || !results)
        return;

    SearchThreadArgs* targs = malloc(sizeof(SearchThreadArgs));
    if (!targs) {
        fprintf(stderr, "queue_search_task: malloc returned null\n");
        return;
    }

    targs->query = query;
    targs->results = results;
    targs->ssl_ctx = ssl_ctx;
    targs->client_context = client_context;

    thread_task_launch(task_queue, targs, get_results_from_query);
}

typedef struct
{
    SearchResult info;
    char* description;
} HighlightedVideo;

typedef struct
{
    HttpsRequest req;
    SSL_CTX* ssl_ctx;
    ClientContext* client_context;
    HighlightedVideo* highlighted_video;
} VideoMetadataArgs;

void* get_video_metadata(ThreadArgs args)
{
    VideoMetadataArgs* targs = (VideoMetadataArgs*) args;
    if ((targs == NULL) || 
        (!post_request_is_ready(targs->req)) ||
        (targs->ssl_ctx == NULL) || 
        (targs->client_context == NULL) || 
        (targs->highlighted_video == NULL)) {
        fprintf(stderr, "get_video_metadata: invalid args\n");
        return NULL;
    }

    ClientContext* client_context = targs->client_context;

    Connection* youtube_conn = &client_context->youtube_api_pool.connections[client_context->youtube_api_pool.current_conn];

    cJSON* res = get_json_response(&targs->req, targs->ssl_ctx, youtube_conn, HTTP_PROTOCOL_VER);
    
    free(targs->req.payload); targs->req.payload = NULL;

    if (res == NULL) {
        fprintf(stderr, "get_video_metadata: failed to resolve json response\n");
        return NULL;
    }

    if (targs->highlighted_video->description) {
        free(targs->highlighted_video->description); targs->highlighted_video->description = NULL;
    }

    targs->highlighted_video->description = get_video_description(res);

    cJSON_Delete(res); res = NULL;

    return NULL;
}

void queue_video_metadata_task(SSL_CTX* ssl_ctx, ClientContext* client_context, ThreadContext* thread_ctx, HighlightedVideo* highlighted_video, Query query)
{
    if (!ssl_ctx || !client_context || !thread_ctx || !highlighted_video)
        return;

    HttpsRequest req = configure_post_request(query, client_context->youtube_api_pool.connections->host, client_context->api_key, client_context->continuation_token);

    VideoMetadataArgs* targs = malloc(sizeof(VideoMetadataArgs));
    if (!targs) {
        fprintf(stderr, "queue_video_metadata_task: malloc returned null\n");
        return;
    }

    targs->req = req;
    targs->ssl_ctx = ssl_ctx;
    targs->client_context = client_context;
    targs->highlighted_video = highlighted_video;
    
    if (!thread_task_launch(&thread_ctx->task_queue, targs, get_video_metadata)) {
        fprintf(stderr, "queue_video_metadata_task: failed to queue task\n");
        free(targs); targs = NULL;
    }
}

typedef struct
{
    SearchResult info;
    TextureCacheEntry* cached;
    bool is_subscribed;
} HighlightedChannel;

typedef struct
{
    Query query;
    ThumbnailLoader* thumbnail_loader;
    cJSON* subscribed_channels_json;
    HighlightedChannel* channel;
    ClientContext* client_context;
    SSL_CTX* ssl_ctx;
    List* results;
} ChannelMetadataArgs;

void* get_channel_metadata(ThreadArgs args)
{
    ChannelMetadataArgs* targs = (ChannelMetadataArgs*) args;
    if ((targs == NULL) || 
        (targs->subscribed_channels_json == NULL) || 
        (targs->channel == NULL) || 
        (targs->client_context == NULL) || 
        (targs->ssl_ctx == NULL) || 
        (targs->thumbnail_loader == NULL) || 
        (targs->results == NULL)) {
        fprintf(stderr, "get_channel_metadata: invalid args\n");
        return NULL;
    }

    ClientContext* client_context = targs->client_context;
    Connection* youtube_conn = &client_context->youtube_api_pool.connections[client_context->youtube_api_pool.current_conn];
    char** continuation_token = &client_context->continuation_token; 
    const char* api_key = client_context->api_key;

    HttpsRequest results_req = configure_post_request(targs->query, youtube_conn->host, api_key, (*continuation_token));
    if (!post_request_is_ready(results_req)) {
        fprintf(stderr, "get_channel_metadata: failed to resolve results request\n");
        return NULL;
    }

    cJSON* res = get_json_response(&results_req, targs->ssl_ctx, youtube_conn, HTTP_PROTOCOL_VER);

    free(results_req.payload); results_req.payload = NULL;
    
    if (res == NULL) {
        fprintf(stderr, "get_channel_metadata: failed to resolve json response\n");
        return NULL;
    }

    const QueryType query_type = targs->query.type;
    const QueryAttribute query_attr = targs->query.attr;
    
    pthread_mutex_lock(&targs->results->mutex);
    create_results_from_json(res, targs->results, query_type, query_attr, false);
    pthread_mutex_unlock(&targs->results->mutex);

    pthread_mutex_lock(&client_context->token_mutex);
    get_continuation_token(res, continuation_token, query_type, query_attr);
    pthread_mutex_unlock(&client_context->token_mutex);

    if (query_attr == QUERY_ATTR_REPLACE) {
        const bool channel_parse_status = parse_highlighted_channel(res, &targs->channel->info);
        
        cJSON_Delete(res); res = NULL;
        
        if (!channel_parse_status) {
            fprintf(stderr, "get_channel_metadata: failed to parse channel information\n");
            targs->channel->info.thumbnail_loaded = false;
            targs->channel->info.thumbnail_path[0] = '\0';
            targs->channel->cached = NULL;
            return NULL;
        }

        targs->channel->is_subscribed = is_subbed_to_channel(targs->subscribed_channels_json, targs->channel->info.id);
   
        LoadThumbnailArgs* thumb_args = malloc(sizeof(LoadThumbnailArgs));
        if (thumb_args == NULL) {
            fprintf(stderr, "get_channel_metadata: malloc returned null\n");
            targs->channel->info.thumbnail_loaded = false;
            targs->channel->info.thumbnail_path[0] = '\0';
            targs->channel->cached = NULL;
            return NULL;
        }
        
        Connection* channel_conn = thumbnail_loader_get_connection(targs->thumbnail_loader, SEARCH_RESULT_TYPE_CHANNEL);
        
        if (channel_conn == NULL) {
            fprintf(stderr, "get_channel_metadata: failed to resolve channel thumbnail connection\n");
            targs->channel->info.thumbnail_loaded = false;
            targs->channel->info.thumbnail_path[0] = '\0';
            targs->channel->cached = NULL;
            return NULL;
        }

        strncpy(thumb_args->id, targs->channel->info.id, sizeof(thumb_args->id));
        strncpy(thumb_args->path, targs->channel->info.thumbnail_path, sizeof(thumb_args->path));
        thumb_args->loader = targs->thumbnail_loader;
        thumb_args->search_result_type = SEARCH_RESULT_TYPE_CHANNEL;
        thumb_args->ssl_ctx = targs->ssl_ctx;

        load_thumbnail(thumb_args);

        free(thumb_args); thumb_args = NULL;

        targs->channel->info.thumbnail_loaded = false;
    }

    return NULL;
}

typedef struct
{
    char video_id[64];
    bool* playing_video;
} PlayVideoArgs;

bool configure_watch_url(const char* video_id, char* dest, const size_t dest_size)
{
    if (!valid_string(video_id) || !dest)
        return false;

    const int written = snprintf(dest, dest_size, "mpv https://www.youtube.com/watch?v=%s", video_id);
    
    return (0 < written) && (written < dest_size);
}

void* play_video(ThreadArgs args)
{
    PlayVideoArgs* targs = (PlayVideoArgs*) args;

    if (!targs || !valid_string(targs->video_id) || !targs->playing_video)
        return NULL;

    char command[512] = {0};
    
    if (!configure_watch_url(targs->video_id, command, sizeof(command)))
        return false;

    (*targs->playing_video) = true;

    system(command);

    (*targs->playing_video) = false;
    
    return NULL;
}

typedef struct
{
    bool is_playing_video;
    bool load_query_results;
    bool load_video_metadata;
    bool load_channel_metadata;
    bool load_liked_videos;
    bool load_watch_history;
    bool load_subscribed_channels;
} UpdateFlags;

void init_app()
{
    SetTargetFPS(60);
    SetTraceLogLevel(LOG_ERROR);
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    SetConfigFlags(FLAG_WINDOW_ALWAYS_RUN);
    InitWindow(1000, 750, "Metube");
}

// ui stuff

typedef struct
{
    Font font;
    Color text_color;
    int font_size;
    int padding;
    int spacing;
    bool word_wrap;
} Ui;

void DrawTextBoxedSelectable(Ui ui, const char *text, Rectangle rec, float fontSize, Color tint, int selectStart, int selectLength, Color selectTint, Color selectBackTint)
{
    int length = TextLength(text);  // Total length in bytes of the text, scanned by codepoints in loop

    float textOffsetY = 0;          // Offset between lines (on line break '\n')
    float textOffsetX = 0.0f;       // Offset X to next character to draw

    float scaleFactor = fontSize/(float)ui.font.baseSize;     // Character rectangle scaling factor

    // Word/character wrapping mechanism variables
    enum { MEASURE_STATE = 0, DRAW_STATE = 1 };
    int state = ui.word_wrap? MEASURE_STATE : DRAW_STATE;

    int startLine = -1;         // Index where to begin drawing (where a line begins)
    int endLine = -1;           // Index where to stop drawing (where a line ends)
    int lastk = -1;             // Holds last value of the character position

    for (int i = 0, k = 0; i < length; i++, k++)
    {
        // Get next codepoint from byte string and glyph index in font
        int codepointByteCount = 0;
        int codepoint = GetCodepoint(&text[i], &codepointByteCount);
        int index = GetGlyphIndex(ui.font, codepoint);

        // NOTE: Normally we exit the decoding sequence as soon as a bad byte is found (and return 0x3f)
        // but we need to draw all of the bad bytes using the '?' symbol moving one byte
        if (codepoint == 0x3f) codepointByteCount = 1;
        i += (codepointByteCount - 1);

        float glyphWidth = 0;
        if (codepoint != '\n')
        {
            glyphWidth = (ui.font.glyphs[index].advanceX == 0) ? ui.font.recs[index].width*scaleFactor : ui.font.glyphs[index].advanceX*scaleFactor;

            if (i + 1 < length) glyphWidth = glyphWidth + ui.spacing;
        }

        // NOTE: When wordWrap is ON we first measure how much of the text we can draw before going outside of the rec container
        // We store this info in startLine and endLine, then we change states, draw the text between those two variables
        // and change states again and again recursively until the end of the text (or until we get outside of the container).
        // When wordWrap is OFF we don't need the measure state so we go to the drawing state immediately
        // and begin drawing on the next line before we can get outside the container.
        if (state == MEASURE_STATE)
        {
            // TODO: There are multiple types of spaces in UNICODE, maybe it's a good idea to add support for more
            // Ref: http://jkorpela.fi/chars/spaces.html
            if ((codepoint == ' ') || (codepoint == '\t') || (codepoint == '\n')) endLine = i;

            if ((textOffsetX + glyphWidth) > rec.width)
            {
                endLine = (endLine < 1)? i : endLine;
                if (i == endLine) endLine -= codepointByteCount;
                if ((startLine + codepointByteCount) == endLine) endLine = (i - codepointByteCount);

                state = !state;
            }
            else if ((i + 1) == length)
            {
                endLine = i;
                state = !state;
            }
            else if (codepoint == '\n') state = !state;

            if (state == DRAW_STATE)
            {
                textOffsetX = 0;
                i = startLine;
                glyphWidth = 0;

                // Save character position when we switch states
                int tmp = lastk;
                lastk = k - 1;
                k = tmp;
            }
        }
        else
        {
            if (codepoint == '\n')
            {
                if (!ui.word_wrap)
                {
                    textOffsetY += (ui.font.baseSize + ui.font.baseSize / 2.0f) * scaleFactor;
                    textOffsetX = 0;
                }
            }
            else
            {
                if (!ui.word_wrap && ((textOffsetX + glyphWidth) > rec.width))
                {
                    textOffsetY += (ui.font.baseSize + ui.font.baseSize / 2.0f) * scaleFactor;
                    textOffsetX = 0;
                }

                // When text overflows rectangle height limit, just stop drawing
                if ((textOffsetY + ui.font.baseSize*scaleFactor) > rec.height) break;

                // Draw selection background
                bool isGlyphSelected = false;
                if ((selectStart >= 0) && (k >= selectStart) && (k < (selectStart + selectLength)))
                {
                    DrawRectangleRec((Rectangle){ rec.x + textOffsetX - 1, rec.y + textOffsetY, glyphWidth, (float)ui.font.baseSize*scaleFactor }, selectBackTint);
                    isGlyphSelected = true;
                }

                // Draw current character glyph
                if ((codepoint != ' ') && (codepoint != '\t'))
                {
                    DrawTextCodepoint(ui.font, codepoint, (Vector2){ rec.x + textOffsetX, rec.y + textOffsetY }, fontSize, isGlyphSelected? selectTint : tint);
                }
            }

            if (ui.word_wrap && (i == endLine))
            {
                textOffsetY += (ui.font.baseSize + ui.font.baseSize / 2.0f) * scaleFactor;
                textOffsetX = 0;
                startLine = endLine;
                endLine = -1;
                glyphWidth = 0;
                selectStart += lastk - k;
                k = lastk;

                state = !state;
            }
        }

        if ((textOffsetX != 0) || (codepoint != ' ')) textOffsetX += glyphWidth;  // avoid leading spaces
    }
}

void DrawTextBoxed(const char *text, Rectangle rec, Ui ui, float fontSize, Color tint)
{
    DrawTextBoxedSelectable(ui, text, rec, fontSize, tint, 0, 0, WHITE, WHITE);
}

Rectangle get_padded_rectangle(const float padding, const Rectangle rect)
{
    return (Rectangle) { rect.x + padding, rect.y + padding, rect.width - (padding * 2), rect.height - (padding * 2) };
}

void draw_thumbnail_subtext(const Rectangle container, Ui ui, const Color text_color, const int font_size, const char* text)
{
    const Vector2 text_size = MeasureTextEx(ui.font, text, font_size, ui.spacing);
    const float content_width = text_size.x + (ui.padding * 2);
    const float content_height = text_size.y + (ui.padding * 2);
    
    const Rectangle length_area = {
        .x = container.x + container.width - content_width - ui.padding,
        .y = container.y + container.height - content_height - ui.padding,
        .width = content_width,
        .height = content_height
    };

    // draw box with text inside it
    DrawRectangleRec(length_area, Fade(BLACK, 0.7));
    DrawTextEx(ui.font, text, (Vector2){length_area.x + ui.padding, length_area.y + ui.padding}, font_size, ui.spacing, text_color);
}

bool draw_toggle_filter(const Rectangle container, const Ui ui, const char* label_text, const char* value_text)
{
    const Color text_color = BLACK;
    const int font_size = 10;

    const char* button_text = "SWITCH";
    const float button_width = 50;
    const float widget_height = container.height - (ui.padding * 2);

    const float y_level = container.y + ui.padding;

    const Vector2 label_pos = {
        .x = container.x + ui.padding,
        .y = y_level,
    };

    DrawTextEx(ui.font, label_text, label_pos,font_size, ui.spacing, text_color);
    
    const Vector2 value_pos = {
        .x = ui.padding + (container.x + (container.width / 2.0f)) - (MeasureTextEx(ui.font, value_text, font_size, ui.spacing).x / 2.0f),
        .y = y_level
    };

    DrawTextEx(ui.font, value_text, value_pos, font_size, ui.spacing, text_color);
    
    const Rectangle button_bounds = {
        .x = container.x + container.width - button_width - ui.padding,
        .y = y_level,
        .width = button_width,
        .height = widget_height,
    };

    return GuiButton(button_bounds, button_text);
}

void draw_filter_window(const Rectangle container, const Ui ui, Query* query)
{
    DrawRectangleLinesEx(container, 1, GRAY);

    const SortType filterable_sort_types [] = {
        SORT_TYPE_RELEVANCE,
        SORT_TYPE_UPLOAD_DATE,
        SORT_TYPE_VIEW_COUNT,
        SORT_TYPE_RATING,
    };          

    const size_t nsorts = sizeof(filterable_sort_types) / sizeof(filterable_sort_types[0]);

    const Rectangle sort_type_bounds = {
        .x = container.x,
        .y = container.y,
        .width = container.width,
        .height = container.height / 3.0f,
    };

    const char* sort_text = sort_type_to_text(query->sort);

    if (draw_toggle_filter(sort_type_bounds, ui, "ORDER", sort_text)) {
        query->sort = bound_index_to_array((query->sort + 1), nsorts);
    }

    const SearchResultType filterable_search_result_types [] = {
        SEARCH_RESULT_TYPE_ANY,
        SEARCH_RESULT_TYPE_VIDEO,
        SEARCH_RESULT_TYPE_CHANNEL,
        SEARCH_RESULT_TYPE_PLAYLIST,
        SEARCH_RESULT_TYPE_LIVE,
    };

    const size_t nmedias = sizeof(filterable_search_result_types) / sizeof(filterable_search_result_types[0]);

    const Rectangle search_result_type_bounds = {
        .x = container.x,
        .y = sort_type_bounds.y + sort_type_bounds.height,
        .width = container.width,
        .height = container.height / 3.0f,
    };

    const char* media_text = search_result_type_to_text(query->media);

    if (draw_toggle_filter(search_result_type_bounds, ui, "TYPE", media_text)) {
        query->media = bound_index_to_array((query->media + 1), nmedias);
    }

    const Rectangle allow_shorts_bounds = {
        .x = container.x,
        .y = search_result_type_bounds.y + search_result_type_bounds.height,
        .width = container.width,
        .height = container.height / 3.0f,
    };

    const char* allow_shorts_text = query->allow_youtube_shorts ? "YES" : "NO";

    if (draw_toggle_filter(allow_shorts_bounds, ui, "SHORTS", allow_shorts_text)) {
        query->allow_youtube_shorts = !query->allow_youtube_shorts;
    }
}

void draw_search_result(SearchResult *search_result, const Texture thumbnail, const Rectangle container,  const Color color, const Ui ui)
{
    DrawRectangleRec(container, color);

    const int font_size = 12;

    const float thumbnail_width = search_result_type_to_thumbnail_width(SEARCH_RESULT_TYPE_VIDEO);

    const Rectangle thumbnail_area = { 
        .x = container.x, 
        .y = container.y, 
        .width = thumbnail_width, 
        .height = container.height 
    };

    const Rectangle title_area = {
        .x = thumbnail_area.x + thumbnail_area.width,
        .y = thumbnail_area.y,
        .width = container.width - thumbnail_area.width,
        .height = thumbnail_area.height * 0.70f
    };

    if (search_result->title[0] != '\0') {
        DrawTextBoxed(search_result->title, get_padded_rectangle(ui.padding, title_area), ui, font_size, BLACK);                            
    }

    const Rectangle subtext_area = {
        .x = thumbnail_area.x + thumbnail_area.width,
        .y = title_area.y + title_area.height,
        .width = title_area.width,
        .height = container.height - title_area.height,
    };

    switch (search_result->search_result_type) {
        case SEARCH_RESULT_TYPE_SHORT:
        case SEARCH_RESULT_TYPE_VIDEO: {
            const Vector2 date_published_pos = {
                .x = subtext_area.x + ui.padding,
                .y = subtext_area.y + ui.padding,
            };

            DrawTextEx(ui.font, search_result->date_published, date_published_pos, font_size, ui.spacing, BLACK);
            
            const Vector2 view_count_pos = {
                .x = subtext_area.x + subtext_area.width - MeasureTextEx(ui.font, search_result->view_count, font_size, ui.spacing).x - ui.padding,
                .y = subtext_area.y + ui.padding,
            };

            DrawTextEx(ui.font, search_result->view_count, view_count_pos, font_size, ui.spacing, BLACK);
            
            DrawTextureEx(thumbnail, (Vector2){thumbnail_area.x, thumbnail_area.y}, 0.0f, 1.0f, WHITE);
            draw_thumbnail_subtext(thumbnail_area, ui, RAYWHITE, font_size, search_result->duration);
            break;
        }
        case SEARCH_RESULT_TYPE_LIVE: {
            const Vector2 view_count_pos = {
                .x = subtext_area.x + ui.padding,
                .y = subtext_area.y + ui.padding,
            };

            DrawTextEx(ui.font, TextFormat("%s watching", search_result->view_count), view_count_pos, font_size, ui.spacing, BLACK);
            
            DrawTextureEx(thumbnail, (Vector2){thumbnail_area.x, thumbnail_area.y}, 0.0f, 1.0f, WHITE);
            draw_thumbnail_subtext(thumbnail_area, ui, RAYWHITE, 12, "LIVE");
            break;
        }
        case SEARCH_RESULT_TYPE_CHANNEL: {
            const float x_padding = thumbnail.width / 2.0f;
            const float y_padding = (container.height - thumbnail.height) / 2.0f;
            DrawTextureEx(thumbnail, (Vector2){thumbnail_area.x + x_padding, thumbnail_area.y + y_padding}, 0.0f, 1.0f, WHITE);
            DrawTextBoxed(search_result->subscriber_count, get_padded_rectangle(ui.padding, subtext_area), ui, 12, BLACK);
            draw_thumbnail_subtext(thumbnail_area, ui, RAYWHITE, 12, "Channel");
            break;
        }
        case SEARCH_RESULT_TYPE_PLAYLIST:
            DrawTextureEx(thumbnail, (Vector2){thumbnail_area.x, thumbnail_area.y}, 0.0f, 1.0f, WHITE);
            draw_thumbnail_subtext(thumbnail_area, ui, RAYWHITE, 12, search_result->video_count);
            break;
        default:    
            break;
    }
}

const int anticipate_lines(const Font font, const char* text, int font_size, int spacing, bool word_wrap, float container_width)
{
    if (valid_string(text) == false) return 1;

    int nlines = 1;
    float line_w = 0.0f;

    if (word_wrap) {
        const char* word_start = text;
        while (*word_start) {
            const char* word_end = word_start;
            while (*word_end && *word_end != ' ' && *word_end != '\n') 
                word_end++;

            size_t word_len = (size_t)(word_end - word_start);
            if (word_len > 0) {
                char word_buf[word_len + 1];
                memcpy(word_buf, word_start, word_len);
                word_buf[word_len] = '\0';

                float word_w = MeasureTextEx(font, word_buf, font_size, spacing).x;

                if (line_w + word_w > container_width) {
                    nlines++;
                    line_w = 0.0f;
                }

                line_w += word_w;
            }

            if (*word_end == ' ') {
                Vector2 space_size = MeasureTextEx(font, " ", font_size, spacing);
                line_w += space_size.x;
                word_end++;
            } 
            
            else if (*word_end == '\n') {
                nlines++;
                line_w = 0.0f;
                word_end++;
            }

            word_start = word_end;
        }
    } 
    
    else {
        const char* p = text;
        
        char buf[2] = {0};

        while (*p) {
            if (*p == '\n') {
                nlines++;
                line_w = 0.0f;
            } else {
                buf[0] = *p;
                float char_w = MeasureTextEx(font, buf, font_size, spacing).x;
                line_w += char_w;

                if (line_w > container_width) {
                    nlines++;
                    line_w = char_w;
                }
            }

            p++;
        }
    }

    return nlines;
}

void draw_text_scrollable(const Rectangle scroll_window_bounds, const bool show_scrollbar, const Ui ui, Vector2* scrollbar_position, const char* text)
{
    if (show_scrollbar && (scrollbar_position == NULL)) return;

    const float padded_width = scroll_window_bounds.width - (ui.padding * 2);
    const int nlines = anticipate_lines(ui.font, text, ui.font_size, ui.spacing, ui.word_wrap, padded_width);
    
    const float text_height = (ui.font_size + ui.spacing + ui.padding) * nlines;
    Rectangle content_area = get_padded_rectangle(ui.padding, scroll_window_bounds);
    content_area.height = text_height;

    GuiScrollPanel(scroll_window_bounds, NULL, content_area, scrollbar_position, NULL, show_scrollbar);

    Rectangle text_area = content_area;
    text_area.y += scrollbar_position->y;
    BeginScissorMode(scroll_window_bounds.x, scroll_window_bounds.y, scroll_window_bounds.width, scroll_window_bounds.height);
    DrawTextBoxed(text, text_area, ui, ui.font_size, ui.text_color);
    EndScissorMode();
}

void draw_highlighted_channel(const Rectangle container, const Ui* ui, cJSON* subscribed_channels_json, HighlightedChannel* highlighted_channel)
{
    DrawRectangleLinesEx(container, 1, GRAY);
    
    const float font_size = 17;
    const float title_size = 25;

    if ((ui == NULL) || (highlighted_channel == NULL) || (highlighted_channel->cached == NULL) || (highlighted_channel->info.id[0] == '\0')) return;

    const float thumbnail_w = search_result_type_to_thumbnail_width(SEARCH_RESULT_TYPE_CHANNEL);
    
    if (texture_cache_entry_is_ready(highlighted_channel->cached)) {
        timer_start(&highlighted_channel->cached->timer, CACHED_TEXTURE_LIFETIME);
        DrawTextureEx(highlighted_channel->cached->texture, (Vector2){container.x + ui->padding, container.y + ui->padding}, 0.0f, 1.0f, RAYWHITE);
    }

    const Vector2 title_pos = {
        .x = container.x + thumbnail_w + (ui->padding * 2),
        .y = container.y + (container.height / 3.0f) - (title_size / 2.0f),
    };

    DrawTextEx(ui->font, highlighted_channel->info.title, title_pos, title_size, ui->spacing, BLACK);
    
    const Vector2 sub_count_pos = {
        .x = container.x + thumbnail_w + (ui->padding * 2),
        .y = container.y + container.height - MeasureTextEx(ui->font, highlighted_channel->info.subscriber_count, font_size, ui->spacing).y - ui->padding,
    };

    DrawTextEx(ui->font, highlighted_channel->info.subscriber_count, sub_count_pos, font_size, ui->spacing, BLACK);

    const float button_width = 75;
    const float widget_height = 25;
    const Rectangle subscribe_button_bounds = {
        .x = container.x + container.width - button_width - ui->padding,
        .y = container.y + container.height - widget_height - ui->padding,
        .width = button_width,
        .height = 25,
    };

    const char* button_text = highlighted_channel->is_subscribed ? "Unsubscribe" : "Subscribe";

    if (GuiButton(subscribe_button_bounds, button_text)) {
        if (highlighted_channel->is_subscribed)
            delete_user_data(subscribed_channels_json, highlighted_channel->info.id);
        else
            add_user_data(subscribed_channels_json, &highlighted_channel->info);
        
        highlighted_channel->is_subscribed = !highlighted_channel->is_subscribed;
    }
}

void draw_video_management_buttons(const Rectangle container, Query* query, HighlightedVideo* selected_video, cJSON* liked_video_data, UpdateFlags* update_flags, ThreadContext* thread_context, UserData* user_data)
{
    const int padding = 5;

    const int nbuttons = 4;
    const int min_button_width = 5;
    const float button_width = (container.width - (padding * (nbuttons - 1))) / nbuttons; 

    const Rectangle play_video_button_bounds = {
        .x = container.x,
        .y = container.y,
        .width = fmax(min_button_width, button_width),
        .height = container.height,
    };

    if (!valid_string(selected_video->info.id) || update_flags->is_playing_video) 
        GuiSetState(STATE_DISABLED);

    if (GuiButton(play_video_button_bounds, "Play Video")) {
        PlayVideoArgs* targs = malloc(sizeof(PlayVideoArgs));
        if (targs) {
            targs->playing_video = &update_flags->is_playing_video;
            snprintf(targs->video_id, sizeof(targs->video_id), "%s", selected_video->info.id);
            thread_task_launch(&thread_context->task_queue, targs, play_video);
            add_user_data(user_data->watched_videos, &selected_video->info);
        }
    } 

    if (update_flags->is_playing_video)
        GuiSetState(STATE_NORMAL);

    const Rectangle like_video_button_bounds = {
        .x = play_video_button_bounds.x + play_video_button_bounds.width + padding,
        .y = padding,
        .width = fmax(min_button_width, button_width),
        .height = container.height,
    };

    if (GuiButton(like_video_button_bounds, "Like Video")) {
        add_user_data(liked_video_data, &selected_video->info);
    }

    const Rectangle related_videos_button_bounds = {
        .x = like_video_button_bounds.x + like_video_button_bounds.width + padding,
        .y = container.y,
        .width = fmax(min_button_width, button_width),
        .height = container.height,
    };

    if (GuiButton(related_videos_button_bounds, "Related Videos")) {
        update_flags->load_query_results = true;
        query->attr = QUERY_ATTR_REPLACE;
        query->type = QUERY_TYPE_VIEW_RELATED;
        strncpy(query->focused_id, selected_video->info.id, sizeof(query->focused_id) - 1);
        query->focused_id[sizeof(query->focused_id) - 1] = '\0';
    }
    
    const Rectangle users_videos_button_bounds = {
        .x = related_videos_button_bounds.x + related_videos_button_bounds.width + padding,
        .y = container.y,
        .width = fmax(min_button_width, button_width),
        .height = container.height,
    };

    if (GuiButton(users_videos_button_bounds, "User's Videos")) {
        update_flags->load_channel_metadata = true;
        query->attr = QUERY_ATTR_REPLACE;
        query->type = QUERY_TYPE_VIEW_CHANNEL;

        strncpy(query->focused_id, selected_video->info.authorId, sizeof(query->focused_id) - 1);
        query->focused_id[sizeof(query->focused_id) - 1] = '\0';
    }

    GuiSetState(STATE_NORMAL);
}

void draw_user_data_buttons(const Rectangle container, Query* query, UpdateFlags* update_flags)
{
    const int padding = 5;
    const int nbuttons = 3;
    
    const int min_button_width = 5;
    const float button_width = (container.width - (padding * (nbuttons - 1))) / nbuttons;

    const Rectangle subscriptions_button_bounds = {
        .x = container.x,
        .y = container.y,
        .width = fmax(min_button_width, button_width),
        .height = container.height,
    };

    if (GuiButton(subscriptions_button_bounds, "Subscribed Channels")) {
        update_flags->load_subscribed_channels = true;
        query->attr = QUERY_ATTR_REPLACE;
        query->type = QUERY_TYPE_VIEW_SUBSCRIBED_CHANNELS;
    }

    const Rectangle liked_videos_button_bounds = {
        .x = subscriptions_button_bounds.x + subscriptions_button_bounds.width + padding,
        .y = container.y,
        .width = fmax(min_button_width, button_width),
        .height = container.height,
    };

    if (GuiButton(liked_videos_button_bounds, "Liked Videos")) {
        update_flags->load_liked_videos = true;
    }
    
    const Rectangle watch_history_button_bounds = {
        .x = liked_videos_button_bounds.x + liked_videos_button_bounds.width + padding,
        .y = container.y,
        .width = fmax(min_button_width, button_width),
        .height = container.height,
    };

    if (GuiButton(watch_history_button_bounds, "Watch History")) {
        update_flags->load_watch_history = true;
        query->attr = QUERY_ATTR_REPLACE;
        query->type = QUERY_TYPE_VIEW_WATCH_HISTORY;
    }
}

void draw_load_more_button(const Rectangle container, const Font font, Query* query, QueryType last_query, bool* load_more)
{
    const int spacing = 2;
    const int font_size = 50;
    const Color text_color = BLACK;
    const char* text = "<< LOAD MORE >>";
    const Vector2 text_dimensions = MeasureTextEx(font, text, font_size, spacing);

    const Vector2 text_pos = {
        .x = container.x + ((container.width - text_dimensions.x) / 2.0f),
        .y = container.y + (text_dimensions.y / 2.0f)
    };

    DrawTextEx(font, text, text_pos, font_size, spacing, text_color);
    if ((CheckCollisionPointRec(GetMousePosition(), container)) && 
        (IsMouseButtonPressed(MOUSE_BUTTON_LEFT))) {
        *load_more = true;
        query->type = last_query;
        query->attr = QUERY_ATTR_APPEND;
    }
}

void draw_search_bar(const Rectangle container, const Ui* ui, Query* query, UpdateFlags* update_flags, bool* text_box_focused, bool* show_filter_window)
{
    const int button_w = 25;
    const int widget_h = 25;

    const char* trending_button_text = "T";

    const Rectangle trending_button_bounds = {
        .x = ui->padding,
        .y = ui->padding,
        .width = button_w,
        .height = widget_h,
    };

    if (GuiButton(trending_button_bounds, trending_button_text)) {
        update_flags->load_query_results = true;
        query->attr = QUERY_ATTR_REPLACE;
        query->type = QUERY_TYPE_VIEW_TRENDING;
    }

    const Rectangle text_bar_bounds = {
        .x = trending_button_bounds.x + trending_button_bounds.width + ui->padding, 
        .y = ui->padding, 
        .width = (container.width * 0.75f), 
        .height = widget_h, 
    };

    if (GuiTextBox(text_bar_bounds, query->string, sizeof(query->string), (*text_box_focused))) 
        (*text_box_focused) = !(*text_box_focused);

    const Rectangle search_button_bounds = {
        .x = text_bar_bounds.x + text_bar_bounds.width + ui->padding, 
        .y = ui->padding, 
        .width = button_w, 
        .height = widget_h
    };

    if ((GuiButton(search_button_bounds, "S") || IsKeyPressed(KEY_ENTER)) && 
       (trim_whitespace(query->string) > 0)) {
        update_flags->load_query_results = true;
        query->attr = QUERY_ATTR_REPLACE;
        query->type = QUERY_TYPE_USER_INPUT;
    }

    const char* filter_button_text = "Fil";

    const Rectangle filter_button_bounds = {
        .x = search_button_bounds.x + search_button_bounds.width + ui->padding,
        .y = ui->padding,
        .width = button_w,
        .height = widget_h,
    };

    if (GuiButton(filter_button_bounds, filter_button_text)) 
        (*show_filter_window) = !(*show_filter_window);
}

void handle_view_user_data(List* results, pthread_mutex_t* token_mutex, char** continuation_token, cJSON* user_data, bool* update_flag)
{
    if (!results || !token_mutex || !continuation_token || !user_data || !update_flag)
        return;

    (*update_flag) = false;

    pthread_mutex_lock(token_mutex);

    if ((*continuation_token)) {
        free((*continuation_token)); (*continuation_token) = NULL;
    }

    pthread_mutex_unlock(token_mutex);

    load_user_data(results, user_data);
}

// handle when exiting application before mpv is closed

int main()
{
    init_app();

    Ui ui = {
        .font = GetFontDefault(),
        .font_size = 12,
        .text_color = BLACK,
        .padding = 5,
        .spacing = 2,
        .word_wrap = true,
    };
    
    Vector2 result_scrollbar = {0};
    Vector2 description_scrollbar = {0};
    bool text_box_focused = false;
    bool show_filter_window = false;
    
    HighlightedVideo highlighted_video = {0};
    HighlightedChannel highlighted_channel = {0};
    
    char last_search_query[512] = {0};
    QueryType last_query_type = -1;
    
    Query query = {
        .string = "",
        .media = SEARCH_RESULT_TYPE_ANY,
        .sort = SORT_TYPE_RELEVANCE,
        .attr = QUERY_ATTR_REPLACE,
        .type = QUERY_TYPE_USER_INPUT,
        .allow_youtube_shorts = true,
    };
    
    List results = list_init();

    TextureCache texture_cache = NULL;
    
    UpdateFlags update_flags = {false};
  
    UserData user_data;
    if (!user_data_init(&user_data)) {
        fprintf(stderr, "CRITICAL: failed to create UserData object\n");
        return 1;
    }
    
    ClientContext client_context;
    if (!client_context_init(&client_context, MAX_THREADS)) {
        fprintf(stderr, "CRITICAL: failed to initialize ClientContext object\n");
        return 1;
    }

    ThumbnailLoader thumbnail_loader;
    if (!thumbnail_loader_init(&thumbnail_loader, MAX_THREADS)) {
        fprintf(stderr, "CRITICAL: failed to initialize ThumbnailLoader object\n");
        return 1;
    }

    ThreadContext thread_context;
    if (!thread_context_init(&thread_context, MAX_THREADS)) {
        fprintf(stderr, "CRITICAL: failed to initialize ThreadContext object\n");
        return 1;
    }

    SSL_CTX* ssl_ctx =  SSL_CTX_new(TLS_client_method());
    if (ssl_ctx == NULL) {
        fprintf(stderr, "CRITICAL: failed to create SSL_CTX object\n");
        return 1;
    }
    
    while (!WindowShouldClose())
    {
        texture_cache_remove_expried_entries(&texture_cache);
        thumbnail_loader_process_raw_images(&thumbnail_loader, &texture_cache);

        if (update_flags.load_video_metadata) {
            update_flags.load_video_metadata = false;
            queue_video_metadata_task(ssl_ctx, &client_context, &thread_context, &highlighted_video, query);
        }

        if (update_flags.load_query_results) {
            update_flags.load_query_results = false;

            // evade bot detection
            if (strcmp(last_search_query, query.string) == 0) 
                cycle_connection(&client_context.youtube_api_pool);

            last_query_type = query.type;
            strncpy(last_search_query, query.string, sizeof(last_search_query));
            
            queue_search_task(ssl_ctx, &client_context, &thread_context.task_queue, &results, query);
        }

        if (update_flags.load_liked_videos) 
            handle_view_user_data(&results, &client_context.token_mutex, &client_context.continuation_token, user_data.liked_videos, &update_flags.load_liked_videos);

        if (update_flags.load_watch_history) 
            handle_view_user_data(&results, &client_context.token_mutex, &client_context.continuation_token, user_data.watched_videos, &update_flags.load_watch_history);

        if (update_flags.load_subscribed_channels)
            handle_view_user_data(&results, &client_context.token_mutex, &client_context.continuation_token, user_data.subscribed_channels, &update_flags.load_subscribed_channels);

        if (update_flags.load_channel_metadata) {
            update_flags.load_channel_metadata = false;
            last_query_type = query.type;

            ChannelMetadataArgs* targs = malloc(sizeof(ChannelMetadataArgs));
            if (targs) {
                targs->query = query;
                targs->results = &results;
                targs->ssl_ctx = ssl_ctx;
                targs->client_context = &client_context;
                targs->channel = &highlighted_channel;
                targs->thumbnail_loader = &thumbnail_loader;
                targs->subscribed_channels_json = user_data.subscribed_channels;

                if (!thread_task_launch(&thread_context.task_queue, targs, get_channel_metadata)) {
                    fprintf(stderr, "failed to launch get_channel_metadata task\n");
                    free(targs); targs = NULL;
                }
            }
        }

        BeginDrawing();
            const float filter_window_height = 80;
            const float focused_channel_height = 80;
            const float load_more_button_height = 25;
            const int SCROLLBAR_WIDTH = 13;

            ClearBackground(RAYWHITE);

            const Rectangle search_bar_bounds = {
                .x = 0,
                .y = 0,
                .width = 380, 
                .height = 35,
            };

            // search bar elements
            {
                const int button_w = 25;
                const int widget_h = 25;

                const char* trending_button_text = "T";

                const Rectangle trending_button_bounds = {
                    .x = search_bar_bounds.x + ui.padding,
                    .y = search_bar_bounds.y + ui.padding,
                    .width = button_w,
                    .height = widget_h,
                };

                if (GuiButton(trending_button_bounds, trending_button_text)) {
                    update_flags.load_query_results = true;
                    query.attr = QUERY_ATTR_REPLACE;
                    query.type = QUERY_TYPE_VIEW_TRENDING;
                }

                const Rectangle text_bar_bounds = {
                    .x = trending_button_bounds.x + trending_button_bounds.width + ui.padding, 
                    .y = search_bar_bounds.y + ui.padding, 
                    .width = 285, 
                    .height = widget_h, 
                };

                if (GuiTextBox(text_bar_bounds, query.string, sizeof(query.string), text_box_focused)) 
                    text_box_focused = !text_box_focused;

                 const Rectangle search_button_bounds = {
                    .x = text_bar_bounds.x + text_bar_bounds.width + ui.padding, 
                    .y = search_bar_bounds.y + ui.padding, 
                    .width = button_w, 
                    .height = widget_h
                };

                if ((GuiButton(search_button_bounds, "S") || IsKeyPressed(KEY_ENTER)) && 
                   (trim_whitespace(query.string) > 0)) {
                    update_flags.load_query_results = true;
                    query.attr = QUERY_ATTR_REPLACE;
                    query.type = QUERY_TYPE_USER_INPUT;
                }

                const char* filter_button_text = "Fil";

                const Rectangle filter_button_bounds = {
                    .x = search_button_bounds.x + search_button_bounds.width + ui.padding,
                    .y = search_bar_bounds.y + ui.padding,
                    .width = button_w,
                    .height = widget_h,
                };

                if (GuiButton(filter_button_bounds, filter_button_text)) 
                    show_filter_window = !show_filter_window;

                const Rectangle filter_window_bounds = {
                    .x = search_bar_bounds.x + ui.padding,
                    .y = search_bar_bounds.y + search_bar_bounds.height + ui.padding,
                    .width = search_bar_bounds.width - ui.padding,
                    .height = filter_window_height,
                };
            
                if (show_filter_window)
                    draw_filter_window(filter_window_bounds, ui, &query);
            }
        
            // bottom left portion
            {  
                if (!highlighted_channel.cached || (strcmp(highlighted_channel.info.id, highlighted_channel.cached->id) != 0))
                    highlighted_channel.cached = texture_cache_find_entry(&texture_cache, highlighted_channel.info.id);
                else
                    timer_start(&highlighted_channel.cached->timer, CACHED_TEXTURE_LIFETIME);

                const Rectangle highlighted_channel_bounds = {
                    .x = ui.padding,
                    .y = GetScreenHeight() - ui.padding - focused_channel_height,
                    .width = search_bar_bounds.width - ui.padding, 
                    .height = focused_channel_height,
                };

                draw_highlighted_channel(highlighted_channel_bounds, &ui, user_data.subscribed_channels, &highlighted_channel);
            
                const Rectangle load_more_button_bounds = {
                    .x = ui.padding,
                    .y = highlighted_channel_bounds.y - load_more_button_height - ui.padding,
                    .width = search_bar_bounds.width - ui.padding,
                    .height = load_more_button_height,
                };

                if (!valid_string(client_context.continuation_token))
                    GuiSetState(STATE_DISABLED);
            
                if (GuiButton(load_more_button_bounds, "<< LOAD MORE >> ")) {
                    update_flags.load_query_results = true;;
                    query.type = last_query_type;
                    query.attr = QUERY_ATTR_APPEND;
                }

                GuiSetState(STATE_NORMAL);
            }

            const Rectangle result_window_bounds = {
                .x = ui.padding,
                .y = search_bar_bounds.y + search_bar_bounds.height + (show_filter_window ? (filter_window_height + (ui.padding * 2)) : 0),
                .width = search_bar_bounds.width - ui.padding,
                .height = GetScreenHeight() - result_window_bounds.y - focused_channel_height - load_more_button_height - (ui.padding * 3),
            };

            // search window elements
            {
                const int container_height = 80;
           
                const Rectangle content_area = {
                    .x = result_window_bounds.x,
                    .y = result_window_bounds.y,
                    .width = result_window_bounds.width,
                    .height = container_height * results.count,
                };

                const bool vertical_scrollbar_visible = content_area.height > result_window_bounds.height;
                
                GuiScrollPanel(result_window_bounds, NULL, content_area, &result_scrollbar, NULL, true);

                const Rectangle scissor_rect = get_padded_rectangle(1, result_window_bounds);

                BeginScissorMode(scissor_rect.x, scissor_rect.y, scissor_rect.width, scissor_rect.height);

                pthread_mutex_lock(&results.mutex);

                int i = 0;
                float container_y = result_window_bounds.y;

                for (Node* node = results.head; node; node = node->next, i++, container_y += container_height) {
                    const Rectangle container = { 
                        .x = result_window_bounds.x, 
                        .y = container_y + result_scrollbar.y, 
                        .width = result_window_bounds.width - (vertical_scrollbar_visible ? SCROLLBAR_WIDTH: 0),
                        .height = container_height 
                    };

                    SearchResult* search_result = (SearchResult*) node->content;

                    Texture thumbnail = {0};
                    
                    TextureCacheEntry* cached = texture_cache_find_entry(&texture_cache, search_result->id);
                    if (texture_cache_entry_is_ready(cached)) {
                        timer_start(&cached->timer, CACHED_TEXTURE_LIFETIME); // refresh lifetime
                        thumbnail = cached->texture;
                    }

                    if (!CheckCollisionRecs(scissor_rect, container)) 
                        continue;

                    if (valid_string(search_result->thumbnail_path) && !search_result->thumbnail_loaded) {
                        search_result->thumbnail_loaded = true;
                        queue_load_thumbnail(ssl_ctx, &thumbnail_loader, &thread_context.task_queue, search_result->search_result_type, search_result->id, search_result->thumbnail_path);
                    }

                    const bool result_is_highlighted = strcmp(search_result->id, highlighted_video.info.id) == 0;

                    const Color container_color = result_is_highlighted ? 
                                                  BLUE : 
                                                  i % 2 ? WHITE : RAYWHITE;

                    draw_search_result(search_result, thumbnail, container, container_color, ui);

                    if ((CheckCollisionPointRec(GetMousePosition(), container)) && 
                        (CheckCollisionPointRec(GetMousePosition(), scissor_rect)) &&
                        (IsMouseButtonPressed(MOUSE_BUTTON_LEFT))) {
                        query.attr = QUERY_ATTR_REPLACE;

                        strncpy(query.focused_id, search_result->id, sizeof(query.focused_id) - 1);
                        query.focused_id[sizeof(query.focused_id) - 1] = '\0';

                        switch (search_result->search_result_type) {
                            case SEARCH_RESULT_TYPE_LIVE:
                            case SEARCH_RESULT_TYPE_SHORT:
                            case SEARCH_RESULT_TYPE_VIDEO:
                                if (!result_is_highlighted) {
                                    update_flags.load_video_metadata = true;
                                    query.type = QUERY_TYPE_VIEW_VIDEO;
                                    memcpy(&highlighted_video.info, search_result, sizeof(SearchResult));
                                }
                                break;
                            case SEARCH_RESULT_TYPE_PLAYLIST:
                                update_flags.load_query_results = true;
                                query.type = QUERY_TYPE_VIEW_PLAYLIST;
                                break;
                            case SEARCH_RESULT_TYPE_CHANNEL:
                                update_flags.load_channel_metadata = true;
                                query.type = QUERY_TYPE_VIEW_CHANNEL;
                                break;
                            case SEARCH_RESULT_TYPE_ANY:
                            case SEARCH_RESULT_TYPE_UNDF:
                            break;
                        }
                    }
                }

                pthread_mutex_unlock(&results.mutex);

                EndScissorMode(); 
            }

            const float button_bar_height = 25.0f;

            const Rectangle video_management_button_bar = {
                .x = search_bar_bounds.x + search_bar_bounds.width + (ui.padding * 2),
                .y = ui.padding,
                .width = GetScreenWidth() - video_management_button_bar.x - ui.padding,
                .height = button_bar_height, 
            };

            draw_video_management_buttons(video_management_button_bar, &query, &highlighted_video, user_data.liked_videos, &update_flags, &thread_context, &user_data);

            const Rectangle user_data_button_bar = {
                .x = search_bar_bounds.x + search_bar_bounds.width + (ui.padding * 2),
                .y = GetScreenHeight() - button_bar_height - ui.padding,
                .width = GetScreenWidth() - user_data_button_bar.x - ui.padding,
                .height = button_bar_height,
            };

            draw_user_data_buttons(user_data_button_bar, &query, &update_flags);
            
            const Rectangle focused_video_bounds = {
                .x = search_bar_bounds.x + search_bar_bounds.width + (ui.padding * 2),
                .y = video_management_button_bar.y + video_management_button_bar.height + ui.padding,
                .width = GetScreenWidth() - focused_video_bounds.x - ui.padding,
                .height = GetScreenHeight() - focused_video_bounds.y - user_data_button_bar.height - (ui.padding * 2),
            };

            draw_text_scrollable(focused_video_bounds, false, ui, &description_scrollbar, highlighted_video.description);
            
        EndDrawing();
    }

    thread_context_free(&thread_context);     
    thumbnail_loader_free(&thumbnail_loader);
    client_context_free(&client_context);

    UnloadFont(ui.font);
    list_free(&results);
    texture_cache_free(&texture_cache);
    
    user_data_free(&user_data);
    
    if (highlighted_video.description) {
        free(highlighted_video.description); highlighted_video.description = NULL;
    }
    
    if (ssl_ctx) {
        SSL_CTX_free(ssl_ctx); ssl_ctx = NULL;
    }
    
    CloseWindow();
    
    return 0;
}

// stuff to do:
    // able to add videos to created playlist
    // fonts for L.O.T.E.
    // thumbnail frames from video click
    // better create_results_from_json?
    // move ui stuff together
    // update highlighted channel anytime you press a video
    // issue with pressing user's videos button, sometimes channel shows for half second, then dissapears
    // dont assign cached texture every frame