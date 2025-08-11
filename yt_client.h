#ifndef YT_CLIENT_H
#define YT_CLIENT_H

#include "query.h"
#include "buffer.h"
#include "connection.h"
#include "https_utils.h"

#include <stdlib.h>
#include <stdbool.h>
#include <cjson/cJSON.h>
#include <openssl/ssl.h>

#define HTTP_PROTOCOL_VER "1.1"
#define CONNECTION_STATUS "keep-alive"
#define USER_AGENT "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/130.0.0.0 Safari/537.36"

#define CLIENT_NAME "WEB"
#define CLIENT_VER "2.20250730"
#define YT_API_PLAYLIST_BROWSE_ID_PREFIX "VL"    
#define YT_API_TRENDING_BROWSE_ID "FEtrending" 
#define YT_API_CHANNEL_VIDEOS_PARAMS "EgZ2aWRlb3PyBgQKAjoA"  

// payload config
cJSON* configure_base_payload();
cJSON* configure_client_object();
cJSON* configure_post_payload(const Query* query, const char* continuation_token);
bool add_view_trending_videos_payload(cJSON* root);
bool add_view_related_videos_payload(cJSON* root, const char* videoId);
bool add_view_channel_videos_payload(cJSON* root, const char* channel_id);
bool add_continuation_payload(cJSON* root, const char* continuation_token);
bool add_view_playlist_videos_payload(cJSON* root, const char* playlist_id);
bool add_view_user_input_payload(cJSON* root, const char* user_input, const SortType sort_type, const MediaType media_type);

// request config
bool configure_youtube_internal_api_path(char* dest, const size_t dest_size, QueryType query_type, const char* key);
HttpsRequest configure_post_request(const Query query, const char* host, const char* api_key, const char* continuation_token);

#endif