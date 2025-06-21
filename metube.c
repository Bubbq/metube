#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <pthread.h>
#include <curl/curl.h>
#include <cjson/cJSON.h>
#include <time.h>
#include "raylib.h"
#include "raylib/src/raylib.h"

#define RAYGUI_IMPLEMENTATION
#include "raygui.h"

#define SEARCH_ITEMS_PER_PAGE 20

typedef struct
{
	double startTime;
	double lifeTime;
} Timer;

void start_timer(Timer *timer, double lifetime) 
{
	timer->startTime = GetTime();
	timer->lifeTime = lifetime;
}

bool is_timer_done(Timer timer)
{ 
	return (GetTime() - timer.startTime) >= timer.lifeTime; 
} 

typedef struct {
    size_t size;
    char* memory;
} MemoryBlock;

typedef struct {
    char* key;
    char* url;
    char* video_endpoint;
    char* search_endpoint;
    char* channel_endpoint;
    char* playlist_endpoint;
} YoutubeAPI;

bool is_memory_ready(const MemoryBlock chunk)
{
    return ((chunk.size > 0) && (chunk.memory != NULL));
}

MemoryBlock create_memory_block(size_t inital)
{
    MemoryBlock chunk = { 0 };

    chunk.memory = malloc(inital);
    if (!chunk.memory) printf("create_memory_block: not enough memory, malloc returned NULL\n");
    else chunk.size = inital;
    
    return chunk;
}

void unload_memory_block(MemoryBlock* chunk)
{
    free(chunk->memory);
    chunk->memory = NULL;
    chunk->size = 0;
}

void create_file_from_memory(const char* filename, const char* memory) 
{
    FILE* fp = fopen(filename, "w");
    if (!fp) printf("could not write memory into \"%s\"\n", filename);
    else {
        fprintf(fp, "%s", memory);
        fclose(fp);
    } 
}

size_t write_data(void* src, int nitems, size_t element_size, void* dst)
{
    // first, we to know how many bytes we are appending to dst
    // becuase src is generic, we dont know the type (cant use sizeof(*(src_type)src))
    size_t src_size = (nitems * element_size);

    // next, we need to resize the dst pointer to fit this new data
    // first, find out how much memory we are currenly holding
    MemoryBlock* mem = (MemoryBlock*) dst;

    // now get the new size, '+1' for '/0'
    char* new_memory = realloc(mem->memory, (mem->size + src_size + 1));
    if (!new_memory) {
        printf("write_data: not enough memory, realloc returned null\n");
        return 0;
    }

    // update the memory of dst
    mem->memory = new_memory;

    // write new content starting from the end of the old content
    memcpy(&(mem->memory[mem->size]), src, src_size);

    // update the size accordingly
    mem->size += src_size;
    
    // explicity set null terminator 
    mem->memory[mem->size] = '\0';

    return src_size;
}

// preform a GET request to some url and return the data fetched
MemoryBlock fetch_url(const char* url, CURL* curl)
{
    MemoryBlock chunk = create_memory_block(0);

    // specify parameters for curl handle
    curl_easy_setopt(curl, CURLOPT_URL, url); // where to request data
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_data); // how to write requested data 
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk); // where to write requested data

    // store the result of the GET request
    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
    return chunk;
}

typedef enum {
    CONTENT_TYPE_VIDEO = 0,
    CONTENT_TYPE_CHANNEL = 1,
    CONTENT_TYPE_PLAYLIST = 2,
    CONTENT_TYPE_ANY = 3,
} ContentType;

typedef enum 
{
    SORT_PARAM_RELEVANCE = 0,
    SORT_PARAM_UPLOAD_DATE = 1,
    SORT_PARAM_VIEW_COUNT = 2,
    SORT_PARAM_RATING = 3,
} SortParameter;

typedef struct YoutubeSearchNode {
	char* id;
	char* title;
	char* author;
	char* subs;
	char* views;
	char* date;
	char* length;
    char* video_count;
    bool thumbnail_loaded;
    char* thumbnail_link;
    Texture thumbnail;
    ContentType type;
    struct YoutubeSearchNode* next;
} YoutubeSearchNode;

typedef struct {
    int count;
    YoutubeSearchNode* head;
    YoutubeSearchNode* tail;
} YoutubeSearchList;

YoutubeSearchList create_youtube_search_list() 
{
    YoutubeSearchList list;
    list.head = list.tail = NULL;
    list.count = 0;
    return list;
}

void add_node(YoutubeSearchList* list, const YoutubeSearchNode node)
{
    // adding the first node to a list
    if (list->count == 0) {
        list->head = list->tail = malloc(sizeof(YoutubeSearchNode));
        if (!list->head) {
            printf("add_node: not enough memory, malloc returned null\n");
            return;
        }

        // set both ends of the list to the first node of the list
        *list->head = *list->tail = node;
    } 
    else {
        // allocate space for the new node
        list->tail->next = malloc(sizeof(YoutubeSearchNode));
        if (!list->tail->next) {
            printf("add_node: not enough memory, malloc returned null\n");
            return;
        }

        // append node to list
        *list->tail->next = node;
        
        // adjust tail ptr pos.
        list->tail = list->tail->next;
        list->tail->next = NULL;
    }
    
    // update list size
    list->count++;
}

void unload_node(YoutubeSearchNode* node)
{
    if (!node) return;

    if (node->id) free(node->id);
    if (node->subs) free(node->subs);
    if (node->date) free(node->date);
    if (node->views) free(node->views);
    if (node->title) free(node->title);
    if (node->length) free(node->length);
    if (node->video_count) free(node->video_count);
    if (node->author) free(node->author);
    if (node->thumbnail_link) free(node->thumbnail_link);
    if (IsTextureReady(node->thumbnail)) UnloadTexture(node->thumbnail);

    free(node);
}

void unload_list(YoutubeSearchList* list) 
{
    YoutubeSearchNode* prev = NULL;
    YoutubeSearchNode* current = list->head;

    while (current) {
        prev = current;
        current = current->next;
        unload_node(prev);
    }

    list->head = list->tail = NULL;
    list->count = 0;
}

void print_node(const YoutubeSearchNode* node) 
{
    printf("id) %s title) %s author) %s subs) %s views) %s date) %s length) %s video count) %s thumbnail id) %d type) %d\n", 
            node->id, node->title, node->author, node->subs, node->views, node->date, node->length, node->video_count, node->thumbnail.id, node->type);
}

void print_list(const YoutubeSearchList* list)
{
    for (YoutubeSearchNode *current = list->head; current; current = current->next) print_node(current);
}

// given the search results page source, return relevant details
char* extract_yt_data(const char* html) 
{
    const char* needle = "var ytInitialData = ";

    // ptr to first char that matches needle
    const char* location = strstr(html, needle);
    if (!location) {
        printf("extract_yt_data: \"%s\" was not found in html arguement\n", needle);
        return NULL;
    }

    // the desired data is enclosed in the '{}' following the yt initalization
    const char* start = (location + strlen(needle));
    const char* end = strstr(start, "};");
    if (!end) {
        printf("extract_yt_data: the closing brace '};' was not found\n");
        return NULL;
    }

    // return a duplicate of this section of data found in the html
    const size_t len = end - start + 1;
    char* ret = malloc(len + 1); // for null terminator
    if (!ret) {
        printf("extract_yt_data: not enough memory, malloc returned NULL\n");
        return NULL;
    }
    strncpy(ret, start, len);
    ret[len] = '\0';
    return ret;
}

typedef struct {
    char url_encoded_query[256];
    SortParameter sort;
    ContentType type;
} Query;

void configure_search_url(const int maxlen, char search_url[maxlen], const Query query)
{
    snprintf(search_url, maxlen, "https://www.youtube.com/results?search_query=%s&sp=", query.url_encoded_query);

    // possible sorting params
    const char* relevance = "CAA";
    const char* upload_date = "CAI";
    const char* popularity = "CAM";
    const char* rating = "CAE";
    
    switch (query.sort) {
        case SORT_PARAM_RELEVANCE: strcat(search_url, relevance); break;
        case SORT_PARAM_UPLOAD_DATE: strcat(search_url, upload_date); break;
        case SORT_PARAM_VIEW_COUNT: strcat(search_url, popularity); break;
        case SORT_PARAM_RATING: strcat(search_url, rating); break;
    }

    // possible type params
    const char* video = "SAhAB"; 
    const char* channel = "SAhAC";
    const char* playlist = "SAhAD";
    const char* none = "%253D";

    switch (query.type) {
        case CONTENT_TYPE_VIDEO: strcat(search_url, video); break;
        case CONTENT_TYPE_CHANNEL: strcat(search_url, channel); break;
        case CONTENT_TYPE_PLAYLIST: strcat(search_url, playlist); break;
        case CONTENT_TYPE_ANY: strcat(search_url, none); break;
    }  
}

void format_youtube_views(const char* views_str, const int maxlen, char dst[maxlen])
{
    // remove  all non numeric chars
    char no_commas[32] = "\0";
    for (int i = 0, j = 0; views_str[i]; i++) 
        if (isdigit(views_str[i])) no_commas[j++] = views_str[i];

    // convert to int
    long long views = atoll(no_commas);

    // formatting string
    
    // hundreds
    if (views < 1e3) snprintf(dst, maxlen, "%lld", views);
    
    // thousands
    else if (views < 1e6) {
        if (views < 1e5) snprintf(dst, maxlen, "%.1fk", (views / 1e3));
        else snprintf(dst, maxlen, "%dk", (int)(views / 1e3));
    }

    // millions
    else if (views < 1e9) {
        if (views < 1e8) snprintf(dst, maxlen, "%.1fM", (views / 1e6));
        else snprintf(dst, maxlen, "%dM", (int)(views / 1e6));
    }
    
    // billions
    else if (views < 1e12) {
        if (views < 1e11) snprintf(dst, maxlen, "%.1fB", (views / 1e9));
        else snprintf(dst, maxlen, "%dB", (int)(views / 1e9));
    }

    // trim '.0' if present
    char* loc = strstr(dst, ".0");
    if (loc) *loc = '\0';
}

Texture2D get_thumbnail_from_memory (const MemoryBlock chunk, const float width, const float height)
{
    if (is_memory_ready(chunk)) {
        Image image = LoadImageFromMemory(".jpeg", (unsigned char*) chunk.memory, chunk.size);
        if (IsImageReady(image)) {
            ImageResize(&image, width, height);
            Texture2D ret = LoadTextureFromImage(image);
            UnloadImage(image);
            return ret;
        }
        else printf("get_thumbnail_from_memory: failed to load image data\n");
    }

    return (Texture){ 0 };
}

int bound_index_to_array (const int pos, const int array_size)
{
    return (pos + array_size) % array_size;
}

char* content_type_to_text (const ContentType content_type)
{
    switch (content_type) {
        case CONTENT_TYPE_VIDEO: return "VIDEO";
        case CONTENT_TYPE_CHANNEL: return "CHANNEL";
        case CONTENT_TYPE_PLAYLIST: return "PLAYLIST";
        case CONTENT_TYPE_ANY: return "ANY";
    }

    return NULL;
}

char* sort_parameter_to_text (const SortParameter sort_parameter)
{
    switch (sort_parameter) {
        case SORT_PARAM_RELEVANCE: return "RELEVANCE";
        case SORT_PARAM_UPLOAD_DATE: return "UPLOAD DATE";
        case SORT_PARAM_VIEW_COUNT: return "VIEWS"; 
        case SORT_PARAM_RATING: return "RATING";
    }

    return NULL;
}

// Draw text using font inside rectangle limits with support for text selection
void DrawTextBoxedSelectable(Font font, const char *text, Rectangle rec, float fontSize, float spacing, bool wordWrap, Color tint, int selectStart, int selectLength, Color selectTint, Color selectBackTint)
{
    int length = TextLength(text);  // Total length in bytes of the text, scanned by codepoints in loop

    float textOffsetY = 0;          // Offset between lines (on line break '\n')
    float textOffsetX = 0.0f;       // Offset X to next character to draw

    float scaleFactor = fontSize/(float)font.baseSize;     // Character rectangle scaling factor

    // Word/character wrapping mechanism variables
    enum { MEASURE_STATE = 0, DRAW_STATE = 1 };
    int state = wordWrap? MEASURE_STATE : DRAW_STATE;

    int startLine = -1;         // Index where to begin drawing (where a line begins)
    int endLine = -1;           // Index where to stop drawing (where a line ends)
    int lastk = -1;             // Holds last value of the character position

    for (int i = 0, k = 0; i < length; i++, k++)
    {
        // Get next codepoint from byte string and glyph index in font
        int codepointByteCount = 0;
        int codepoint = GetCodepoint(&text[i], &codepointByteCount);
        int index = GetGlyphIndex(font, codepoint);

        // NOTE: Normally we exit the decoding sequence as soon as a bad byte is found (and return 0x3f)
        // but we need to draw all of the bad bytes using the '?' symbol moving one byte
        if (codepoint == 0x3f) codepointByteCount = 1;
        i += (codepointByteCount - 1);

        float glyphWidth = 0;
        if (codepoint != '\n')
        {
            glyphWidth = (font.glyphs[index].advanceX == 0) ? font.recs[index].width*scaleFactor : font.glyphs[index].advanceX*scaleFactor;

            if (i + 1 < length) glyphWidth = glyphWidth + spacing;
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
                if (!wordWrap)
                {
                    textOffsetY += (font.baseSize + font.baseSize / 2.0f) * scaleFactor;
                    textOffsetX = 0;
                }
            }
            else
            {
                if (!wordWrap && ((textOffsetX + glyphWidth) > rec.width))
                {
                    textOffsetY += (font.baseSize + font.baseSize / 2.0f) * scaleFactor;
                    textOffsetX = 0;
                }

                // When text overflows rectangle height limit, just stop drawing
                if ((textOffsetY + font.baseSize*scaleFactor) > rec.height) break;

                // Draw selection background
                bool isGlyphSelected = false;
                if ((selectStart >= 0) && (k >= selectStart) && (k < (selectStart + selectLength)))
                {
                    DrawRectangleRec((Rectangle){ rec.x + textOffsetX - 1, rec.y + textOffsetY, glyphWidth, (float)font.baseSize*scaleFactor }, selectBackTint);
                    isGlyphSelected = true;
                }

                // Draw current character glyph
                if ((codepoint != ' ') && (codepoint != '\t'))
                {
                    DrawTextCodepoint(font, codepoint, (Vector2){ rec.x + textOffsetX, rec.y + textOffsetY }, fontSize, isGlyphSelected? selectTint : tint);
                }
            }

            if (wordWrap && (i == endLine))
            {
                textOffsetY += (font.baseSize + font.baseSize / 2.0f) * scaleFactor;
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
void DrawTextBoxed(Font font, const char *text, Rectangle rec, float fontSize, float spacing, bool wordWrap, Color tint)
{
    DrawTextBoxedSelectable(font, text, rec, fontSize, spacing, wordWrap, tint, 0, 0, WHITE, WHITE);
}

// applies some padding to a rectangle
Rectangle padded_rectangle (const float padding, const Rectangle rect)
{
    return (Rectangle) { rect.x + padding, rect.y + padding, rect.width - padding, rect.height - padding };
}

typedef struct ThumbnailData {
    MemoryBlock data;
    char id[256];
    struct ThumbnailData *next;
} ThumbnailData;

typedef struct {
    ThumbnailData *head;
    ThumbnailData *tail;  
    pthread_mutex_t mutex;
} ThumbnailList;

ThumbnailList create_thumbnail_list ()
{
    ThumbnailList tl;
    tl.head = tl.tail = NULL;
    pthread_mutex_init(&tl.mutex, NULL);
    
    return tl;
}

typedef struct {
    Query query;
    YoutubeSearchList* search_results;
    ThumbnailList *thumbnail_list;
} ThreadArgs;

#define MAX_THREADS 4
pthread_t threads[MAX_THREADS];
int current_thread = 0;

void add_thumbnail_node (ThumbnailData *node, ThumbnailList *list) 
{
    if (list->head == NULL) {
        list->head = list->tail = node;
    }
    else {
        node->next = NULL;
        list->tail->next = node;
        list->tail = list->tail->next;
    }
}

void draw_thumbnail_subtext (const Rectangle container, const Font font, const Color text_color, const int font_size, const int spacing, const int padding, const char* text)
{
    const Vector2 text_size = MeasureTextEx(font, text, font_size, spacing);
    const float width = text_size.x + (padding * 2);
    const float height = text_size.y + (padding * 2);
    const Rectangle length_area = {
        container.x + container.width - width - padding,
        container.y + container.height - height - padding,
        width,
        height
    };

    DrawRectangleRec(length_area, Fade(BLACK, 0.7));
    DrawTextBoxed(font, text, padded_rectangle(padding, length_area), font_size, spacing, true, text_color);
}

bool search_finished = true;
bool searching = false;

// writes a list of search result nodes to some list
void* get_results_from_query(void* args)
{
    time_t before = time(NULL);
    
    search_finished = false;
    searching = true;

    ThreadArgs* targs = (ThreadArgs *) args;

    CURL *curl = curl_easy_init();
    curl_easy_setopt(curl, CURLOPT_TCP_FASTOPEN, 1L);         // Enable TCP Fast Open
    curl_easy_setopt(curl, CURLOPT_TCP_NODELAY, 1L);          // Disable Nagle's algorithm
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);             // Avoid signals for speed
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);       // Disable redirects (if not needed)
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);       // Skip hostname verification
    curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2_0); // Use HTTP/2

    if (!curl) {
        printf("curl object could not be created\n");
        free(targs);
        return NULL;
    }

    // get the query in url encoded format
    char* buff = curl_easy_escape(curl, targs->query.url_encoded_query, 0);
    strcpy(targs->query.url_encoded_query, buff);
    printf("processing %s\n", targs->query.url_encoded_query);
    curl_free(buff);

    // append the query to the yt query string
    char url[512] = "\0";
    configure_search_url(512, url, targs->query);

    // get the page source of this url
    MemoryBlock html = fetch_url(url, curl);

    if (!is_memory_ready(html)) return NULL;

    // extract search result data
    char* cjson_data = extract_yt_data(html.memory);
    unload_memory_block(&html);
    if (!cjson_data) return NULL;

    // get json obj
    cJSON* search_json = cJSON_Parse(cjson_data);
    free(cjson_data);
    if (!search_json) {
        printf("Error: %s\n", cJSON_GetErrorPtr());
        return NULL;
    }

    int elements_added = 0;
    cJSON *contents = cJSON_GetObjectItem(search_json, "contents");
    cJSON *twoColumnSearchResultsRenderer = contents ? cJSON_GetObjectItem(contents, "twoColumnSearchResultsRenderer") : NULL;
    cJSON *primaryContents = twoColumnSearchResultsRenderer ? cJSON_GetObjectItem(twoColumnSearchResultsRenderer, "primaryContents") : NULL;
    cJSON *sectionListRenderer = primaryContents ? cJSON_GetObjectItem(primaryContents, "sectionListRenderer") : NULL;
    cJSON *sections = sectionListRenderer ? cJSON_GetObjectItem(sectionListRenderer, "contents") : NULL;

    if (sections && cJSON_IsArray(sections)) {
        cJSON *first_section = cJSON_GetArrayItem(sections, 0);
        cJSON *itemSectionRenderer = first_section ? cJSON_GetObjectItem(first_section, "itemSectionRenderer") : NULL;
        cJSON *contents = itemSectionRenderer ? cJSON_GetObjectItem(itemSectionRenderer, "contents") : NULL;
        if (contents && cJSON_IsArray(contents)) {
            // loop through every item and get the node equivalent 
            cJSON *item;
            cJSON_ArrayForEach (item, contents) {
                YoutubeSearchNode node = { 0 };
                cJSON *channelRenderer = cJSON_GetObjectItem(item, "channelRenderer");
                cJSON *videoRenderer = cJSON_GetObjectItem(item, "videoRenderer");
                cJSON *lockupViewModel = cJSON_GetObjectItem(item, "lockupViewModel");

                if (videoRenderer) {
                    node.type = CONTENT_TYPE_VIDEO;

                    // video id
                    cJSON *videoId = cJSON_GetObjectItem(videoRenderer, "videoId");
                    if (videoId && cJSON_IsString(videoId)) node.id = strdup(videoId->valuestring);

                    // video title
                    cJSON *title = cJSON_GetObjectItem(videoRenderer, "title");
                    cJSON* runs = title ? cJSON_GetObjectItem(title, "runs") : NULL;
                    if (runs && cJSON_IsArray(runs)) {
                        cJSON* first_run = cJSON_GetArrayItem(runs, 0);
                        if (first_run) {
                            cJSON *text = cJSON_GetObjectItem(first_run, "text");
                            if (text && cJSON_IsString(text)) node.title = strdup(text->valuestring);
                        }
                    }

                    // thumbnail link
                    if (node.id) {
                        char video_thumbnail_url[128];
                        snprintf(video_thumbnail_url, sizeof(video_thumbnail_url), "https://img.youtube.com/vi/%s/mqdefault.jpg", node.id);
                        node.thumbnail_link = strdup(video_thumbnail_url);
                        // node.thumbnail = get_thumbnail_from_youtube_link(video_thumbnail_url, curl);
                    }

                    // creator of video
                    cJSON *ownerTextRuns = cJSON_GetObjectItem(cJSON_GetObjectItem(videoRenderer, "ownerText"), "runs");
                    if (ownerTextRuns && cJSON_IsArray(ownerTextRuns)) {
                        cJSON *first_run = cJSON_GetArrayItem(ownerTextRuns, 0);
                        if (first_run) {
                            cJSON *text = cJSON_GetObjectItem(first_run, "text");
                            if (text && cJSON_IsString(text)) node.author = strdup(text->valuestring);
                        }
                    }

                    // view count
                    cJSON *viewCountText = cJSON_GetObjectItem(cJSON_GetObjectItem(videoRenderer, "viewCountText"), "simpleText");
                    if (viewCountText && cJSON_IsString(viewCountText)) {
                        node.views = malloc(16);
                        format_youtube_views(viewCountText->valuestring, 16, node.views);
                    }

                    // publish date
                    cJSON *publishedTimeText = cJSON_GetObjectItem(cJSON_GetObjectItem(videoRenderer, "publishedTimeText"), "simpleText");
                    if (publishedTimeText && cJSON_IsString(publishedTimeText)) node.date = strdup(publishedTimeText->valuestring);

                    // video length
                    cJSON *lengthText = cJSON_GetObjectItem(cJSON_GetObjectItem(videoRenderer, "lengthText"), "simpleText");
                    if (lengthText && cJSON_IsString(lengthText)) node.length = strdup(lengthText->valuestring);
                }

                else if (channelRenderer) {
                    node.type = CONTENT_TYPE_CHANNEL;

                    // channel id
                    cJSON* channelId = cJSON_GetObjectItem(channelRenderer, "channelId");
                    if (channelId && cJSON_IsString(channelId)) node.id = strdup(channelId->valuestring);

                    // channel title
                    cJSON *title = cJSON_GetObjectItem(cJSON_GetObjectItem(channelRenderer, "title"), "simpleText");
                    if (title && cJSON_IsString(title)) node.title = strdup(title->valuestring);

                    // subscriber count
                    cJSON *subCount = cJSON_GetObjectItem(cJSON_GetObjectItem(channelRenderer, "videoCountText"), "simpleText");
                    if(subCount && cJSON_IsString(subCount)) node.subs = strdup(subCount->valuestring);

                    // thumbnail link
                    cJSON *thumbnails = cJSON_GetObjectItem(cJSON_GetObjectItem(channelRenderer, "thumbnail"), "thumbnails");
                    if (thumbnails && cJSON_IsArray(thumbnails)) {
                        cJSON *first_thumbnail = cJSON_GetArrayItem(thumbnails, 0);
                        cJSON *url = first_thumbnail ? cJSON_GetObjectItem(first_thumbnail, "url") : NULL;
                        if(url && cJSON_IsString(url)) {
                            char channel_thumbnail_link [128] = "https:";
                            strcat(channel_thumbnail_link, url->valuestring);
                            // if (url && cJSON_IsString(url)) node.thumbnail = get_thumbnail_from_youtube_link(channel_thumbnail_link, curl);
                            if (url && cJSON_IsString(url)) node.thumbnail_link = strdup(channel_thumbnail_link);
                        }
                    }
                }

                else if (lockupViewModel) {
                    node.type = CONTENT_TYPE_PLAYLIST;

                    // playlist id
                    cJSON *contentId = cJSON_GetObjectItem(lockupViewModel, "contentId");
                    if (contentId && cJSON_IsString(contentId)) node.id = strdup(contentId->valuestring);

                    // playlist title
                    cJSON *metadata = cJSON_GetObjectItem(lockupViewModel, "metadata");
                    cJSON *lockupMetadataViewModel = metadata ? cJSON_GetObjectItem(metadata, "lockupMetadataViewModel") : NULL;
                    cJSON *title = lockupMetadataViewModel ? cJSON_GetObjectItem(lockupMetadataViewModel, "title") : NULL;
                    cJSON *content = title ? cJSON_GetObjectItem(title, "content") : NULL;
                    if (content && cJSON_IsString(content)) node.title = strdup(content->valuestring);

                    cJSON *contentImage = cJSON_GetObjectItem(lockupViewModel, "contentImage");
                    cJSON *collectionThumbnailViewModel = contentImage ? cJSON_GetObjectItem(contentImage, "collectionThumbnailViewModel") : NULL;
                    cJSON *primaryThumbnail = collectionThumbnailViewModel ? cJSON_GetObjectItem(collectionThumbnailViewModel, "primaryThumbnail") : NULL;
                    cJSON *thumbnailViewModel = primaryThumbnail ? cJSON_GetObjectItem(primaryThumbnail, "thumbnailViewModel") : NULL;

                    // playlist thumbnail
                    cJSON *image = thumbnailViewModel ? cJSON_GetObjectItem(thumbnailViewModel, "image") : NULL;
                    cJSON *sources = image ? cJSON_GetObjectItem(image, "sources") : NULL;
                    if (sources && cJSON_IsArray(sources)) {
                        cJSON* first_source = cJSON_GetArrayItem(sources, 0);
                        cJSON *url = first_source ? cJSON_GetObjectItem(first_source, "url") : NULL;
                        // if (url && cJSON_IsString(url)) node.thumbnail = get_thumbnail_from_youtube_link(url->valuestring, curl);
                        if (url && cJSON_IsString(url)) node.thumbnail_link = strdup(url->valuestring);
                    }

                    // number of videos in playlist
                    cJSON *overlays = thumbnailViewModel ? cJSON_GetObjectItem(thumbnailViewModel, "overlays") : NULL;
                    cJSON *overlay;
                    if (overlays && cJSON_IsArray(overlays)) {
                        cJSON_ArrayForEach (overlay, overlays) {
                            cJSON *thumbnailOverlayBadgeViewModel = cJSON_GetObjectItem(overlay, "thumbnailOverlayBadgeViewModel");
                            cJSON *thumbnailBadges = thumbnailOverlayBadgeViewModel ? cJSON_GetObjectItem(thumbnailOverlayBadgeViewModel, "thumbnailBadges") : NULL;
                            if (thumbnailBadges && cJSON_IsArray(thumbnailBadges)) {
                                cJSON *thumbnailBadge;
                                cJSON_ArrayForEach (thumbnailBadge, thumbnailBadges) {
                                    cJSON *thumbnailBadgeViewModel = cJSON_GetObjectItem(thumbnailBadge, "thumbnailBadgeViewModel");
                                    if (thumbnailBadgeViewModel) {
                                        cJSON *text = cJSON_GetObjectItem(thumbnailBadgeViewModel, "text");
                                        if (text && cJSON_IsString(text)) {
                                            node.video_count = strdup(text->valuestring);
                                            break;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                if (node.id && (targs->search_results->count < SEARCH_ITEMS_PER_PAGE)) {
                    add_node(targs->search_results, node);
                    elements_added++;
                }
            }
        }
    }

    printf("processing thumbnails\n");
    pthread_mutex_lock(&targs->thumbnail_list->mutex);
        YoutubeSearchList *search_list = targs->search_results;
        for (YoutubeSearchNode *node = search_list->head; node; node = node->next) {
            if (node->thumbnail_link[0] != '\0') {
                MemoryBlock image_data = fetch_url(node->thumbnail_link, curl);
                if (is_memory_ready(image_data)) {
                    ThumbnailData *thumbnail_data = malloc(sizeof(ThumbnailData));
                    thumbnail_data->data = image_data;
                    strcpy(thumbnail_data->id, node->id);
                    add_thumbnail_node(thumbnail_data, targs->thumbnail_list);
                }
            }
        }
    pthread_mutex_unlock(&targs->thumbnail_list->mutex);
    printf("thumbnails loaded\n");

    search_finished = true;
    searching = false;

    if (elements_added == 0) printf("no items were found\n");
    cJSON_Delete(search_json);
    curl_easy_cleanup(curl);
    free(args);
    time_t after = time(NULL);
    printf("search process took %ld seconds\n", (after - before));
    return NULL;
}

int main()
{
    // start the curl session
    curl_global_init(CURL_GLOBAL_ALL);

    // list containing the search results from a query
    YoutubeSearchList search_results = create_youtube_search_list();

    // list containing the image data of thumbails from a search
    ThumbnailList thumbnail_list = create_thumbnail_list();

    // init app
    SetTargetFPS(60);
    SetTraceLogLevel(LOG_ERROR);
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    SetConfigFlags(FLAG_WINDOW_ALWAYS_RUN);
    InitWindow(1000, 750, "metube");

    const Font FONT = GetFontDefault();
    
    // flag used to start search thread
    bool search = false;
    
    // object containing elements required for the search
    // the string the user passes in the textbox
    // how the results shall be sorted
    // what type of items the user is looking for (videos, channels, playlists, etc.) 
    Query query = { 0 };

    // flag determining "GuiTextBox" functionality
    bool edit_mode = false;
    
    // data for the filter window
    bool show_filter_window = false;
    
    int current_type = 0;
    ContentType availible_types[] = { 
        CONTENT_TYPE_ANY, 
        CONTENT_TYPE_VIDEO, 
        CONTENT_TYPE_CHANNEL, 
        CONTENT_TYPE_PLAYLIST 
    };
    
    int current_sort = 0;
    SortParameter availible_sorts[] = {
        SORT_PARAM_RELEVANCE,
        SORT_PARAM_UPLOAD_DATE,
        SORT_PARAM_VIEW_COUNT,
        SORT_PARAM_RATING 
    };

    // for scroll panel
    Vector2 scroll = { 10, 10 };
    Rectangle scrollView = { 0, 0 };

    // the current search result the user has selected
    int current_node = -1;

    while (!WindowShouldClose() || searching) {
        // loading thumbnails gathered from thread
        pthread_mutex_lock(&thumbnail_list.mutex);
            while (thumbnail_list.head) {
                ThumbnailData *thumbnail_data = thumbnail_list.head;

                for (YoutubeSearchNode *search_node = search_results.head; search_node; search_node = search_node->next) {
                    if ((strcmp(thumbnail_data->id, search_node->id) == 0)) {
                        search_node->thumbnail = get_thumbnail_from_memory(thumbnail_data->data, 150, 100);
                    }
                }

                // delete node from thumbnail list
                thumbnail_list.head = thumbnail_list.head->next;
                unload_memory_block(&thumbnail_data->data);
                free(thumbnail_data);
            }
        pthread_mutex_unlock(&thumbnail_list.mutex);
        
        if (search) {
            search = false;

            // clear search result list information
            if (search_results.count > 0) unload_list(&search_results);
            current_node = -1;
            
            // configure thread arguements for routine
            ThreadArgs *targs = malloc(sizeof(ThreadArgs));
            targs->query = query;
            targs->search_results = &search_results;
            targs->thumbnail_list = &thumbnail_list;

            // get the results of this query in this thread
            pthread_create(&threads[current_thread], NULL, get_results_from_query, targs);
            pthread_detach(threads[current_thread]);

            // update thread pool
            current_thread = bound_index_to_array((current_thread + 1), MAX_THREADS);
        }

        BeginDrawing();
            ClearBackground(RAYWHITE);

            // the space elements gives one another
            const int padding = 5;
            
            // searching UI
            const Rectangle search_bar = { padding, padding, 350, 25 };
            const Rectangle search_button = { (search_bar.x + search_bar.width + padding), search_bar.y, 50, 25 };

            // toggle edit mode engaging or leaving the text box window
            // pressing enter returns 1
            // clicking out of the window returns 2
            int text_box_status; 
            if ((text_box_status = GuiTextBox(search_bar, query.url_encoded_query, 256, edit_mode))) {
                edit_mode = !edit_mode;
            } 
            
            const bool start_search = GuiButton(search_button, "SEARCH") || (text_box_status == 1);
            const bool query_entered = (query.url_encoded_query[0] != '\0') && (!edit_mode);

            // pressing the search button or pressing enter in the search bar will search 
            if (start_search && query_entered) {
                query.sort = availible_sorts[current_sort];
                query.type = availible_types[current_type];
                
                // only search when last search is done 
                search = (search_finished);
            }
            
            // filtering UI
            const Rectangle filter_button = { 
                search_button.x + search_button.width + padding, 
                padding, 
                50, 
                25 
            };
            
            // toggle filter window on press
            if (GuiButton(filter_button, "FILTER")) show_filter_window = !show_filter_window;
            
            const Rectangle filter_window_area = { padding, (search_bar.y + search_bar.height + padding), search_bar.width, 50 };
            if (show_filter_window) {
                DrawRectangleLinesEx(filter_window_area, 1, GRAY);
                const int font_size = 11;

                // buttons to switch filter params (the type of content and how they will be sorted)
                const char* button_text = "SWITCH";
                const Rectangle sort_type_button = { (filter_window_area.x + filter_window_area.width - 55), (filter_window_area.y + padding), 50, 17.5 };
                const Rectangle content_type_button = { (filter_window_area.x + filter_window_area.width - 55), (sort_type_button.y + sort_type_button.height + padding), 50, 17.5 };
                
                // update the index of filter params when pressed
                if (GuiButton(sort_type_button, button_text)) current_sort = bound_index_to_array((current_sort + 1), 4);
                if (GuiButton(content_type_button, button_text)) current_type = bound_index_to_array((current_type + 1), 4);

                // filters availible
                DrawTextEx(FONT, "ORDER:", (Vector2){ filter_window_area.x + padding, sort_type_button.y + padding }, font_size, 2, BLACK);
                DrawTextEx(FONT, "TYPE:", (Vector2){ filter_window_area.x + padding, content_type_button.y + padding }, font_size, 2, BLACK);
                
                // current param value
                DrawTextEx(FONT, content_type_to_text(availible_types[current_type]), (Vector2){ ((filter_window_area.x + filter_window_area.width) * 0.4f), (content_type_button.y + padding) }, font_size, 2, BLACK);
                DrawTextEx(FONT, sort_parameter_to_text(availible_sorts[current_sort]), (Vector2){ ((filter_window_area.x + filter_window_area.width) * 0.4f), (sort_type_button.y + padding) }, font_size, 2, BLACK);
            }

            // display search results

            // the bound of the scroll panel
            const Rectangle scroll_panel_area = { 
                search_bar.x, 
                search_bar.y + search_bar.height + (show_filter_window ? (padding + filter_window_area.height) : 0) + padding, 
                search_bar.width, 
                GetScreenHeight() - scroll_panel_area.y - padding, 
            };
            
            // the area of the content drawn in the window
            const int content_height = 100;
            const Rectangle content_area = {
                scroll_panel_area.x,
                scroll_panel_area.y,
                scroll_panel_area.width,
                content_height * (search_results.count ? search_results.count : SEARCH_ITEMS_PER_PAGE),
            };

            // the width of the scrollbar is only felt when it's visible
            const int SCROLLBAR_WIDTH = content_area.height > scroll_panel_area.height ? 14 : 2;
            
            GuiScrollPanel(scroll_panel_area, NULL, content_area, &scroll, &scrollView);

            // clip drawings within the scroll panel
            BeginScissorMode((scroll_panel_area.x + 1), (scroll_panel_area.y + 1), scroll_panel_area.width, (scroll_panel_area.height - 2));
                // the y value of the ith rectangle to be drawn
                float y_level = scroll_panel_area.y;
                
                // for every search result, draw a container and display its data
                int i = 0;
                for (YoutubeSearchNode* search_result = search_results.head; search_result; search_result = search_result->next, i++, y_level += content_height) {
                    // area of the ith rectangle
                    const Rectangle content_rect = { 
                        padding, 
                        y_level + scroll.y, // scroll is added so moving the scrollbar offsets all elements
                        scroll_panel_area.width - SCROLLBAR_WIDTH,
                        content_height 
                    };

                    const Vector2 mouse_position = GetMousePosition();
                    if (CheckCollisionPointRec(mouse_position, scroll_panel_area) && CheckCollisionPointRec(mouse_position, content_rect) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                        current_node = i;
                    }

                    // only process items that are onscreen
                    if (CheckCollisionRecs(content_rect, scroll_panel_area)) {
                        // if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(GetMousePosition(), content_rect)) {
                        //     if (current_node == i) {
                        //         if (current->type == CONTENT_TYPE_VIDEO) {
                        //             char watch_link[256] = "https://www.youtube.com/watch?v=";
                        //             strcat(watch_link, current->id);
                        //             MemoryBlock video_data = fetch_url(watch_link, curl);
                        //             if (is_memory_ready(video_data)) {
                        //                 create_file_from_memory("video.json", video_data.memory);
                        //                 unload_memory_block(&video_data);
                        //             }
                        //         }
                        //     }
                        //     else current_node = i;
                        // }

                        const Rectangle thumbnail_bounds = { 
                            content_rect.x, 
                            content_rect.y, 
                            content_rect.width * 0.45f, 
                            content_rect.height 
                        };

                        const Rectangle title_bounds = {
                            thumbnail_bounds.x + thumbnail_bounds.width,
                            content_rect.y,
                            content_rect.width - thumbnail_bounds.width,
                            content_rect.height * 0.75f
                        };

                        const Rectangle subtext_bounds = {
                            thumbnail_bounds.x + thumbnail_bounds.width,
                            title_bounds.y + title_bounds.height,
                            title_bounds.width,
                            content_rect.height - title_bounds.height
                        };

                        const int padding = 5;
                        const int font_size = 11;
                        const int spacing = 2;
                        const bool wrap_word = true; // words move to next line if there's enough space, rather than getting cut in half

                        Color background_color;
                        if (i == current_node) background_color = SKYBLUE;
                        else {
                            background_color = (i % 2) ? WHITE : RAYWHITE;
                        }

                        // content backgound
                        DrawRectangleRec(content_rect, background_color);
                        
                        // title
                        if (search_result->title)
                            DrawTextBoxed(FONT, search_result->title, padded_rectangle(padding, title_bounds), font_size, spacing, wrap_word, BLACK);

                        // thumbnail
                        if (IsTextureReady(search_result->thumbnail))
                            DrawTextureEx(search_result->thumbnail, (Vector2){ thumbnail_bounds.x, thumbnail_bounds.y }, 0.0f, 1.0f, RAYWHITE);

                        if (search_result->type == CONTENT_TYPE_VIDEO) {
                            if (search_result->date && search_result->views)    
                                DrawTextBoxed(FONT, TextFormat("%s - %s views", search_result->date, search_result->views), padded_rectangle(padding, subtext_bounds), font_size, spacing, wrap_word, BLACK);
                            draw_thumbnail_subtext(thumbnail_bounds, FONT, RAYWHITE, font_size, spacing, 5, search_result->length ? search_result->length : "LIVE");
                        }

                        else if (search_result->type == CONTENT_TYPE_CHANNEL) {
                            if (search_result->subs) 
                                DrawTextBoxed(FONT, search_result->subs, padded_rectangle(padding, subtext_bounds), font_size, spacing, wrap_word, BLACK);
                            
                            draw_thumbnail_subtext(thumbnail_bounds, FONT, RAYWHITE, font_size, spacing, padding, "CHANNEL");
                        }  

                        else if (search_result->type == CONTENT_TYPE_PLAYLIST) {
                            if (search_result->video_count) 
                                draw_thumbnail_subtext(thumbnail_bounds, FONT, RAYWHITE, font_size, spacing, padding, search_result->video_count);
                        }
                    }
                }
            EndScissorMode();
            
            DrawFPS(GetScreenWidth() - 70, GetScreenHeight() - 20);
        EndDrawing();
    }

    // deinit app
    {
        pthread_mutex_destroy(&thumbnail_list.mutex);
        if (search_results.count > 0) unload_list(&search_results);
        curl_global_cleanup();
        CloseWindow();
        return 0;
    }
}
// to do
    // show video information when double clicking video
    // actually play video when pressed
    // loading thumbnails halts the program
    // pagination 

// for read me
    // need curl, cjson, and raylib/raygui