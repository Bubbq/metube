#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <curl/curl.h>
#include <cjson/cJSON.h>
#include "raylib.h"

#define RAYGUI_IMPLEMENTATION
#include "raygui.h"

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

void configure_search_url(const int maxlen, char search_url[maxlen], const char* query, const SortParameter sort_param, const ContentType content_param)
{
    snprintf(search_url, maxlen, "https://www.youtube.com/results?search_query=%s&sp=", query);

    // possible sorting params
    const char* relevance = "CAA";
    const char* upload_date = "CAI";
    const char* popularity = "CAM";
    const char* rating = "CAE";
    
    switch (sort_param) {
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

    switch (content_param) {
        case CONTENT_TYPE_VIDEO: strcat(search_url, video); break;
        case CONTENT_TYPE_CHANNEL: strcat(search_url, channel); break;
        case CONTENT_TYPE_PLAYLIST: strcat(search_url, playlist); break;
        case CONTENT_TYPE_ANY: strcat(search_url, none); break;
    }  
}

// writes a list of search result nodes to some list
void get_results_from_query(const char* url_encoded_query, CURL *curl, YoutubeSearchList *search_results, const SortParameter sort_param, const ContentType content_param)
{
    // append the query to the yt query string
    char query_url[512] = "\0";
    configure_search_url(512, query_url, url_encoded_query, sort_param, content_param);
    // printf("%s\n", query_url);

    // get the page source of this url
    MemoryBlock html = fetch_url(query_url, curl);
    if (!is_memory_ready(html)) return;

    // extract search result data
    char* cjson_data = extract_yt_data(html.memory);
    unload_memory_block(&html);
    if (!cjson_data) return;

    // get json obj
    cJSON* search_json = cJSON_Parse(cjson_data);
    free(cjson_data);
    if (!search_json) {
        printf("Error: %s\n", cJSON_GetErrorPtr());
        return;
    }

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
                    cJSON* thumbnail = cJSON_GetObjectItem(videoRenderer, "thumbnail");
                    cJSON* thumbnails = thumbnail ? cJSON_GetObjectItem(thumbnail, "thumbnails") : NULL;
                    if(thumbnails && cJSON_IsArray(thumbnails)) {
                        cJSON* first_thumbnail = cJSON_GetArrayItem(thumbnails, 0);
                        cJSON *url = cJSON_GetObjectItem(first_thumbnail, "url");
                        // TODO: make image from thumbnail url
                        if (url && cJSON_IsString(url)) 
                            ;
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
                    if (viewCountText && cJSON_IsString(viewCountText)) node.views = strdup(viewCountText->valuestring);

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
                        // TODO: add url to image (need to add 'http:')
                        if(url && cJSON_IsString(url)) 
                            ;
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

                    // number of videos in playlist
                    cJSON *contentImage = cJSON_GetObjectItem(lockupViewModel, "contentImage");
                    cJSON *collectionThumbnailViewModel = contentImage ? cJSON_GetObjectItem(contentImage, "collectionThumbnailViewModel") : NULL;
                    cJSON *primaryThumbnail = collectionThumbnailViewModel ? cJSON_GetObjectItem(collectionThumbnailViewModel, "primaryThumbnail") : NULL;
                    cJSON *thumbnailViewModel = primaryThumbnail ? cJSON_GetObjectItem(primaryThumbnail, "thumbnailViewModel") : NULL;
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

                if (node.id) add_node(search_results, node);
            }
        }
    }
    cJSON_Delete(search_json);
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

void search (const char* query, CURL* curl, YoutubeSearchList *search_results, const SortParameter sort_parameter, ContentType content_type)
{
    // curl only accepts url encoded queries
    char* url_encoded_query = curl_easy_escape(curl, query,0);
    
    // store the data of the search results to some list
    get_results_from_query(url_encoded_query, curl, search_results, sort_parameter, content_type);
    print_list(search_results);

    // freeing alloc'ed memory
    curl_free(url_encoded_query);
    unload_list(search_results);
}

int main()
{
    // start the curl session
    CURL* curl = curl_easy_init();
    curl_global_init(CURL_GLOBAL_ALL);
    
    YoutubeSearchList search_results = create_youtube_search_list();
    
    // init app
    SetTargetFPS(60);
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    SetConfigFlags(FLAG_WINDOW_ALWAYS_RUN);
    InitWindow(1000, 750, "metube");

    char buffer[256] = "\0";
    bool edit_mode = false;
    
    bool show_filter_window = false;
    int current_type= 0;
    int current_sort = 0;
    ContentType content[] = { CONTENT_TYPE_ANY, CONTENT_TYPE_VIDEO, CONTENT_TYPE_CHANNEL, CONTENT_TYPE_PLAYLIST };
    SortParameter sort[] = { SORT_PARAM_RELEVANCE, SORT_PARAM_UPLOAD_DATE, SORT_PARAM_VIEW_COUNT, SORT_PARAM_RATING };

    while (!WindowShouldClose()) {
        BeginDrawing();
            ClearBackground(RAYWHITE);

            // searchbar
            const Rectangle search_bar = { 5, 5, 300, 25 };
            int text_box_status; 
            if ((text_box_status = GuiTextBox(search_bar, buffer, 256, edit_mode))) {
                edit_mode = !edit_mode;
                const size_t query_len = strlen(buffer);
                const bool ENTER_key_pressed = (text_box_status == 1);

                if ((edit_mode == false) && (query_len > 0) && ENTER_key_pressed) search(buffer, curl, &search_results, sort[current_sort], content[current_type]);
            }

            // filter button
            const Rectangle filter_button = { (search_bar.x + search_bar.width + 5), 5, 25, 25 };
            if (GuiButton(filter_button, "F")) show_filter_window = !show_filter_window;
            if (show_filter_window) {
                const int font = 11;
                const Rectangle filter_window_area = { 5, (search_bar.y + search_bar.height + 5), search_bar.width, 75 };

                // buttons to switch filter params (the type of content and how they will be sorted)
                const char* button_text = "SWITCH";
                const Rectangle sort_type_button = { (filter_window_area.x + filter_window_area.width - 55), (filter_window_area.y + 5), 50, 17.5 };
                const Rectangle content_type_button = { (filter_window_area.x + filter_window_area.width - 55), (sort_type_button.y + sort_type_button.height + 10), 50, 17.5 };
                
                // update the index of filter params when pressed
                if (GuiButton(sort_type_button, button_text)) current_sort = bound_index_to_array((current_sort + 1), 4);
                if (GuiButton(content_type_button, button_text)) current_type = bound_index_to_array((current_type + 1), 4);

                // filters availible
                DrawText("ORDER:", (filter_window_area.x + 5), (sort_type_button.y + 5), font, BLACK);
                DrawText("TYPE:", (filter_window_area.x + 5), (content_type_button.y + 5), font, BLACK);
                
                // current param value
                DrawText(content_type_to_text(content[current_type]), ((filter_window_area.x + filter_window_area.width) * 0.4f), (content_type_button.y + 5), font, BLACK);
                DrawText(sort_parameter_to_text(sort[current_sort]), ((filter_window_area.x + filter_window_area.width) * 0.4f), (sort_type_button.y + 5), font, BLACK);
            }

            DrawFPS(GetScreenWidth() - 70, GetScreenHeight() - 20);
        EndDrawing();
    }

    // deinit app
    {
        curl_easy_cleanup(curl);
        curl_global_cleanup();
        CloseWindow();
        return 0;
    }
}

// to do
    // display searched information
    // pagination 

// for read me
    // need curl, cjson, and raylib/raygui