#include "include/utils.h"
#include "include/thumbnails.h"
#include "include/thread_utils.h"
#include "innertube/include/innertube.h"

#define RAYGUI_IMPLEMENTATION
#include "include/raygui.h"

#define THREADS 4
#define YOUTUBE_HOST "www.youtube.com"
#define PARSE_CONFIG_PATH "./config/paths.json"

void write_json_to_file(const cJSON* json, const char* filename)
{
    if (!json || !valid_string(filename)) 
        return;

    char* buffer = cJSON_Print(json);
    if (!buffer) {
        fprintf(stderr, "write_json_file: json object is empty\n");
        return; 
    }

    write_string_to_file(filename, buffer);

    free(buffer); buffer = NULL;
}

MediaType result_type_to_media (const SearchResultType type)
{
    switch (type) {
        case SEARCH_RESULT_TYPE_LIVE_VIDEO:
        case SEARCH_RESULT_TYPE_VIDEO:
        case SEARCH_RESULT_TYPE_PLAYLIST_VIDEO:
        case SEARCH_RESULT_TYPE_HIGHLIGHTED_VIDEO: return MEDIA_VIDEO ;
        case SEARCH_RESULT_TYPE_CHANNEL: return MEDIA_CHANNEL ;
        case SEARCH_RESULT_TYPE_PLAYLIST: return MEDIA_PLAYLIST ;
        default:
            return -1 ;
    }
}

bool queue_load_thumbnail(SSL_CTX* ssl_ctx, ThumbnailLoader* loader, ThreadContext * thread_context, SearchResultType search_result_type, const char* id, const char* path)
{
    if ((!ssl_ctx) || 
        (!loader) || 
        (!thread_context) || 
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
    targs->media_type = result_type_to_media(search_result_type);
    strncpy(targs->id, id, sizeof(targs->id));
    strncpy(targs->path, path, sizeof(targs->path));

    return thread_context_add_task(thread_context, targs, free, load_thumbnail) ;
}

// user data management

// #define ARRAY_NAME "array"

// #define OBJ_ID_PATH           "id" 
// #define OBJ_THUMBNAIL_PATH    "thumbnail_path"
// #define OBJ_TITLE_PATH        "title"
// #define OBJ_AUTHOR_ID_PATH    "authorId"
// #define OBJ_SUB_COUNT_PATH    "subscriber_count"
// #define OBJ_UPLOAD_DATE_PATH  "upload_date"
// #define OBJ_VIDEO_COUNT_PATH  "video_count"
// #define OBJ_VIEW_COUNT_PATH   "view_count"
// #define OBJ_VIDEO_LENGTH_PATH "duration"
// #define OBJ_search_result_type_PATH   "search_result_type"

// #define LIKED_VIDEOS_FILE  "liked_videos.json"
// #define SUBSCRIPTIONS_FILE "subscriptions.json"
// #define WATCH_HISTORY_FILE "watch_history.json"

typedef struct
{
    cJSON* liked_videos;
    cJSON* watched_videos;
    cJSON* subscribed_channels;
} UserData;

// cJSON* create_user_data_object()
// {
//     cJSON* root = cJSON_CreateObject();
//     if (root == NULL) {
//         fprintf(stderr, "create_user_data_object: failed to create root\n");
//         return NULL;
//     }

//     cJSON* array = cJSON_CreateArray();
//     if (array == NULL) {
//         fprintf(stderr, "create_user_data_object: failed to create array\n");
//         cJSON_Delete(root); root = NULL;
//         return NULL;
//     }

//     if (cJSON_AddItemToObject(root, ARRAY_NAME, array) == false) {
//         fprintf(stderr, "create_user_data_object: failed to add array to root\n");
//         cJSON_Delete(root); root = NULL;
//     }

//     return root;
// }

// bool user_data_init(UserData* user_data)
// {
//     if (!user_data)
//         return false;

//     user_data->liked_videos        = file_exists(LIKED_VIDEOS_FILE)   ? parse_json_file(LIKED_VIDEOS_FILE)   : create_user_data_object();
//     user_data->watched_videos      = file_exists(WATCH_HISTORY_FILE)  ? parse_json_file(WATCH_HISTORY_FILE)  : create_user_data_object();
//     user_data->subscribed_channels = file_exists(SUBSCRIPTIONS_FILE)  ? parse_json_file(SUBSCRIPTIONS_FILE)  : create_user_data_object();

//     return (user_data->liked_videos || user_data->watched_videos || user_data->subscribed_channels);
// }

// void user_data_free(UserData* user_data)
// {
//     if (user_data == NULL) return;
    
//     if (user_data->watched_videos) {
//         write_json_to_file(user_data->watched_videos, WATCH_HISTORY_FILE);
//         cJSON_Delete(user_data->watched_videos); user_data->watched_videos = NULL;
//     }

//     if (user_data->subscribed_channels) {
//         write_json_to_file(user_data->subscribed_channels, SUBSCRIPTIONS_FILE);
//         cJSON_Delete(user_data->subscribed_channels); user_data->subscribed_channels = NULL;
//     }

//     if (user_data->liked_videos) {
//         write_json_to_file(user_data->liked_videos, LIKED_VIDEOS_FILE);
//         cJSON_Delete(user_data->liked_videos); user_data->liked_videos = NULL;
//     }
// }

// bool user_data_is_ready(UserData* user_data)
// {
//     return (user_data) && 
//            (cJSON_IsObject(user_data->liked_videos)) && 
//            (cJSON_IsObject(user_data->watched_videos)) && 
//            (cJSON_IsObject(user_data->subscribed_channels));
// }

// int find_user_data_index(const cJSON* user_data, const char* id, const char* id_path)
// {
//     if ((cJSON_IsArray(user_data) == false) || (valid_string(id) == false) || (valid_string(id_path) == false)) return -1;

//     int i = 0;
    
//     cJSON* item;
//     cJSON_ArrayForEach(item, user_data) {
//         const cJSON* id_item = cjson_pointer_get(item, id_path);
//         if (json_string_is_valid(id_item) && (strcmp(id_item->valuestring, id) == 0)) {
//             return i;
//         }

//         i++;
//     }

//     return -1;
// }

// cJSON* init_search_result_json(const YoutubeSearchResult * result)
// {
//     if (result == NULL) return NULL;
    
//     cJSON* result_json = cJSON_CreateObject();
//     if (result_json == NULL) {
//         fprintf(stderr, "init_search_result_json: failed to create cJSON* object\n");
//         return NULL;
//     }

//     cJSON_AddStringToObject(result_json, OBJ_ID_PATH, result->id);
//     cJSON_AddStringToObject(result_json, OBJ_TITLE_PATH, result->title);
//     cJSON_AddNumberToObject(result_json, OBJ_search_result_type_PATH, result->type);
//     cJSON_AddStringToObject(result_json, OBJ_THUMBNAIL_PATH, result->thumbnail_path);
    
//     switch (result->type) {
//         case SEARCH_RESULT_TYPE_LIVE:
//         case SEARCH_RESULT_TYPE_VIDEO: 
//             cJSON_AddStringToObject(result_json, OBJ_AUTHOR_ID_PATH, result->author_id);
//             cJSON_AddNumberToObject(result_json, OBJ_VIEW_COUNT_PATH, result->view_count);
//             cJSON_AddNumberToObject(result_json, OBJ_VIDEO_LENGTH_PATH, result->duration);
//             cJSON_AddStringToObject(result_json, OBJ_UPLOAD_DATE_PATH, result->upload_date);
//             break;
//         case SEARCH_RESULT_TYPE_CHANNEL: 
//             cJSON_AddNumberToObject(result_json, OBJ_SUB_COUNT_PATH, result->subscriber_count); 
//             break;
//         case SEARCH_RESULT_TYPE_PLAYLIST: 
//             cJSON_AddNumberToObject(result_json, OBJ_VIDEO_COUNT_PATH, result->playlist_length);
//             break;
//         default: 
//             fprintf(stderr, "init_search_result_json: SearchResultType %d not supported\n", result->type);
//             return NULL;
//     }

//     return result_json;
// }

// void delete_user_data(cJSON* user_data, const char* id)
// {
//     if ((user_data == NULL) || (valid_string(id) == false)) return;

//     cJSON* array = cjson_pointer_get(user_data, ARRAY_NAME);
//     if (cJSON_IsArray(array) == false) {
//         fprintf(stderr, "delete_user_data: invalid array parsed\n");
//         return;
//     }

//     const int i = find_user_data_index(array, id, OBJ_ID_PATH);
//     if (i >= 0) {
//         cJSON* delete = cJSON_DetachItemFromArray(array, i);
//         cJSON_Delete(delete); delete = NULL;
//     }
// }

// void add_user_data(cJSON* user_data, const YoutubeSearchResult * interacted_result)
// {
//     if (!user_data || !interacted_result)
//         return;

//     cJSON* array = cjson_pointer_get(user_data, ARRAY_NAME);
//     if (cJSON_IsArray(array) == false) {
//         fprintf(stderr, "add_user_data: invalid array parsed\n");
//         return;
//     }

//     cJSON* add = NULL;

//     const char* id = interacted_result->id;
//     const int i = find_user_data_index(array, id, OBJ_ID_PATH);

//     if (i >= 0) 
//         add = cJSON_DetachItemFromArray(array, i);

//     else 
//         add = init_search_result_json(interacted_result);

//     if (add)
//         cJSON_InsertItemInArray(array, 0, add);
// }

// bool is_subbed_to_channel(cJSON* subscribed_channels_json, const char* id)
// {
//     if ((subscribed_channels_json == NULL) || (id == NULL)) return false;

//     cJSON* subbed_channels = cjson_pointer_get(subscribed_channels_json, ARRAY_NAME);
    
//     if (cJSON_IsArray(subbed_channels) == false) return false;

//     const int found = find_user_data_index(subbed_channels, id, OBJ_ID_PATH);

//     return (found >= 0);
// }

// void parse_user_data(cJSON* user_data, YoutubeSearchResult * dest) 
// {
//     if ((user_data == NULL) || (dest == NULL)) return;

//     assign_number_from_path(user_data, OBJ_search_result_type_PATH, (int*) &dest->type);
//     if (dest->type == SEARCH_RESULT_TYPE_UNDF) 
//         return;

//     assign_string_from_path(user_data, OBJ_ID_PATH, dest->id, sizeof(dest->id));
//     assign_string_from_path(user_data, OBJ_TITLE_PATH, dest->title, sizeof(dest->title));
//     assign_string_from_path(user_data, OBJ_THUMBNAIL_PATH, dest->thumbnail_path, sizeof(dest->thumbnail_path));

//     switch (dest->type) {
//         case SEARCH_RESULT_TYPE_LIVE:
//         case SEARCH_RESULT_TYPE_VIDEO: 
//             assign_string_from_path(user_data, OBJ_AUTHOR_ID_PATH, dest->author_id, sizeof(dest->author_id));
//             assign_string_from_path(user_data, OBJ_VIDEO_LENGTH_PATH, dest->duration, sizeof(dest->duration));
//             assign_string_from_path(user_data, OBJ_VIEW_COUNT_PATH, dest->view_count, sizeof(dest->view_count));
//             assign_string_from_path(user_data, OBJ_UPLOAD_DATE_PATH, dest->upload_date, sizeof(dest->upload_date));
//             break;
//         case SEARCH_RESULT_TYPE_CHANNEL: 
//             assign_string_from_path(user_data, OBJ_SUB_COUNT_PATH, dest->subscriber_count, sizeof(dest->subscriber_count));
//             break;
//         case SEARCH_RESULT_TYPE_PLAYLIST: 
//             assign_string_from_path(user_data, OBJ_VIDEO_COUNT_PATH, dest->video_count, sizeof(dest->video_count));
//             break;
//         case SEARCH_RESULT_TYPE_ANY:
//         case SEARCH_RESULT_TYPE_UNDF:
//             break;
//     }
// }

// void load_user_data(LinkedList* results, cJSON* user_data)
// {
//     if ((results == NULL) || (user_data == NULL)) return;

//     cJSON* array = cjson_pointer_get(user_data, ARRAY_NAME);
//     if (cJSON_IsArray(array) == false) {
//         fprintf(stderr, "load_user_data: invalid array parsed\n");
//         return;
//     }

//     pthread_mutex_lock(&results->mutex);
    
//     const int old_size = results->count;

//     cJSON* item;
//     cJSON_ArrayForEach(item, array) {
//         SearchResult * search_result = youtube_search_result_init() ;
//         if ( !search_result)
//             return ;

//         Node* node = node_init(search_result, sizeof(SearchResult), free, NULL) ;
//         if (node) {
//             SearchResult * dest = (SearchResult*) node->data;
//             if (dest) {
//                 parse_user_data(item, dest);
                
//                 if (dest->type != SEARCH_RESULT_TYPE_UNDF) 
//                     linked_list_append(results, node);
//                 else 
//                     node_free(node);
//             }
//         }
//     }

//     for (int i = 0; i < old_size; i++) {
//         node_free(linked_list_dequeue(results));
//     }

//     pthread_mutex_unlock(&results->mutex);
// }

// query operations

typedef struct
{
    YoutubeSearchResult info;
    char* description;
} HighlightedVideo;

typedef struct
{
    YoutubeSearchResult info;
    TextureCacheEntry* cached;
    bool is_subscribed;
} HighlightedChannel;

typedef struct
{
    char video_id[64];
    bool* playing_video;
} PlayVideoArgs;

// bool configure_watch_url(const char* video_id, char* dest, const size_t dest_size)
// {
//     if (!valid_string(video_id) || !dest)
//         return false;

//     const int written = snprintf(dest, dest_size, "mpv https://www.youtube.com/watch?v=%s", video_id);
    
//     return (0 < written) && (written < dest_size);
// }

// void* play_video(void * args)
// {
//     PlayVideoArgs* targs = (PlayVideoArgs*) args;

//     if (!targs || !valid_string(targs->video_id) || !targs->playing_video)
//         return NULL;

//     char command[512] = {0};
    
//     if (!configure_watch_url(targs->video_id, command, sizeof(command)))
//         return false;

//     (*targs->playing_video) = true;

//     system(command);

//     (*targs->playing_video) = false;
    
//     return NULL;
// }

typedef struct
{
    bool is_playing_video;
    bool is_task_set;
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

#define MIN_BUTTON_WIDTH 10

typedef struct
{
    Font font;
    Color text_color;
    int font_size;
    int padding;
    int spacing;
    bool word_wrap;
} Ui;

Rectangle get_padded_rectangle(const float padding, const Rectangle rect)
{
    return (Rectangle) { rect.x + padding, rect.y + padding, rect.width - (padding * 2), rect.height - (padding * 2) };
}

void layout_dynamic_bar(const Rectangle container, const float padding, const float min_width, Rectangle* recs, const size_t nrecs)
{
    if (!recs || (nrecs == 0))
        return;

    // padd the rectangle
    const Rectangle padded_container = get_padded_rectangle(padding, container);

    // find the width availible for the widgets
    const float availible_width = padded_container.width - (padding * (nrecs - 1));

    // get dimensions of the widget
    const float widget_width = fmaxf(availible_width / nrecs, min_width);
    const float widget_height = padded_container.height;
    
    float widget_x = padded_container.x;
    const float widget_y = padded_container.y;
    
    for (size_t i = 0; i < nrecs; i++, widget_x += widget_width + padding) {
        // assign the dimensions of the rectangle
        recs[i] = (Rectangle) {
            .x = widget_x,
            .y = widget_y,
            .width = widget_width,
            .height = widget_height
        };
    }
}

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

void draw_thumbnail_subtext(const Rectangle container, Ui ui, const Color text_color, const int font_size, const char * text)
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
    
    static size_t filtered_type ;

    const SortBy filterable_sort_types [] = {
        SORT_BY_RELEVANCE,
        SORT_BY_UPLOAD_DATE,
        SORT_BY_VIEW_COUNT,
        SORT_BY_RATING,
    };          

    const size_t nsorts = sizeof(filterable_sort_types) / sizeof(filterable_sort_types[0]);

    const Rectangle sort_type_bounds = {
        .x = container.x,
        .y = container.y,
        .width = container.width,
        .height = container.height / 3.0f,
    };

    const char* sort_text = sort_to_text(query->sort);
    if (!sort_text) {
        fprintf(stderr, "draw_filter_window: SortType %d is invalid\n", query->sort);
        return;
    }

    if (draw_toggle_filter(sort_type_bounds, ui, "ORDER", sort_text)) {
        filtered_type = bound_index_to_array((filtered_type + 1), nsorts) ;
        query->sort = filterable_sort_types[filtered_type] ;
    }

    static size_t current_result ;

    const FilterBy filterable_search_result_types [] = {
        FILTER_BY_ANY,
        FILTER_BY_VIDEO,
        FILTER_BY_CHANNEL,
        FILTER_BY_PLAYLIST,
        FILTER_BY_VIDEO,
    };

    const size_t nmedias = sizeof(filterable_search_result_types) / sizeof(filterable_search_result_types[0]);

    const Rectangle search_result_type_bounds = {
        .x = container.x,
        .y = sort_type_bounds.y + sort_type_bounds.height,
        .width = container.width,
        .height = container.height / 3.0f,
    };

    const char* media_text = filter_to_text(query->filter);
    if (!media_text) {
        fprintf(stderr, "draw_filter_window: SearchResultType %d is invalid\n", query->filter);
        return;
    }

    if (draw_toggle_filter(search_result_type_bounds, ui, "TYPE", media_text)) {
        current_result = bound_index_to_array((current_result + 1), nmedias) ;
        query->filter = filterable_search_result_types[current_result] ;
    }
}

void format_view_count (const float view_count, char * dest, const size_t dest_size)
{
    if ( !dest) 
        return ;

    if      (view_count < 1e3)  snprintf(dest, dest_size, "%.0f  views", view_count) ;         // 0               - 999
    else if (view_count < 1e4)  snprintf(dest, dest_size, "%.2fk views", (view_count / 1e3)) ; // 1,000           - 9,999
    else if (view_count < 1e5)  snprintf(dest, dest_size, "%.1fk views", (view_count / 1e3)) ; // 10,000          - 99,999
    else if (view_count < 1e6)  snprintf(dest, dest_size, "%.0fk views", (view_count / 1e3)) ; // 100,000         - 999,999
    else if (view_count < 1e7)  snprintf(dest, dest_size, "%.2fM views", (view_count / 1e6)) ; // 1,000,000       - 9,999,999
    else if (view_count < 1e8)  snprintf(dest, dest_size, "%.1fM views", (view_count / 1e6)) ; // 10,000,000      - 99,999,999
    else if (view_count < 1e9)  snprintf(dest, dest_size, "%.0fM views", (view_count / 1e6)) ; // 100,000,000     - 999,999,999
    else if (view_count < 1e10) snprintf(dest, dest_size, "%.2fB views", (view_count / 1e9)) ; // 1,000,000,000   - 9,999,999,999
    else if (view_count < 1e11) snprintf(dest, dest_size, "%.1fB views", (view_count / 1e9)) ; // 10,000,000,000  - 99,999,999,999
    else if (view_count < 1e12) snprintf(dest, dest_size, "%.0fB views", (view_count / 1e9)) ; // 100,000,000,000 - 999,999,999,999
}

void format_video_duration (const int duration_s, char * dest, const size_t sizeof_dest)
{   
    int hours = duration_s / 3600 ; 
    int minutes = (duration_s - (hours * 3600)) / 60 ;
    int seconds = duration_s % 60 ;

    if (hours > 0)
        snprintf(dest, sizeof_dest, "%d:%d:%d", hours, minutes, seconds) ;

    else 
        snprintf(dest, sizeof_dest, "%d:%d", minutes, seconds) ;
}

void draw_video_card (const YoutubeSearchResult * video)
{
    // set the thumbnail area for the video

    // set the title area for the video

    // set the subtext area 

    // format the view count

    // format the video duration

    // draw everything
}

void draw_search_result(YoutubeSearchResult * search_result, const Texture thumbnail, const Rectangle container, const Color background_color, const Ui ui)
{
    DrawRectangleRec(container, background_color) ;

    const int font_size = 12 ;

    const float thumbnail_width = media_type_to_dimensions(MEDIA_VIDEO).width ;

    const Rectangle thumbnail_area = { 
        .x = container.x, 
        .y = container.y, 
        .width = thumbnail_width, 
        .height = container.height 
    } ;

    const Rectangle title_area = {
        .x = thumbnail_area.x + thumbnail_area.width,
        .y = thumbnail_area.y,
        .width = container.width - thumbnail_area.width,
        .height = thumbnail_area.height * 0.70f
    } ;

    DrawTextureEx(thumbnail, (Vector2){thumbnail_area.x, thumbnail_area.y}, 0.0f, 1.0f, WHITE) ;

    DrawTextBoxed(search_result->title, get_padded_rectangle(ui.padding, title_area), ui, font_size, BLACK) ;                            

    const Rectangle subtext_area = {
        .x = thumbnail_area.x + thumbnail_area.width,
        .y = title_area.y + title_area.height,
        .width = title_area.width,
        .height = container.height - title_area.height,
    } ;

    switch (search_result->type) {
        case SEARCH_RESULT_TYPE_VIDEO:
        case SEARCH_RESULT_TYPE_PLAYLIST_VIDEO: {
            const Vector2 upload_date_pos = {
                .x = subtext_area.x + ui.padding,
                .y = subtext_area.y + ui.padding,
            } ;
            
            DrawTextEx(ui.font, search_result->upload_date, upload_date_pos, font_size, ui.spacing, BLACK) ;
            
            char view_count_text[16] = {0} ;

            format_view_count(search_result->view_count, view_count_text, sizeof(view_count_text)) ;

            const Vector2 view_count_pos = {
                .x = subtext_area.x + subtext_area.width - MeasureTextEx(ui.font, view_count_text, font_size, ui.spacing).x - ui.padding,
                .y = subtext_area.y + ui.padding,
            };

            char duration_text[16] = {0} ;

            format_video_duration(search_result->duration, duration_text, sizeof(duration_text)) ;

            DrawTextEx(ui.font, view_count_text, view_count_pos, font_size, ui.spacing, BLACK);
            
            draw_thumbnail_subtext(thumbnail_area, ui, RAYWHITE, font_size, duration_text);
            break;
        }
        case SEARCH_RESULT_TYPE_LIVE_VIDEO: {
            // const Vector2 view_count_pos = {
            //     .x = subtext_area.x + ui.padding,
            //     .y = subtext_area.y + ui.padding,
            // };

            // DrawTextEx(ui.font, TextFormat("%d watching", search_result->view_count), view_count_pos, font_size, ui.spacing, BLACK);
            
            // DrawTextureEx(thumbnail, (Vector2){thumbnail_area.x, thumbnail_area.y}, 0.0f, 1.0f, WHITE);
            // draw_thumbnail_subtext(thumbnail_area, ui, RAYWHITE, 12, "LIVE");
            break;
        }
        case SEARCH_RESULT_TYPE_CHANNEL: {
            // DrawTextBoxed(search_result->subscriber_count, get_padded_rectangle(ui.padding, subtext_area), ui, 12, BLACK);
            
            // const float x_padding = thumbnail.width / 2.0f;
            // const float y_padding = (container.height - thumbnail.height) / 2.0f;
            
            draw_thumbnail_subtext(thumbnail_area, ui, RAYWHITE, 12, "Channel");
            break;
        }
        case SEARCH_RESULT_TYPE_PLAYLIST: {
            // DrawTextureEx(thumbnail, (Vector2){thumbnail_area.x, thumbnail_area.y}, 0.0f, 1.0f, WHITE);
            // draw_thumbnail_subtext(thumbnail_area, ui, RAYWHITE, 12, search_result->video_count);
            break;
        }
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

// void draw_highlighted_channel(const Rectangle container, const Ui* ui, cJSON* subscribed_channels_json, HighlightedChannel* highlighted_channel)
// {
//     DrawRectangleLinesEx(container, 1, GRAY);
    
//     const float font_size = 17;
//     const float title_size = 25;

//     YoutubeSearchResult * data = &highlighted_channel->info ;

//     if ((ui == NULL) || (highlighted_channel == NULL) || (highlighted_channel->cached == NULL) || ( !valid_string(data->id))) 
//         return;

//     const float thumbnail_w = search_result_type_to_thumbnail_width(SEARCH_RESULT_TYPE_CHANNEL);
    
//     if (texture_cache_entry_is_ready(highlighted_channel->cached)) 
//         DrawTextureEx(highlighted_channel->cached->texture, (Vector2){container.x + ui->padding, container.y + ui->padding}, 0.0f, 1.0f, RAYWHITE);

//     const Vector2 title_pos = {
//         .x = container.x + thumbnail_w + (ui->padding * 2),
//         .y = container.y + (container.height / 3.0f) - (title_size / 2.0f),
//     };

//     DrawTextEx(ui->font, data->title, title_pos, title_size, ui->spacing, BLACK);
    
//     const Vector2 sub_count_pos = {
//         .x = container.x + thumbnail_w + (ui->padding * 2),
//         .y = container.y + container.height - MeasureTextEx(ui->font, data->subscriber_count, font_size, ui->spacing).y - ui->padding,
//     };

//     DrawTextEx(ui->font, data->subscriber_count, sub_count_pos, font_size, ui->spacing, BLACK);

//     const float button_width = 75;
//     const float widget_height = 25;
//     const Rectangle subscribe_button_bounds = {
//         .x = container.x + container.width - button_width - ui->padding,
//         .y = container.y + container.height - widget_height - ui->padding,
//         .width = button_width,
//         .height = 25,
//     };

//     const char* button_text = highlighted_channel->is_subscribed ? "Unsubscribe" : "Subscribe";

//     if (GuiButton(subscribe_button_bounds, button_text)) {
//         if (highlighted_channel->is_subscribed)
//             delete_user_data(subscribed_channels_json, data->id);
//         else
//             add_user_data(subscribed_channels_json, &highlighted_channel->info);
        
//         highlighted_channel->is_subscribed = !highlighted_channel->is_subscribed;
//     }
// }

void draw_video_management_buttons(const Rectangle container, Query* query, const YoutubeSearchResult * highlighted_video, cJSON * liked_video_data, UpdateFlags* update_flags, ThreadContext* thread_context, UserData* user_data)
{
    const int padding = 5;
    const size_t nbuttons = 4;
    const bool is_button_active = valid_string(highlighted_video->id);

    Rectangle button_areas[nbuttons];
    layout_dynamic_bar(container, padding, MIN_BUTTON_WIDTH, button_areas, nbuttons);

    if ( !is_button_active)
        GuiSetState(STATE_DISABLED) ;

    // TODO: launch task in update section in main loop render
    if (GuiButton(button_areas[0], "Play Video") && is_button_active && !update_flags->is_playing_video) {
        // query->action = QUERY_ACTION_PLAY_VIDEO ;
        // update_flags->is_task_set = update_flags->is_playing_video = true ;
    } 

    if (GuiButton(button_areas[1], "Like") && is_button_active) 
        ;
        // add_user_data(liked_video_data, &selected_video->info);

    if (GuiButton(button_areas[2], "Related Videos") && is_button_active) {
        // update_flags->is_task_set = true;
        // query->action = QUERY_ACTION_VIEW_RELATED;
        // query->type = QUERY_TYPE_REPLACE;
    }
    
    if (GuiButton(button_areas[3], "View Channel") && is_button_active) {
        // update_flags->is_task_set = true;
        // query->attr = QUERY_ATTR_REPLACE;
        // query->type = QUERY_TYPE_VIEW_CHANNEL;

        // strncpy(query->focused_id, data->author_id, sizeof(query->focused_id) - 1);
        // query->focused_id[sizeof(query->focused_id) - 1] = '\0';
    }

    GuiSetState(STATE_NORMAL) ;
}

void draw_user_data_buttons(const Rectangle container, Query* query, UpdateFlags* update_flags)
{
    if (!query || !update_flags)
        return;

    const int padding = 5;
    const size_t nbuttons = 3;

    Rectangle button_areas[nbuttons];
    layout_dynamic_bar(container, padding, MIN_BUTTON_WIDTH, button_areas, nbuttons);

    if (GuiButton(button_areas[0], "Subscribed Channels")) {
        update_flags->is_task_set = true;
        query->action = QUERY_ACTION_VIEW_SUBSCRIPTIONS ;
        query->type = QUERY_TYPE_REPLACE ;
    }

    if (GuiButton(button_areas[1], "Liked Videos")) {
        update_flags->is_task_set = true;
        query->type = QUERY_TYPE_REPLACE ;
        query->action = QUERY_ACTION_VIEW_LIKES ;
    }
    
    if (GuiButton(button_areas[2], "Watch History")) {
        update_flags->is_task_set = true;
        query->type = QUERY_TYPE_REPLACE ;
        query->action = QUERY_ACTION_VIEW_HISTORY ;
    }
}

void handle_view_user_data(LinkedList* results, pthread_mutex_t* token_mutex, char** continuation_token, cJSON* user_data)
{
    if (!results || !token_mutex || !continuation_token || !user_data)
        return;

    pthread_mutex_lock(token_mutex);

    if ((*continuation_token)) {
        free((*continuation_token)); (*continuation_token) = NULL;
    }

    pthread_mutex_unlock(token_mutex);

    // load_user_data(results, user_data);
}

// cJSON* query_type_to_user_data(const UserData* user_data, const QueryType query_type)
// {
//     if (!user_data)
//         return NULL;

//     switch (query_type) {
//         case QUERY_TYPE_VIEW_LIKED_VIDEOS:
//             return user_data->liked_videos;
//         case QUERY_TYPE_VIEW_WATCH_HISTORY:
//             return user_data->watched_videos;
//         case QUERY_TYPE_VIEW_SUBSCRIBED_CHANNELS:
//             return user_data->subscribed_channels;
//         default:
//             fprintf(stderr, "query_type_to_user_data: QueryType %d is invalid\n", query_type);
//             return NULL;
//     }
// }

// TODO : seperate user data logic from main
// TODO : add functionality to video management buttons (view related, play video, view channel)
// TODO : draw highlighted channel 
// TODO : display results properly
// TODO : replace CJSON with another parser that doesnt duplicate memory when parsing, or create my own
// TODO : clean innertube
    // pass query to get video metadata
    // combine video/channel metadata into same thing?

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
    
    QueryAction last_action = -1;
    
    Query query = {
        .string = "",
        .action = QUERY_ACTION_SEARCH,
        .type = QUERY_TYPE_REPLACE,
        .filter = FILTER_BY_ANY,
        .sort = SORT_BY_RELEVANCE,
    };

    YoutubeSearchResult * highlighted_video = youtube_search_result_init() ;
    YoutubeSearchResult * highlighted_channel = youtube_search_result_init() ;    
    
    LinkedList results = linked_list_init();

    TextureCache texture_cache = NULL;
    
    UpdateFlags update_flags = {false};

    UserData user_data = {0};
    // if (!user_data_init(&user_data)) {
    //     fprintf(stderr, "CRITICAL: failed to create UserData object\n");
    //     return 1;
    // }
    
    YoutubeParseContext parse_context ;
    if ( !youtube_parse_context_init(&parse_context, PARSE_CONFIG_PATH)) {
        fprintf(stderr, "CRITICAL: failed to create YoutubeParseContext object\n") ;
        return 1 ;
    }

    ClientContext client_context ;
    if ( !client_context_init(&client_context, THREADS, YOUTUBE_HOST, INTERNAL_YOUTUBE_API_KEY)) {
        fprintf(stderr, "CRITICAL: failed to initialize ClientContext object\n") ;
        return 1 ;
    }

    ThumbnailLoader thumbnail_loader ;
    if ( !thumbnail_loader_init(&thumbnail_loader, THREADS)) {
        fprintf(stderr, "CRITICAL: failed to initialize ThumbnailLoader object\n") ;
        return 1 ;
    }

    ThreadContext thread_context ;
    if ( !thread_context_init(&thread_context, THREADS)) {
        fprintf(stderr, "CRITICAL: failed to initialize ThreadContext object\n") ;
        return 1 ;
    }

    SSL_CTX * ssl_ctx =  SSL_CTX_new(TLS_client_method()) ;
    if ( !ssl_ctx) {
        fprintf(stderr, "CRITICAL: failed to create SSL_CTX object\n") ;
        return 1 ;
    }

    while ( !WindowShouldClose())
    {
        texture_cache_remove_expried_entries(&texture_cache);

        thumbnail_loader_process_raw_images(&thumbnail_loader, &texture_cache);

        if (update_flags.is_task_set) {
            update_flags.is_task_set = false ;

            switch (query.action) {
                case QUERY_ACTION_SEARCH:
                case QUERY_ACTION_VIEW_RELATED:
                case QUERY_ACTION_PLAYLIST_PRESS: {
                    last_action = query.action ;
                    SearchThreadArgs * targs = create_search_thread_args(query, ssl_ctx, &results, &client_context, &parse_context) ;
                    thread_context_add_task(&thread_context, targs, free, get_results_from_query) ;
                    query.string[0] = '\0' ;
                    break ;
                }
                case QUERY_ACTION_VIDEO_PRESS: {
                    VideoMetadataArgs * args = create_video_metadata_args(ssl_ctx, query.focused_id, &client_context, highlighted_video, &parse_context) ;
                    thread_context_add_task(&thread_context, args, free, get_video_metadata) ;
                    break ;
                }
                case QUERY_ACTION_CHANNEL_PRESS: {
                    last_action = query.action ;
                    SearchThreadArgs * sargs = create_search_thread_args(query, ssl_ctx, &results, &client_context, &parse_context) ;
                    ChannelMetadataArgs * targs = create_channel_metadata_args(sargs, highlighted_channel) ;
                    thread_context_add_task(&thread_context, targs, (void*) free_channel_metadata_args, get_channel_metadata) ;
                    break ;
                }
                case QUERY_ACTION_PLAY_VIDEO:
                case QUERY_ACTION_VIEW_LIKES:
                case QUERY_ACTION_VIEW_HISTORY:
                case QUERY_ACTION_VIEW_SUBSCRIPTIONS: 
                default:
                    fprintf(stderr, "QueryType %d is invalid\n", query.type);
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
                const Rectangle text_bar_bounds = {
                    .x = search_bar_bounds.x + ui.padding, 
                    .y = search_bar_bounds.y + ui.padding, 
                    .width = 285, 
                    .height = search_bar_bounds.height - (ui.padding * 2) 
                };

                if (GuiTextBox(text_bar_bounds, query.string, sizeof(query.string), text_box_focused)) 
                    text_box_focused = !text_box_focused;
                
                const Rectangle button_container = {
                    .x = text_bar_bounds.x + text_bar_bounds.width,
                    .y = search_bar_bounds.y,
                    .width = search_bar_bounds.width - text_bar_bounds.width,
                    .height = search_bar_bounds.height
                };

                const size_t nbuttons = 2;
                Rectangle button_areas[nbuttons];
                layout_dynamic_bar(button_container, ui.padding, MIN_BUTTON_WIDTH, button_areas, nbuttons);

                if ((GuiButton(button_areas[0], "S") || IsKeyPressed(KEY_ENTER)) && (trim_whitespace(query.string) > 0)) {
                    update_flags.is_task_set = true;
                    query.action = QUERY_ACTION_SEARCH;
                    query.type = QUERY_TYPE_REPLACE;
                }

                if (GuiButton(button_areas[1], "Fil")) 
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
                const Rectangle highlighted_channel_bounds = {
                    .x = ui.padding,
                    .y = GetScreenHeight() - ui.padding - focused_channel_height,
                    .width = search_bar_bounds.width - ui.padding, 
                    .height = focused_channel_height,
                };

                const Rectangle load_more_button_bounds = {
                    .x = ui.padding,
                    .y = highlighted_channel_bounds.y - load_more_button_height - ui.padding,
                    .width = search_bar_bounds.width - ui.padding,
                    .height = load_more_button_height,
                };

                if (!valid_string(client_context.continuation_token))
                    GuiSetState(STATE_DISABLED);
            
                if (GuiButton(load_more_button_bounds, "<< LOAD MORE >> ")) {
                    update_flags.is_task_set = true;
                    query.action = last_action ;
                    query.type = QUERY_TYPE_APPEND ;
                }

                GuiSetState(STATE_NORMAL);
            }

            // search window elements
            {
                const Rectangle result_window_bounds = {
                    .x = ui.padding,
                    .y = search_bar_bounds.y + search_bar_bounds.height + (show_filter_window ? (filter_window_height + (ui.padding * 2)) : 0),
                    .width = search_bar_bounds.width - ui.padding,
                    .height = GetScreenHeight() - result_window_bounds.y - focused_channel_height - load_more_button_height - (ui.padding * 3),
                };

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
                        .y = (result_window_bounds.y + (container_height * i)) + result_scrollbar.y, 
                        .width = result_window_bounds.width - (vertical_scrollbar_visible ? SCROLLBAR_WIDTH: 0),
                        .height = container_height 
                    };

                    YoutubeSearchResult * search_result = (YoutubeSearchResult*) node->data;

                    Texture thumbnail = {0};
                    TextureCacheEntry* entry = texture_cache_find_entry(&texture_cache, search_result->id);
                    if (texture_cache_entry_is_ready(entry)) {
                        timer_start(&entry->timer, CACHED_TEXTURE_LIFETIME);
                        thumbnail = entry->texture;
                        if ( !search_result->is_thumbnail_loaded)
                            search_result->is_thumbnail_loaded = true;
                    }

                    if (!CheckCollisionRecs(scissor_rect, container)) 
                        continue;

                    if (valid_string(search_result->thumbnail_path) && !search_result->is_thumbnail_loaded) {
                        search_result->is_thumbnail_loaded = true;
                        queue_load_thumbnail(ssl_ctx, &thumbnail_loader, &thread_context, search_result->type, search_result->id, search_result->thumbnail_path);
                    }

                    const Color container_color = i % 2 ? WHITE : RAYWHITE;
                    
                    draw_search_result(search_result, thumbnail, container, container_color, ui);

                    if ((CheckCollisionPointRec(GetMousePosition(), container)) && (CheckCollisionPointRec(GetMousePosition(), scissor_rect)) && (IsMouseButtonPressed(MOUSE_BUTTON_LEFT))) {

                            update_flags.is_task_set = true ;
                            query.type = QUERY_TYPE_REPLACE ;
                            strcpy(query.focused_id, search_result->id) ;
                        
                        switch (search_result->type) {
                            case SEARCH_RESULT_TYPE_VIDEO:
                            case SEARCH_RESULT_TYPE_LIVE_VIDEO:
                            case SEARCH_RESULT_TYPE_PLAYLIST_VIDEO: query.action = QUERY_ACTION_VIDEO_PRESS ;    break ;
                            case SEARCH_RESULT_TYPE_PLAYLIST:       query.action = QUERY_ACTION_PLAYLIST_PRESS ; break ;
                            case SEARCH_RESULT_TYPE_CHANNEL:        query.action = QUERY_ACTION_CHANNEL_PRESS ; break ;
                            default:
                                break ;
                        }
                    }
                }

                pthread_mutex_unlock(&results.mutex);

                EndScissorMode(); 
            }

            // right half of app
            {
                const Rectangle area = {
                    .x = search_bar_bounds.x + search_bar_bounds.width + ui.padding,
                    .y = 0,
                    .width = GetScreenWidth() - area.x,
                    .height = GetScreenHeight()
                };

                const float button_bar_height = 35;

                const Rectangle video_management_button_bar = {
                    .x = area.x,
                    .y = 0,
                    .width = GetScreenWidth() - video_management_button_bar.x,
                    .height = button_bar_height, 
                };

                const Rectangle user_data_button_bar = {
                    .x = area.x,
                    .y = area.y + area.height - button_bar_height,
                    .width = GetScreenWidth() - user_data_button_bar.x,
                    .height = button_bar_height,
                };

                const Rectangle focused_video_bounds = {
                    .x = area.x + ui.padding,
                    .y = video_management_button_bar.y + video_management_button_bar.height,
                    .width = area.width - (ui.padding * 2),
                    .height = GetScreenHeight() - (button_bar_height * 2),
                };

                draw_video_management_buttons(video_management_button_bar, &query, highlighted_video, user_data.liked_videos, &update_flags, &thread_context, &user_data);
                draw_user_data_buttons(user_data_button_bar, &query, &update_flags);
                draw_text_scrollable(focused_video_bounds, false, ui, &description_scrollbar, highlighted_video->description);
            }

        EndDrawing();
    }

    thread_context_free(&thread_context);     
    thumbnail_loader_free(&thumbnail_loader);
    client_context_free(&client_context);

    UnloadFont(ui.font);
    linked_list_free(&results);
    texture_cache_free(&texture_cache);
    youtube_parse_context_free(&parse_context) ;
    
    // user_data_free(&user_data);

    youtube_search_result_free(highlighted_video) ;
    youtube_search_result_free(highlighted_channel) ;
    
    if (ssl_ctx) {
        SSL_CTX_free(ssl_ctx); ssl_ctx = NULL;
    }
    
    CloseWindow();
    
    return 0;
}