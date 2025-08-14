#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "include/thread_context.h"
#include "include/utils.h"
#include "include/threads.h"
#include "include/user_data.h"
#include "include/query_ops.h"
#include "include/request_config.h"

#include "include/raylib.h"
#include "cjson/cJSON.h"

#define RAYGUI_IMPLEMENTATION
#include "include/raygui.h"

void init_app()
{
    SetTargetFPS(60);
    SetTraceLogLevel(LOG_ERROR);
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    SetConfigFlags(FLAG_WINDOW_ALWAYS_RUN);
    InitWindow(1000, 750, "Metube");
}

typedef struct
{
    Font font;
    Color text_color;
    int font_size;
    int padding;
    int spacing;
    bool word_wrap;
} Ui;

// Draw text using font inside rectangle limits with support for text selection
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

// Draw text using font inside rectangle limits
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
    const float button_height = container.height - (ui.padding * 2);

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
        .height = button_height,
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

    const MediaType filterable_media_types [] = {
        MEDIA_TYPE_ANY,
        MEDIA_TYPE_VIDEO,
        MEDIA_TYPE_CHANNEL,
        MEDIA_TYPE_PLAYLIST,
        MEDIA_TYPE_LIVE,
    };

    const size_t nmedias = sizeof(filterable_media_types) / sizeof(filterable_media_types[0]);

    const Rectangle media_type_bounds = {
        .x = container.x,
        .y = sort_type_bounds.y + sort_type_bounds.height,
        .width = container.width,
        .height = container.height / 3.0f,
    };

    const char* media_text = media_type_to_text(query->media);

    if (draw_toggle_filter(media_type_bounds, ui, "TYPE", media_text)) {
        query->media = bound_index_to_array((query->media + 1), nmedias);
    }

    const Rectangle allow_shorts_bounds = {
        .x = container.x,
        .y = media_type_bounds.y + media_type_bounds.height,
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

    const float thumbnail_width = media_type_to_thumbnail_width(MEDIA_TYPE_VIDEO);

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

    switch (search_result->media_type) {
        case MEDIA_TYPE_SHORT:
        case MEDIA_TYPE_VIDEO: {
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
        case MEDIA_TYPE_LIVE: {
            const Vector2 view_count_pos = {
                .x = subtext_area.x + ui.padding,
                .y = subtext_area.y + ui.padding,
            };

            DrawTextEx(ui.font, TextFormat("%s watching", search_result->view_count), view_count_pos, font_size, ui.spacing, BLACK);
            
            DrawTextureEx(thumbnail, (Vector2){thumbnail_area.x, thumbnail_area.y}, 0.0f, 1.0f, WHITE);
            draw_thumbnail_subtext(thumbnail_area, ui, RAYWHITE, 12, "LIVE");
            break;
        }
        case MEDIA_TYPE_CHANNEL: {
            const float x_padding = thumbnail.width / 2.0f;
            const float y_padding = (container.height - thumbnail.height) / 2.0f;
            DrawTextureEx(thumbnail, (Vector2){thumbnail_area.x + x_padding, thumbnail_area.y + y_padding}, 0.0f, 1.0f, WHITE);
            DrawTextBoxed(search_result->subscriber_count, get_padded_rectangle(ui.padding, subtext_area), ui, 12, BLACK);
            draw_thumbnail_subtext(thumbnail_area, ui, RAYWHITE, 12, "Channel");
            break;
        }
        case MEDIA_TYPE_PLAYLIST:
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

    const float thumbnail_w = media_type_to_thumbnail_width(MEDIA_TYPE_CHANNEL);
    
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
    const float button_height = 25;
    const Rectangle subscribe_button_bounds = {
        .x = container.x + container.width - button_width - ui->padding,
        .y = container.y + container.height - button_height - ui->padding,
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

void draw_video_management_buttons(const Rectangle container, Query* query, HighlightedVideo* selected_video, cJSON* liked_video_data, bool* launch_search, bool* load_channel_info)
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

    if (selected_video->info.id[0] == '\0') GuiSetState(STATE_DISABLED);

    if (GuiButton(play_video_button_bounds, "Play Video")) {
        // TODO
    } 
    
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
        *launch_search = true;
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
        *load_channel_info = true;
        query->attr = QUERY_ATTR_REPLACE;
        query->type = QUERY_TYPE_VIEW_CHANNEL;

        strncpy(query->focused_id, selected_video->info.authorId, sizeof(query->focused_id) - 1);
        query->focused_id[sizeof(query->focused_id) - 1] = '\0';
    }

    GuiSetState(STATE_NORMAL);
}

void draw_user_data_buttons(const Rectangle container, Query* query, bool* view_subscribed_channels, bool* view_liked_videos, bool* view_watch_history)
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
        *view_subscribed_channels = true;
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
        *view_liked_videos = true;
    }
    
    const Rectangle watch_history_button_bounds = {
        .x = liked_videos_button_bounds.x + liked_videos_button_bounds.width + padding,
        .y = container.y,
        .width = fmax(min_button_width, button_width),
        .height = container.height,
    };

    if (GuiButton(watch_history_button_bounds, "Watch History")) {
        *view_watch_history = true;
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
        query->attr = QUERTY_ATTR_APPEND;
    }
}

// need to check if all big structs failed in init or not

// seperate all queue operations somewhere?

int main()
{
    List results = init_list();

    TextureCache texture_cache = NULL;

    bool load_video_information = false;
    Vector2 description_scrollbar = {0};
    HighlightedVideo highlighted_video = {0};
    
    bool load_channel_information = false;
    HighlightedChannel highlighted_channel = {0};

    Query query = {
        .string = "",
        .media = MEDIA_TYPE_ANY,
        .sort = SORT_TYPE_RELEVANCE,
        .attr = QUERY_ATTR_REPLACE,
        .type = QUERY_TYPE_USER_INPUT,
        .allow_youtube_shorts = true,
    };
    
    bool view_liked_videos = false;
    bool view_watch_history = false;
    bool view_subscribed_channels = false;
    bool show_filter_window = false;
    bool text_box_focused = false;
    bool launch_search = false;
    
    QueryType last_query_type = -1;
    char last_search_query[512] = {0};
    
    Vector2 result_scrollbar = {0};
    
    init_app();
    
    Ui ui = {
        .font = GetFontDefault(),
        .font_size = 12,
        .text_color = BLACK,
        .padding = 5,
        .spacing = 2,
        .word_wrap = true,
    };
    
    UserData user_data = user_data_init();
    if (user_data_is_ready(&user_data) == false) {
        fprintf(stderr, "CRITICAL: failed to create UserData object\n");
        return 1;
    }
    
    ClientContext client_ctx = client_context_init(MAX_THREADS);
    ThumbnailLoader thumbnail_loader = thumbnail_loader_init(MAX_THREADS);

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

        if (load_video_information) {
            load_video_information = false;

            HttpsRequest req = configure_post_request(query, client_ctx.youtube_api_pool.connections->host, client_ctx.api_key, client_ctx.continuation_token);

            VideoMetadataArgs* targs = malloc(sizeof(VideoMetadataArgs));
            if (targs) {
                targs->req = req;
                targs->ssl_ctx = ssl_ctx;
                targs->client_ctx = &client_ctx;
                targs->highlighted_video = &highlighted_video;
                
                if (!thread_context_add_task(&thread_context, targs, get_video_metadata)) {
                    free(targs); targs = NULL;
                }
            }
        }

        if (launch_search) {
            launch_search = false;

            // evade bot detection
            if (strcmp(last_search_query, query.string) == 0) {
                cycle_connection(&client_ctx.youtube_api_pool);
            }

            last_query_type = query.type;
            strncpy(last_search_query, query.string, sizeof(last_search_query));

            SearchThreadArgs* targs = malloc(sizeof(SearchThreadArgs));
            if (targs == NULL) {
                printf("'targs' is null\n");
                return 1;
            }

            targs->query = query;
            targs->results = &results;
            targs->ssl_ctx = ssl_ctx;
            targs->client_ctx = &client_ctx;

            if (thread_context_add_task(&thread_context, targs, get_results_from_query) == false) {
                printf("failed to launch task: 'get_results_from_query'\n");
                free(targs); targs = NULL;
            }
        }

        if (view_liked_videos) {
            view_liked_videos = false;

            pthread_mutex_lock(&client_ctx.token_mutex);

            if (client_ctx.continuation_token) {
                free(client_ctx.continuation_token); client_ctx.continuation_token = NULL;
            }

            pthread_mutex_unlock(&client_ctx.token_mutex);

            load_user_data(&results, user_data.liked_videos);
        }

        if (view_watch_history) {
            view_watch_history = false;

            pthread_mutex_lock(&client_ctx.token_mutex);

            if (client_ctx.continuation_token) {
                free(client_ctx.continuation_token); client_ctx.continuation_token = NULL;
            }

            pthread_mutex_unlock(&client_ctx.token_mutex);

            load_user_data(&results, user_data.watched_videos);
        }

        if (view_subscribed_channels) {
            view_subscribed_channels = false;

            pthread_mutex_lock(&client_ctx.token_mutex);

            if (client_ctx.continuation_token) {
                free(client_ctx.continuation_token); client_ctx.continuation_token = NULL;
            }

            pthread_mutex_unlock(&client_ctx.token_mutex);

            load_user_data(&results, user_data.subscribed_channels);
        }

        if (load_channel_information) {
            load_channel_information = false;
            last_query_type = query.type;

            ChannelMetadataArgs* targs = malloc(sizeof(ChannelMetadataArgs));
            if (targs) {
                targs->query = query;
                targs->results = &results;
                targs->ssl_ctx = ssl_ctx;
                targs->client_ctx = &client_ctx;
                targs->channel = &highlighted_channel;
                targs->thumbnail_loader = &thumbnail_loader;
                targs->subscribed_channels_json = user_data.subscribed_channels;

                if (!thread_context_add_task(&thread_context, targs, get_channel_metadata)) {
                    fprintf(stderr, "failed to launch get_channel_metadata task\n");
                    free(targs); targs = NULL;
                }
            }
        }

        BeginDrawing();

            ClearBackground(RAYWHITE);

            const float BAR_HEIGHT = 25.0;

            const Rectangle trending_button_bounds = {
                .x = ui.padding,
                .y = ui.padding,
                .width = 20,
                .height = BAR_HEIGHT,
            };

            if (GuiButton(trending_button_bounds, "T")) {
                launch_search = true;
                query.attr = QUERY_ATTR_REPLACE;
                query.type = QUERY_TYPE_VIEW_TRENDING;
            }

            const Rectangle search_bar_bounds = {
                .x = trending_button_bounds.x + trending_button_bounds.width + ui.padding, 
                .y = ui.padding, 
                .width = 325, 
                .height = BAR_HEIGHT 
            };

            if (GuiTextBox(search_bar_bounds, query.string, sizeof(query.string), text_box_focused)) {
                text_box_focused = !text_box_focused;
            }

            const Rectangle search_button_bounds = {
                .x = search_bar_bounds.x + search_bar_bounds.width + ui.padding, 
                .y = ui.padding, 
                .width = 25, 
                .height = BAR_HEIGHT
            };

            if (GuiButton(search_button_bounds, "S") || IsKeyPressed(KEY_ENTER)) {
                if (trim_whitespace(query.string) > 0) {
                    launch_search = true;
                    query.attr = QUERY_ATTR_REPLACE;
                    query.type = QUERY_TYPE_USER_INPUT;
                }
            }

            const Rectangle filter_button_bounds = {
                .x = search_button_bounds.x + search_button_bounds.width + ui.padding,
                .y = ui.padding,
                .width = 25,
                .height = BAR_HEIGHT,
            };

            if (GuiButton(filter_button_bounds, "Fil")) {
                show_filter_window = !show_filter_window;
            }

            const Rectangle filter_window_bounds = {
                    .x = ui.padding,
                    .y = BAR_HEIGHT + (ui.padding * 2), 
                    .width = trending_button_bounds.width + search_bar_bounds.width + search_button_bounds.width + filter_button_bounds.width + (ui.padding * 3), 
                    .height = 75
            };

            if (show_filter_window) {
                draw_filter_window(filter_window_bounds, ui, &query);
            }
            
            const float focused_channel_height = 80;
            const Rectangle focused_channel_bounds = {
                .x = ui.padding,
                .y = GetScreenHeight() - focused_channel_height - ui.padding,
                .width = trending_button_bounds.width + search_bar_bounds.width + search_button_bounds.width + filter_button_bounds.width + (ui.padding * 3), 
                .height = focused_channel_height,
            };
           
            const Rectangle scroll_window_bounds = { 
                .x = ui.padding, 
                .y = search_bar_bounds.y + search_bar_bounds.height + (show_filter_window ? (filter_window_bounds.height + ui.padding) : 0) + ui.padding, 
                .width = focused_channel_bounds.width, 
                .height = GetScreenHeight() - scroll_window_bounds.y - focused_channel_height - (ui.padding * 2), 
            };

            const bool load_more_button_visible = valid_string(client_ctx.continuation_token);
            const size_t results_len = results.count + load_more_button_visible;

            const int container_height = 80;
            const Rectangle content_area = {
                .x = scroll_window_bounds.x,
                .y = scroll_window_bounds.y,
                .width = scroll_window_bounds.width,
                .height = container_height * results_len,
            };

            const int SCROLLBAR_WIDTH = 13;
            const bool vertical_scrollbar_visible = content_area.height > scroll_window_bounds.height;

            GuiScrollPanel(scroll_window_bounds, NULL, content_area, &result_scrollbar, NULL, true);

            const Rectangle scissor_rect = get_padded_rectangle(1, scroll_window_bounds);

            BeginScissorMode(scissor_rect.x, scissor_rect.y, scissor_rect.width, scissor_rect.height);

            int i = 0;
            float container_y = scroll_window_bounds.y;
            Rectangle container = { 
                .x = scroll_window_bounds.x, 
                .y = container_y, 
                .width = scroll_window_bounds.width - (vertical_scrollbar_visible ? SCROLLBAR_WIDTH: 0),
                .height = container_height 
            };

            pthread_mutex_lock(&results.mutex);

            for (Node* node = results.head; node; node = node->next, i++, container_y += container_height) {
                SearchResult* search_result = (SearchResult*) node->content;

                container.y = container_y + result_scrollbar.y;

                if (CheckCollisionRecs(scissor_rect, container) == false) {
                    continue;
                }

                const bool result_is_highlighted = strcmp(search_result->id, highlighted_video.info.id) == 0;
                const Color container_color = result_is_highlighted ? BLUE : ((i % 2) ? WHITE : RAYWHITE);

                Texture2D thumbnail = (Texture2D){0};

                TextureCacheEntry* cached = texture_cache_find_entry(&texture_cache, search_result->id);
                if (texture_cache_entry_is_ready(cached)) {
                    thumbnail = cached->texture;
                    timer_start(&cached->timer, CACHED_TEXTURE_LIFETIME); // refresh lifetime
                }

                else if (valid_string(search_result->thumbnail_path) && !search_result->thumbnail_loaded) {
                    search_result->thumbnail_loaded = true;

                    if (!queue_thumbnail_load(ssl_ctx, &thumbnail_loader, &thread_context.task_queue, search_result->media_type, search_result->id, search_result->thumbnail_path)) {
                        fprintf(stderr, "failed to queue thumbnail load\n");
                    }
                }

                draw_search_result(search_result, thumbnail, container, container_color, ui);

                if ((CheckCollisionPointRec(GetMousePosition(), container)) && 
                    (CheckCollisionPointRec(GetMousePosition(), scissor_rect)) &&
                    (IsMouseButtonPressed(MOUSE_BUTTON_LEFT))) {
                    query.attr = QUERY_ATTR_REPLACE;

                    strncpy(query.focused_id, search_result->id, sizeof(query.focused_id) - 1);
                    query.focused_id[sizeof(query.focused_id) - 1] = '\0';

                    switch (search_result->media_type) {
                        case MEDIA_TYPE_LIVE:
                        case MEDIA_TYPE_SHORT:
                        case MEDIA_TYPE_VIDEO:
                            if (result_is_highlighted == false) {
                                load_video_information = true;
                                query.type = QUERY_TYPE_VIEW_VIDEO;
                                memcpy(&highlighted_video.info, search_result, sizeof(SearchResult));
                                add_user_data(user_data.watched_videos, search_result);

                            }
                            break;
                        case MEDIA_TYPE_PLAYLIST:
                            launch_search = true;
                            query.type = QUERY_TYPE_VIEW_PLAYLIST;
                            break;
                        case MEDIA_TYPE_CHANNEL:
                            load_channel_information = true;
                            query.type = QUERY_TYPE_VIEW_CHANNEL;
                            break;
                        case MEDIA_TYPE_ANY:
                        case MEDIA_TYPE_UNDF:
                          break;
                    }
                }
            }

            pthread_mutex_unlock(&results.mutex);
            
            if (load_more_button_visible) {
                const Rectangle load_more_button_bounds = {
                    .x = container.x,
                    .y = container.y + container_height,
                    .width = container.width,
                    .height = container_height,
                };

                draw_load_more_button(load_more_button_bounds, ui.font, &query, last_query_type, &launch_search);
            }
            
            EndScissorMode();

            const Rectangle TOP_RIGHT_PANEL = {
                .x = filter_button_bounds.x + filter_button_bounds.width + ui.padding,
                .y = ui.padding,
                .width = GetScreenWidth() - TOP_RIGHT_PANEL.x - ui.padding,
                .height = BAR_HEIGHT, 
            };

            draw_video_management_buttons(TOP_RIGHT_PANEL, &query, &highlighted_video, user_data.liked_videos, &launch_search, &load_channel_information);

            if ((highlighted_channel.info.thumbnail_loaded == false) && (highlighted_channel.info.thumbnail_path[0] != '\0')) { 
                highlighted_channel.info.thumbnail_loaded = true;
                highlighted_channel.cached = texture_cache_find_entry(&texture_cache, highlighted_channel.info.id);
            }

            draw_highlighted_channel(focused_channel_bounds, &ui, user_data.subscribed_channels, &highlighted_channel);

            const Rectangle BOTTOM_RIGHT_PANEL = {
                .x = focused_channel_bounds.x + focused_channel_bounds.width + ui.padding,
                .y = GetScreenHeight() - BAR_HEIGHT - ui.padding,
                .width = TOP_RIGHT_PANEL.width,
                .height = BAR_HEIGHT,
            };

            draw_user_data_buttons(BOTTOM_RIGHT_PANEL, &query, &view_subscribed_channels, &view_liked_videos, &view_watch_history);
            
            const Rectangle focused_video_bounds = {
                .x = scroll_window_bounds.x + scroll_window_bounds.width + ui.padding,
                .y = filter_window_bounds.y,
                .width = GetScreenWidth() - focused_video_bounds.x,
                .height = GetScreenHeight() - focused_video_bounds.y - BAR_HEIGHT - (ui.padding * 2),
            };
            
            draw_text_scrollable(focused_video_bounds, false, ui, &description_scrollbar, highlighted_video.description);
            
        EndDrawing();
    }

    thread_context_free(&thread_context);     
    thumbnail_loader_free(&thumbnail_loader);
    client_context_free(&client_ctx);

    UnloadFont(ui.font);
    free_list(&results);
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