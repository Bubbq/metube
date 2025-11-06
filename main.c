#include "include/thumbnails.h"
#include "include/thread_utils.h"
#include "innertube/include/innertube.h"

#define RAYGUI_IMPLEMENTATION
#include "include/raygui.h"

#define THREADS 4
#define YOUTUBE_HOST "www.youtube.com"
#define PARSE_CONFIG_PATH "./config/paths.json"

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

// user data management

typedef enum {
    USER_DATA_LIKES,
    USER_DATA_HISTORY,
    USER_DATA_SUBSCRIPTIONS,
    USER_DATA_COUNT,
} UserDataType;

char * user_data_type_to_text (const UserDataType type)
{
    switch (type) {
        case USER_DATA_LIKES: return "likes" ;
        case USER_DATA_HISTORY: return "history" ;
        case USER_DATA_SUBSCRIPTIONS: return "subscriptions" ;
        default:
            return NULL ;
    }
}

static const char * filepaths [USER_DATA_COUNT] = {
    [USER_DATA_LIKES] = "likes.json",
    [USER_DATA_HISTORY] = "history.json",
    [USER_DATA_SUBSCRIPTIONS] = "subscriptions.json",
};

static const ParseToken USER_DATA_TOKENS [] = {
    {FIELD_ID, extract_string},
    {FIELD_TITLE, extract_string},
    {FIELD_DURATION, extract_int},
    {FIELD_AUTHOR_ID, extract_string},
    {FIELD_VIEW_COUNT, extract_int},
    {FIELD_LIVE_VIEW_COUNT, extract_int},
    {FIELD_PLAYLIST_LENGTH, extract_int},
    {FIELD_UPLOAD_DATE, extract_string},
    {FIELD_THUMBNAIL_PATH, extract_string},
    {FIELD_SUBSCRIBER_COUNT, extract_int},
    {FIELD_SEARCH_RESULT_TYPE, extract_int},
    TOKEN_SENTINAL,
};

bool user_data_init (LinkedList user_data[USER_DATA_COUNT], const char * filepaths[USER_DATA_COUNT])
{
    for (int i = 0; i < USER_DATA_COUNT; i++) {
        user_data[i] = linked_list_init() ;

        if ( !file_exists(filepaths[i])) {
            fprintf(stderr, "user_data_init: \"%s\" not yet created\n", filepaths[i]);
            continue ;
        }

        json *jarray = json_create_from_file(filepaths[i]) ;
        
        if ( !json_array_is_valid(jarray)) {
            fprintf(stderr, "user_data_init: failed to retrieve valid JSON array from \"%s\"\n", filepaths[i]);
            if (jarray) 
                json_free(jarray) ;
            continue ;
        }

        const size_t nelements = json_get_array_size(jarray) ;

        for (size_t j = 0; j < nelements; j++) {
            json * item = json_get_array_item(jarray, j) ;

            YoutubeSearchResult *result = youtube_search_result_init() ;
            
            if ( !result) {
                fprintf(stderr, "user_data_init: failed to create YoutubeSearchResult object\n") ;
                json_free(jarray) ;
                return false ;
            }

            for (const ParseToken *token = USER_DATA_TOKENS; parse_token_is_ready(token); token++) 
                parse_token_execute(token, field_to_text(token->field), result, item, RESPONSE_USER_DATA) ;

            Node *node = node_init(result, sizeof(YoutubeSearchResult),(void*)youtube_search_result_free,(void*)youtube_search_result_print);
            
            if ( !node) {
                fprintf(stderr, "user_data_init: failed to create Node object\n");
                youtube_search_result_free(result) ;
                json_free(jarray) ;
                return false ;
            }

            pthread_mutex_lock(&user_data[i].mutex) ;
            linked_list_append(&user_data[i], node) ;
            pthread_mutex_unlock(&user_data[i].mutex) ;
        }

        json_free(jarray) ;
    }

    return true ;
}

void user_data_free (LinkedList * lists, const size_t list_count)
{
    if ( !lists)
        return ;

    size_t i = 0 ;

    for (LinkedList * list = lists; i < list_count; i++, list++) {
        json * jarray = json_create_array() ;

        if ( !jarray) {
            fprintf(stderr, "user_data_free: failed to create json array object\n") ;
            continue ; 
        }
        
        for (Node * node = list->head; node; node = node->next) {
            json * result_json = create_search_result_json(node->data) ;
            if (result_json)
                cJSON_AddItemToArray(jarray, result_json) ;
        }
        
        json_write_to_file(jarray, filepaths[i]) ;

        json_free(jarray) ;

        linked_list_free(list) ;
    }
}

bool user_data_add (LinkedList * list, YoutubeSearchResult * data, const UserDataType type)
{
    if ( !list || !data || !enum_is_valid(type, USER_DATA_COUNT))
        return false ;

    YoutubeSearchResult * result = youtube_search_result_init() ;

    if ( !result) {
        fprintf(stderr, "user_data_add: failed to create YoutubeSearchResult object\n") ;
        return false ;
    }

    memcpy(result, data, sizeof(YoutubeSearchResult)) ;

    Node * node = node_init(result, sizeof(YoutubeSearchResult), (void*) youtube_search_result_free, (void*) youtube_search_result_print) ;

    if ( !node) {
        fprintf(stderr, "user_data_add: failed to create Node object\n") ;
        youtube_search_result_free(result) ;
    }

    linked_list_append(list, node) ;

    printf("\"%s\" added to %s\n", result->id, user_data_type_to_text(type)) ;

    return true ;
}

// query operations

typedef struct
{
    char * video_id;
    bool * playing_video;
} PlayVideoArgs;

PlayVideoArgs * create_play_video_args (char * video_id, bool * is_playing_video)
{
    if ( !valid_string(video_id) || !is_playing_video)
        return NULL ;

    PlayVideoArgs * targs = calloc(1, sizeof(PlayVideoArgs)) ;

    if (targs) {
        targs->playing_video = is_playing_video ;
        targs->video_id = video_id ;
    }

    return targs ;
}

bool configure_watch_url (const char * video_id, char * dest, const size_t dest_size)
{
    if ( !valid_string(video_id) || !dest)
        return false ;

    const int written = snprintf(dest, dest_size, "mpv https://www.youtube.com/watch?v=%s", video_id) ;
    
    return (0 < written) && (written < dest_size) ;
}

void * play_video(void * args)
{
    PlayVideoArgs * targs = (PlayVideoArgs*) args ;

    if ( !targs || !targs->playing_video || !valid_string(targs->video_id))
        return NULL ;

    char command[512] = {0} ;
    
    if ( !configure_watch_url(targs->video_id, command, sizeof(command)))
        return false ;

    (*targs->playing_video) = true ;

    system(command) ;

    (*targs->playing_video) = false ;
    
    return NULL ;
}

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

void get_youtube_number_format (const float value, char * dest, const size_t dest_size)
{
    if      (value < 1e3)  snprintf(dest, dest_size, "%.0f", value) ;         // 0               - 999
    else if (value < 1e4)  snprintf(dest, dest_size, "%.2fk", (value / 1e3)) ; // 1,000           - 9,999
    else if (value < 1e5)  snprintf(dest, dest_size, "%.1fk", (value / 1e3)) ; // 10,000          - 99,999
    else if (value < 1e6)  snprintf(dest, dest_size, "%.0fk", (value / 1e3)) ; // 100,000         - 999,999
    else if (value < 1e7)  snprintf(dest, dest_size, "%.2fM", (value / 1e6)) ; // 1,000,000       - 9,999,999
    else if (value < 1e8)  snprintf(dest, dest_size, "%.1fM", (value / 1e6)) ; // 10,000,000      - 99,999,999
    else if (value < 1e9)  snprintf(dest, dest_size, "%.0fM", (value / 1e6)) ; // 100,000,000     - 999,999,999
    else if (value < 1e10) snprintf(dest, dest_size, "%.2fB", (value / 1e9)) ; // 1,000,000,000   - 9,999,999,999
    else if (value < 1e11) snprintf(dest, dest_size, "%.1fB", (value / 1e9)) ; // 10,000,000,000  - 99,999,999,999
    else if (value < 1e12) snprintf(dest, dest_size, "%.0fB", (value / 1e9)) ; // 100,000,000,000 - 999,999,999,999
}

void format_video_duration (const int duration_s, char * dest, const size_t sizeof_dest)
{   
    int hours = duration_s / 3600 ; 
    int minutes = (duration_s - (hours * 3600)) / 60 ;
    int seconds = duration_s % 60 ;

    if (hours > 0)
        snprintf(dest, sizeof_dest, "%d:%02d:%02d", hours, minutes, seconds) ;

    else 
        snprintf(dest, sizeof_dest, "%d:%02d", minutes, seconds) ;
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

            get_youtube_number_format(search_result->view_count, view_count_text, sizeof(view_count_text)) ;
            
            strcat(view_count_text, " views") ;
            
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

void draw_video_management_buttons(const Rectangle container, Query * query, LinkedList * user_likes, const YoutubeSearchResult * highlighted_video, UpdateFlags * update_flags)
{
    const int padding = 5 ;

    const size_t nbuttons = 4 ;
    
    Rectangle button_areas[nbuttons] ;

    layout_dynamic_bar(container, padding, MIN_BUTTON_WIDTH, button_areas, nbuttons) ;

    if ( !highlighted_video || !valid_string(highlighted_video->id))
        GuiSetState(STATE_DISABLED) ;

    if (GuiButton(button_areas[0], "Play Video") && !update_flags->is_playing_video) {
        query->action = QUERY_ACTION_PLAY_VIDEO ;
        update_flags->is_task_set = update_flags->is_playing_video = true ;
    } 

    if (GuiButton(button_areas[1], highlighted_video->is_liked ? "unlike" : "like")) {
        update_flags->is_task_set = true ;
        query->action = QUERY_ACTION_LIKE_VIDEO ;
    } 

    if (GuiButton(button_areas[2], "related")) {
        update_flags->is_task_set = true ;
        query->type = QUERY_TYPE_REPLACE ;
        query->action = QUERY_ACTION_VIEW_RELATED ;
        strcpy(query->focused_id, highlighted_video->id) ;
    }
    
    if (GuiButton(button_areas[3], "channel")) {
        update_flags->is_task_set = true ;
        query->type = QUERY_TYPE_REPLACE ;
        query->action = QUERY_ACTION_CHANNEL_PRESS ;
        strcpy(query->focused_id, highlighted_video->author_id) ;
    }

    GuiSetState(STATE_NORMAL) ;
}

void draw_user_data_buttons(const Rectangle container, Query* query, UpdateFlags* update_flags)
{
    if ( !query || !update_flags)
        return ;

    const int padding = 5 ;
    const size_t nbuttons = 3 ;

    Rectangle button_areas[nbuttons] ;
    layout_dynamic_bar(container, padding, MIN_BUTTON_WIDTH, button_areas, nbuttons) ;

    if (GuiButton(button_areas[0], "subscriptions")) {
        update_flags->is_task_set = true ;
        query->type = QUERY_TYPE_REPLACE ;
        query->action = QUERY_ACTION_VIEW_SUBSCRIPTIONS ;
    }

    if (GuiButton(button_areas[1], "likes")) {
        update_flags->is_task_set = true ;
        query->type = QUERY_TYPE_REPLACE ;
        query->action = QUERY_ACTION_VIEW_LIKES ;
    }
    
    if (GuiButton(button_areas[2], "history")) {
        update_flags->is_task_set = true ;
        query->type = QUERY_TYPE_REPLACE ;
        query->action = QUERY_ACTION_VIEW_HISTORY ;
    }
}

void draw_highlighted_channel (const YoutubeSearchResult * channel, UpdateFlags * flags, Query * query)
{
    const float height = 80 ;
    const float padding = 5 ;
    const Rectangle bounds = {
        .x = padding,
        .y = GetScreenHeight() - padding - height,
        .width = 380, 
        .height = height,
    } ;
    
    DrawRectangleLinesEx(bounds, 1, GRAY) ;
   
    if ( !channel || !valid_string(channel->id) || !flags || !query)
        return ;

    const float title_font_size = 25 ;
    const float sub_count_font_size = 20 ;

    char subscriber_count_text[32] = {0} ;
    get_youtube_number_format(channel->subscriber_count, subscriber_count_text, sizeof(subscriber_count_text)) ;
    strcat(subscriber_count_text, " subscribers") ;

    DrawText(channel->title, bounds.x + padding, bounds.y + padding, title_font_size, DARKGRAY) ;
    DrawText(subscriber_count_text, bounds.x + padding, bounds.y + title_font_size + (padding * 2), sub_count_font_size, DARKGRAY) ;

    const char * button_text = channel->is_subscribed ? "unsubscribe" : "subscribe" ;
    const float button_width = 80 ;
    const Rectangle button_bounds = {
        .x = (bounds.x + bounds.width) - (button_width + padding),
        .y = bounds.y + title_font_size + (padding * 2),
        .width = button_width,
        .height = 20, 
    };

    if (GuiButton(button_bounds, button_text)) {
        flags->is_task_set = true ;
        query->action = QUERY_ACTION_UPDATE_SUBSCRIPTION ;
    }
}

// TODO : UI OVERHAUL
    // TODO : VIEW USER DATA (SUBSCRIPTIONS, LIKES, HISTORY)
// TODO : REDO THUMBNAILS.H/C
// TODO : CHANGE HOW VIDEO/CHANNEL METADATA IS PROCESSED
// TODO : BETTER JSON PARSER OR CREATE OWN

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

    LinkedList user_data[USER_DATA_COUNT] ;

    if ( !user_data_init(user_data, filepaths)) {
        fprintf(stderr, "CRITICAL: failed to create user data object\n") ;
        return 1 ;
    }

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
                    LinkedList * likes = &user_data[USER_DATA_LIKES] ;
                    VideoMetadataArgs * args = create_video_metadata_args(ssl_ctx, query.focused_id, &client_context, highlighted_video, &parse_context, likes) ;
                    thread_context_add_task(&thread_context, args, free, get_video_metadata) ;
                    break ;
                }
                case QUERY_ACTION_CHANNEL_PRESS: {
                    last_action = query.action ;
                    LinkedList * subscriptions = &user_data[USER_DATA_SUBSCRIPTIONS] ;
                    SearchThreadArgs * sargs = create_search_thread_args(query, ssl_ctx, &results, &client_context, &parse_context) ;
                    ChannelMetadataArgs * targs = create_channel_metadata_args(sargs, highlighted_channel, subscriptions) ;
                    thread_context_add_task(&thread_context, targs, (void*) free_channel_metadata_args, get_channel_metadata) ;
                    break ;
                }
                case QUERY_ACTION_PLAY_VIDEO: {
                    PlayVideoArgs * targs = create_play_video_args(highlighted_video->id, &update_flags.is_playing_video) ;
                    thread_context_add_task(&thread_context, targs, free, play_video) ;
                    
                    LinkedList * history = &user_data[USER_DATA_HISTORY] ;

                    pthread_mutex_lock(&history->mutex) ;

                    Node * found = linked_list_find(history, highlighted_video, (void*) youtube_search_result_equals) ;
                    if (found) {
                        node_detach(history, found) ;
                        linked_list_insert(history, found, 0) ;
                    }
                    
                    else 
                        user_data_add(history, highlighted_video, USER_DATA_HISTORY) ;
                    
                    pthread_mutex_unlock(&history->mutex) ;

                    break ;
                }
                case QUERY_ACTION_LIKE_VIDEO: {
                    LinkedList * likes = &user_data[USER_DATA_LIKES] ;

                    pthread_mutex_lock(&likes->mutex) ;
                    
                    if (highlighted_video->is_liked) {
                        Node * found = linked_list_find(likes, highlighted_video, (void*) youtube_search_result_equals) ;
                        if (found) {
                            printf("\"%s\" removed from likes\n", highlighted_video->id) ;
                            node_detach(likes, found) ;
                            node_free(found) ;
                        }
                    }
                    
                    else
                        user_data_add(likes, highlighted_video, USER_DATA_LIKES) ;
                
                    pthread_mutex_unlock(&likes->mutex) ;
                    
                    highlighted_video->is_liked = !highlighted_video->is_liked ;
                    
                    break ;
                }
                case QUERY_ACTION_UPDATE_SUBSCRIPTION: {
                    LinkedList * subscriptions = &user_data[USER_DATA_SUBSCRIPTIONS] ;

                    pthread_mutex_lock(&subscriptions->mutex) ;
                    
                    if (highlighted_channel->is_subscribed) {
                        Node * found = linked_list_find(subscriptions, highlighted_channel, (void*) youtube_search_result_equals) ;
                        if (found) {
                            printf("\"%s\" removed from subscriptions\n", highlighted_channel->id) ;
                            node_detach(subscriptions, found) ;
                            node_free(found) ;
                        }
                    }
                    
                    else 
                        user_data_add(subscriptions, highlighted_channel, USER_DATA_SUBSCRIPTIONS) ;
                
                    pthread_mutex_unlock(&subscriptions->mutex) ;
    
                    highlighted_channel->is_subscribed = !highlighted_channel->is_subscribed ;

                    break ;
                }
                case QUERY_ACTION_VIEW_LIKES:
                case QUERY_ACTION_VIEW_HISTORY:
                case QUERY_ACTION_VIEW_SUBSCRIPTIONS: 
                default:
                    fprintf(stderr, "QueryAction %d is invalid\n", query.action);
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
                draw_highlighted_channel(highlighted_channel, &update_flags, &query) ;
                
                // const Rectangle load_more_button_bounds = {
                //     .x = ui.padding,
                //     .y = highlighted_channel_bounds.y - load_more_button_height - ui.padding,
                //     .width = search_bar_bounds.width - ui.padding,
                //     .height = load_more_button_height,
                // };

                // if (!valid_string(client_context.continuation_token))
                //     GuiSetState(STATE_DISABLED);
            
                // if (GuiButton(load_more_button_bounds, "<< LOAD MORE >> ")) {
                //     update_flags.is_task_set = true;
                //     query.action = last_action ;
                //     query.type = QUERY_TYPE_APPEND ;
                // }

                // GuiSetState(STATE_NORMAL);
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
                        search_result->is_thumbnail_loaded = true ;
                        LoadThumbnailArgs * targs = create_load_thumbnail_args(search_result->thumbnail_path, search_result->id, ssl_ctx, &thumbnail_loader, result_type_to_media(search_result->type)) ;
                        thread_context_add_task(&thread_context, targs, free, load_thumbnail) ;
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

                draw_video_management_buttons(video_management_button_bar, &query, &user_data[USER_DATA_LIKES], highlighted_video, &update_flags) ;
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
    
    user_data_free(user_data, USER_DATA_COUNT) ;
    
    youtube_search_result_free(highlighted_video) ;
    youtube_search_result_free(highlighted_channel) ;
    
    if (ssl_ctx) 
        SSL_CTX_free(ssl_ctx) ;
    
    CloseWindow();
    
    return 0;
}