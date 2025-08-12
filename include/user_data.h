#ifndef USER_DATA_H
#define USER_DATA_H

#define ARRAY_NAME "array"
#define ELEMENT_NAME "element"

#define OBJ_ID_PATH           "id" 
#define OBJ_THUMBNAIL_PATH    "thumbnail_path"
#define OBJ_TITLE_PATH        "title"
#define OBJ_AUTHOR_ID_PATH    "authorId"
#define OBJ_SUB_COUNT_PATH    "subscriber_count"
#define OBJ_UPLOAD_DATE_PATH  "date_published"
#define OBJ_VIDEO_COUNT_PATH  "video_count"
#define OBJ_VIEW_COUNT_PATH   "view_count"
#define OBJ_VIDEO_LENGTH_PATH "duration"
#define OBJ_MEDIA_TYPE_PATH   "media_type"

#define LIKED_VIDEOS_FILE  "liked_videos.json"
#define SUBSCRIPTIONS_FILE "subscriptions.json"
#define WATCH_HISTORY_FILE "watch_history.json"

#include "search_result.h"

#include <cjson/cJSON.h>

cJSON* create_user_data_object();
cJSON* init_search_result_json(const SearchResult* result);

void delete_user_data(cJSON* user_data, const char* id);
void add_user_data(cJSON* user_data, const SearchResult* interacted_result);
int  find_user_data_index(const cJSON* user_data, const char* id, const char* id_path);

void parse_user_data(cJSON* user_data, SearchResult* dest);

#endif