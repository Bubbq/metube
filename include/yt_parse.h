#ifndef YT_PARSE_H
#define YT_PARSE_H

#define MEDIUM_THUMBNAIL_VIDEO_RESOLUTION "mqdefault"

#include "query.h"
#include "search_result.h"

#include <stdbool.h>
#include <cjson/cJSON.h>

bool assign_video_thumbnail_path(const char* video_id, char* dest, const size_t dest_size);

bool video_is_youtube_short(cJSON *videoRenderer);
bool video_is_live(cJSON* videoRenderer);

char* get_video_description(cJSON* videoDetails);
void parse_related_video(cJSON* lockupViewModel, SearchResult* related_vid);
void parse_playlist_video(cJSON* playlistVideoRenderer, SearchResult* playlist_vid);
void parse_video(cJSON* videoRenderer, const char* author_id_override, const bool allow_youtube_shorts, SearchResult* video);
void parse_channel_result(cJSON* channelRenderer, SearchResult* channel);
bool parse_highlighted_channel(cJSON* json, SearchResult* channel);
void parse_playlist_result(cJSON *lockupViewModel, SearchResult *playlist);

const char* get_results_list_path(const QueryType search_type, const QueryAttribute search_attr);
const char* get_continuation_token_path(const QueryType search_type, const QueryAttribute search_attr);
void get_continuation_token(cJSON* json, char** dest, const QueryType query_type, const QueryAttribute query_attr);

#endif