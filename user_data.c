#include "include/user_data.h"

#include "include/media_type.h"
#include "include/utils.h"
#include "include/json_utils.h"

#include <cjson/cJSON.h>
#include <stdio.h>
#include <string.h>

UserData user_data_init()
{
    UserData user_data = (UserData) {
        .liked_videos        = file_exists(LIKED_VIDEOS_FILE)  ? parse_json_file(LIKED_VIDEOS_FILE)  : create_user_data_object(),
        .watched_videos      = file_exists(WATCH_HISTORY_FILE) ? parse_json_file(WATCH_HISTORY_FILE) : create_user_data_object(),
        .subscribed_channels = file_exists(SUBSCRIPTIONS_FILE) ? parse_json_file(SUBSCRIPTIONS_FILE) : create_user_data_object(),
    };

    return user_data;
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
    cJSON_AddNumberToObject(result_json, OBJ_MEDIA_TYPE_PATH, result->media_type);
    cJSON_AddStringToObject(result_json, OBJ_THUMBNAIL_PATH, result->thumbnail_path);
    
    switch (result->media_type) {
        case MEDIA_TYPE_LIVE:
        case MEDIA_TYPE_SHORT:
        case MEDIA_TYPE_VIDEO:
            cJSON_AddStringToObject(result_json, OBJ_AUTHOR_ID_PATH, result->authorId);
            cJSON_AddStringToObject(result_json, OBJ_VIEW_COUNT_PATH, result->view_count);
            cJSON_AddStringToObject(result_json, OBJ_VIDEO_LENGTH_PATH, result->duration);
            cJSON_AddStringToObject(result_json, OBJ_UPLOAD_DATE_PATH, result->date_published);
            break;
        case MEDIA_TYPE_CHANNEL:  
            cJSON_AddStringToObject(result_json, OBJ_SUB_COUNT_PATH, result->subscriber_count); 
            break;
        case MEDIA_TYPE_PLAYLIST: 
            cJSON_AddStringToObject(result_json, OBJ_VIDEO_COUNT_PATH, result->video_count);
            break;
        default: 
            fprintf(stderr, "init_search_result_json: MediaType %d not supported\n", result->media_type);
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

    assign_number_from_path(user_data, OBJ_MEDIA_TYPE_PATH, (int*) &dest->media_type);
    if (dest->media_type == MEDIA_TYPE_UNDF) 
        return;
    
    assign_string_from_path(user_data, OBJ_ID_PATH, dest->id, sizeof(dest->id));
    assign_string_from_path(user_data, OBJ_TITLE_PATH, dest->title, sizeof(dest->title));
    assign_string_from_path(user_data, OBJ_THUMBNAIL_PATH, dest->thumbnail_path, sizeof(dest->thumbnail_path));

    switch (dest->media_type) {
        case MEDIA_TYPE_LIVE:
        case MEDIA_TYPE_SHORT:
        case MEDIA_TYPE_VIDEO:
            assign_string_from_path(user_data, OBJ_AUTHOR_ID_PATH, dest->authorId, sizeof(dest->authorId));
            assign_string_from_path(user_data, OBJ_VIDEO_LENGTH_PATH, dest->duration, sizeof(dest->duration));
            assign_string_from_path(user_data, OBJ_VIEW_COUNT_PATH, dest->view_count, sizeof(dest->view_count));
            assign_string_from_path(user_data, OBJ_UPLOAD_DATE_PATH, dest->date_published, sizeof(dest->date_published));
            break;
        case MEDIA_TYPE_CHANNEL:
            assign_string_from_path(user_data, OBJ_SUB_COUNT_PATH, dest->subscriber_count, sizeof(dest->subscriber_count));
            break;
        case MEDIA_TYPE_PLAYLIST:
            assign_string_from_path(user_data, OBJ_VIDEO_COUNT_PATH, dest->video_count, sizeof(dest->video_count));
            break;
        case MEDIA_TYPE_ANY:
        case MEDIA_TYPE_UNDF:
            break;
    }
}