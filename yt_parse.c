#include "include/yt_parse.h"

#include "include/utils.h"
#include "include/json_utils.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

bool video_is_youtube_short(cJSON *videoRenderer) 
{
    const char* path = ".navigationEndpoint.commandMetadata.webCommandMetadata.url";
    const cJSON* url = cjson_pointer_get(videoRenderer, path); 
    if (json_string_is_valid(url)) {
        return strstr(url->valuestring, "/shorts");
    }

    return false;
}

bool video_is_live(cJSON* videoRenderer)
{
    const char* path = ".badges[0].metadataBadgeRenderer.label";
    const cJSON* label = cjson_pointer_get(videoRenderer, path);
    if (json_string_is_valid(label)) {
        return (strcmp("LIVE", label->valuestring) == 0);
    }

    return false;
}

void format_view_count(char* dest, const size_t dest_size)
{
    if ((dest == NULL) || (filter_non_numeric_chars(dest, dest_size) <= 0)) return;

    const float view_count = strtof(dest, NULL);

    if      (view_count < 1e3)  snprintf(dest, dest_size, "%.0f  views", view_count);         // 0               - 999
    else if (view_count < 1e4)  snprintf(dest, dest_size, "%.2fk views", (view_count / 1e3)); // 1,000           - 9,999
    else if (view_count < 1e5)  snprintf(dest, dest_size, "%.1fk views", (view_count / 1e3)); // 10,000          - 99,999
    else if (view_count < 1e6)  snprintf(dest, dest_size, "%.0fk views", (view_count / 1e3)); // 100,000         - 999,999
    else if (view_count < 1e7)  snprintf(dest, dest_size, "%.2fM views", (view_count / 1e6)); // 1,000,000       - 9,999,999
    else if (view_count < 1e8)  snprintf(dest, dest_size, "%.1fM views", (view_count / 1e6)); // 10,000,000      - 99,999,999
    else if (view_count < 1e9)  snprintf(dest, dest_size, "%.0fM views", (view_count / 1e6)); // 100,000,000     - 999,999,999
    else if (view_count < 1e10) snprintf(dest, dest_size, "%.2fB views", (view_count / 1e9)); // 1,000,000,000   - 9,999,999,999
    else if (view_count < 1e11) snprintf(dest, dest_size, "%.1fB views", (view_count / 1e9)); // 10,000,000,000  - 99,999,999,999
    else if (view_count < 1e12) snprintf(dest, dest_size, "%.0fB views", (view_count / 1e9)); // 100,000,000,000 - 999,999,999,999
}

bool assign_video_thumbnail_path(const char* video_id, char* dest, const size_t dest_size)
{
    if ((valid_string(video_id) == false) || (dest == NULL)) return false;

    const size_t written = snprintf(dest, dest_size, "/vi/%s/" MEDIUM_THUMBNAIL_VIDEO_RESOLUTION ".jpg", video_id);
    
    return (0 < written) && (written < dest_size);
}

void parse_playlist_video(cJSON* playlistVideoRenderer, SearchResult* playlist_vid)
{
    if ((playlistVideoRenderer == NULL) || (playlist_vid == NULL)) return;

    const char* id_path = ".videoId";

    if (!assign_string_from_path(playlistVideoRenderer, id_path, playlist_vid->id, sizeof(playlist_vid->id))) {
        printf("parse_playlist_video: id assign fail (json path: \"%s\")\n", id_path);
        playlist_vid->media_type = MEDIA_TYPE_UNDF;
        return;
    }

    playlist_vid->media_type = MEDIA_TYPE_VIDEO;

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
            video->media_type = MEDIA_TYPE_UNDF;
            return;
        }

        else video->media_type = MEDIA_TYPE_SHORT;
    }

    const char* id_path = ".videoId";

    if (assign_string_from_path(videoRenderer, id_path, video->id, sizeof(video->id)) == false) {
        printf("parse_video: id assign fail (json path: \"%s\")\n", id_path);
        video->media_type = MEDIA_TYPE_UNDF;
        return;
    }

    video->media_type = MEDIA_TYPE_VIDEO;

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
        video->media_type = MEDIA_TYPE_LIVE;

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

char* get_video_description(cJSON* videoDetails)
{
    const char* desc_path = ".videoDetails.shortDescription";

    const cJSON* description_item = cjson_pointer_get(videoDetails, desc_path);

    return json_string_is_valid(description_item) ? strdup(description_item->valuestring) : NULL;
}

void parse_related_video(cJSON* lockupViewModel, SearchResult* related_vid)
{
    if ((lockupViewModel == NULL) || (related_vid == NULL)) return;

    const char* id_path = ".contentId";

    if (!assign_string_from_path(lockupViewModel, id_path, related_vid->id, sizeof(related_vid->id))) {
        printf("parse_related_video: id assign fail (json path: \"%s\")\n", id_path);
        related_vid->media_type = MEDIA_TYPE_UNDF;
        return;
    }

    related_vid->media_type = MEDIA_TYPE_VIDEO;

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
        channel->media_type = MEDIA_TYPE_UNDF;
        return;
    }

    channel->media_type = MEDIA_TYPE_CHANNEL;

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

    channel->media_type = MEDIA_TYPE_CHANNEL;

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
        playlist->media_type = MEDIA_TYPE_UNDF;
        return;
    }
    
    playlist->media_type = MEDIA_TYPE_PLAYLIST;

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
            if (query_attr == QUERY_ATTR_REPLACE) return ".contents.twoColumnSearchResultsRenderer.primaryContents.sectionListRenderer.contents[0].itemSectionRenderer.contents";
            if (query_attr == QUERTY_ATTR_APPEND) return ".onResponseReceivedCommands[0].appendContinuationItemsAction.continuationItems[0].itemSectionRenderer.contents";
        case QUERY_TYPE_VIEW_RELATED: 
            if (query_attr == QUERY_ATTR_REPLACE) return "contents.twoColumnWatchNextResults.secondaryResults.secondaryResults.results";
            if (query_attr == QUERTY_ATTR_APPEND) return ".onResponseReceivedEndpoints[0].appendContinuationItemsAction.continuationItems";
        case QUERY_TYPE_VIEW_PLAYLIST: 
            if (query_attr == QUERY_ATTR_REPLACE) return ".contents.twoColumnBrowseResultsRenderer.tabs[0].tabRenderer.content.sectionListRenderer.contents[0].itemSectionRenderer.contents[0].playlistVideoListRenderer.contents";
            if (query_attr == QUERTY_ATTR_APPEND) return ".onResponseReceivedActions[0].appendContinuationItemsAction.continuationItems";
        case QUERY_TYPE_VIEW_CHANNEL: 
            if (query_attr == QUERY_ATTR_REPLACE) return ".contents.twoColumnBrowseResultsRenderer.tabs[1].tabRenderer.content.richGridRenderer.contents";
            if (query_attr == QUERTY_ATTR_APPEND) return ".onResponseReceivedActions[0].appendContinuationItemsAction.continuationItems";
        case QUERY_TYPE_VIEW_TRENDING:             return ".contents.twoColumnBrowseResultsRenderer.tabs[0].tabRenderer.content.sectionListRenderer.contents[2].itemSectionRenderer.contents[0].shelfRenderer.content.expandedShelfContentsRenderer.items";
        default:
            fprintf(stderr, "get_results_list_path: QueryType %d is not supported\n", query_type);
            return NULL;
    }
}

const char* get_continuation_token_path(const QueryType search_type, const QueryAttribute search_attr)
{
    switch (search_type) {
        case QUERY_TYPE_USER_INPUT:
            if (search_attr == QUERY_ATTR_REPLACE) return ".contents.twoColumnSearchResultsRenderer.primaryContents.sectionListRenderer.contents[1].continuationItemRenderer.continuationEndpoint.continuationCommand.token";
            if (search_attr == QUERTY_ATTR_APPEND) return ".onResponseReceivedCommands[0].appendContinuationItemsAction.continuationItems[1].continuationItemRenderer.continuationEndpoint.continuationCommand.token";
        case QUERY_TYPE_VIEW_RELATED:
            if (search_attr == QUERY_ATTR_REPLACE) return ".contents.twoColumnWatchNextResults.secondaryResults.secondaryResults.results[-1].continuationItemRenderer.continuationEndpoint.continuationCommand.token";
            if (search_attr == QUERTY_ATTR_APPEND) return ".onResponseReceivedEndpoints[0].appendContinuationItemsAction.continuationItems[-1].continuationItemRenderer.continuationEndpoint.continuationCommand.token";
        case QUERY_TYPE_VIEW_PLAYLIST:
            if (search_attr == QUERY_ATTR_REPLACE) return ".contents.twoColumnBrowseResultsRenderer.tabs[0].tabRenderer.content.sectionListRenderer.contents[0].itemSectionRenderer.contents[0].playlistVideoListRenderer.contents[-1].continuationItemRenderer.continuationEndpoint.commandExecutorCommand.commands[1].continuationCommand.token";
            if (search_attr == QUERTY_ATTR_APPEND) return ".onResponseReceivedActions[0].appendContinuationItemsAction.continuationItems[-1].continuationItemRenderer.continuationEndpoint.continuationCommand.token";
        case QUERY_TYPE_VIEW_CHANNEL:
            if (search_attr == QUERY_ATTR_REPLACE) return ".contents.twoColumnBrowseResultsRenderer.tabs[1].tabRenderer.content.richGridRenderer.contents[-1].continuationItemRenderer.continuationEndpoint.continuationCommand.token";
            if (search_attr == QUERTY_ATTR_APPEND) return ".onResponseReceivedActions[0].appendContinuationItemsAction.continuationItems[-1].continuationItemRenderer.continuationEndpoint.continuationCommand.token";
        default: return NULL;
    }
}

void get_continuation_token(cJSON* json, char* dest, const QueryType query_type, const QueryAttribute query_attr)
{
    if (json == NULL) {
        printf("get_continuation_token: invalid input(s)\n");
        return;
    }

    const char* continuation_path = get_continuation_token_path(query_type, query_attr);
    
    if (dest) {
        free(dest); dest = NULL;
    }

    const cJSON* token_obj = cjson_pointer_get(json, continuation_path);
    if (json_string_is_valid(token_obj) == false) {
        printf("get_continuation_token: 'token_obj' is invalid (path: %s)\n", continuation_path);
        return;
    }

    if ((dest = strdup(token_obj->valuestring)) == NULL) {
        printf("get_continuation_token: strdup failed\n");
    }
}