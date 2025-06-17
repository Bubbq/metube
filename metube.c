#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <curl/curl.h>
#include <cjson/cJSON.h>
#include "raylib.h"

// #define RAYGUI_IMPLEMENTATION
// #include "raygui.h"

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
    SEARCH_RESULT_VIDEO = 0,
    SEARCH_RESULT_CHANNEL = 1,
    SEARCH_RESULT_PLAYLIST = 2,
} SearchResultType;

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
    SearchResultType type;
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

// returns the json object that is an array of search result items (videos, channels, and/or playlists)
cJSON* get_search_items(cJSON *json)
{
    cJSON *contents = cJSON_GetObjectItem(json, "contents");
    cJSON *twoColumnSearchResultsRenderer = contents ? cJSON_GetObjectItem(contents, "twoColumnSearchResultsRenderer") : NULL;
    cJSON *primaryContents = twoColumnSearchResultsRenderer ? cJSON_GetObjectItem(twoColumnSearchResultsRenderer, "primaryContents") : NULL;
    cJSON *sectionListRenderer = primaryContents ? cJSON_GetObjectItem(primaryContents, "sectionListRenderer") : NULL;
    cJSON *sections = sectionListRenderer ? cJSON_GetObjectItem(sectionListRenderer, "contents") : NULL;

    if (sections && cJSON_IsArray(sections)) {
        cJSON *first_section = cJSON_GetArrayItem(sections, 0);
        cJSON *itemSectionRenderer = first_section ? cJSON_GetObjectItem(first_section, "itemSectionRenderer") : NULL;
        cJSON *contents = itemSectionRenderer ? cJSON_GetObjectItem(itemSectionRenderer, "contents") : NULL;
        if (contents && cJSON_IsArray(contents)) return contents;
    }

    return NULL;
}

// writes a list of search result nodes to some list
void get_results_from_query(const char* url_encoded_query, CURL *curl, YoutubeSearchList *search_results)
{
    // append the query to the yt query string
    char query_url[512] = "https://www.youtube.com/results?search_query=";
    strcat(query_url, url_encoded_query);
    
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
    cJSON *contents = get_search_items(search_json);
    if (contents && cJSON_IsArray(contents)) {
        // loop through every item and get the node equivalent 
        cJSON *item;
        cJSON_ArrayForEach (item, contents) {
            YoutubeSearchNode node = { 0 };
            cJSON *channelRenderer = cJSON_GetObjectItem(item, "channelRenderer");
            cJSON *videoRenderer = cJSON_GetObjectItem(item, "videoRenderer");
            cJSON *lockupViewModel = cJSON_GetObjectItem(item, "lockupViewModel");

            if (videoRenderer) {
                node.type = SEARCH_RESULT_VIDEO;

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
                node.type = SEARCH_RESULT_CHANNEL;

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
                node.type = SEARCH_RESULT_PLAYLIST;

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
    cJSON_Delete(search_json);
}

int main()
{
    // start the curl session
    CURL* curl = curl_easy_init();
    curl_global_init(CURL_GLOBAL_ALL);
    
    YoutubeSearchList search_results = create_youtube_search_list();
    char* query = curl_easy_escape(curl, "finding nemo", 0);
    get_results_from_query(query, curl, &search_results);
    
    print_list(&search_results);

    // dealloc app
    {
        // cleanup curl stuff
        curl_free(query); 
        curl_easy_cleanup(curl);
        curl_global_cleanup();

        // freeing all alloc'ed memory from nodes 
        unload_list(&search_results);
        return 0;
    }
}

// to do
    // search function
    // display information properly in raylib
    // pagination 

// for read me
    // need curl, cjson, and raylib