#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <curl/curl.h>
#include <cjson/cJSON.h>

#include "raylib.h"

#define DEBUG 

struct Data {
    size_t size;
    char* memory;
};

enum SearchResultType {
    VIDEO = 0,
    CHANNEL = 1,
    PLAYLIST = 2,
};

struct YoutubeSearchNode {
	char* id;
	char* link;
	char* title;
	char* author;
	char* subs;
	char* views;
	char* date;
	char* length;
    Texture thumbnail;
    enum SearchResultType type;
    struct YoutubeSearchNode* next;
};

struct YoutubeSearchList 
{
    size_t count;
    struct YoutubeSearchNode* head;
    struct YoutubeSearchNode* tail;
};

struct YoutubeAPI {
    char* key;
    char* url;
    char* video_endpoint;
    char* search_endpoint;
    char* channel_endpoint;
    char* playlist_endpoint;
};

bool data_ready(struct Data data)
{
    return ((data.size > 0) && (data.memory != NULL));
}

void unload_data(struct Data* mem)
{
    free(mem->memory);
    mem->size = 0;
}

struct YoutubeSearchList create_youtube_search_list() 
{
    struct YoutubeSearchList list;
    list.count = 0;
    list.head = list.tail = malloc(sizeof(struct YoutubeSearchNode));
    return list;
}

void add_node(struct YoutubeSearchList* list, struct YoutubeSearchNode node)
{
    // adding the first node to a list
    if (list->count == 0) (*list->head) = (*list->tail) = node;
    else {
        // allocate space for the new node
        list->tail->next = malloc(sizeof(struct YoutubeSearchNode));
        
        // append node to list
        *list->tail->next = node;
        
        // adjust tail ptr pos.
        list->tail = list->tail->next;
        list->tail->next = NULL;
    }
    
    list->count++;
}

void unload_node(struct YoutubeSearchNode* node)
{
    if (node->id) free(node->id);
    if (node->link) free(node->link);
    if (node->subs) free(node->subs);
    if (node->date) free(node->date);
    if (node->views) free(node->views);
    if (node->title) free(node->title);
    if (node->length) free(node->length);
    if (node->author) free(node->author);
    free(node);
}

void unload_youtube_search_list(struct YoutubeSearchList* list) 
{
    struct YoutubeSearchNode* prev = NULL;
    struct YoutubeSearchNode* current = list->head;

    while(current) {
        prev = current;
        current = current->next;
        unload_node(prev);
    }
}

void print_youtube_search_data(struct YoutubeSearchNode* node) 
{
    printf("id) %s link) %s title) %s author) %s subs) %s views) %s date) %s length) %s thumbnail id) %d type) %d\n", 
            node->id, node->link, node->title, node->author, node->subs, node->views, node->date, node->length, node->thumbnail.id, node->type);
}

// write sizeof(src) bytes to dst
size_t write_data(void* src, int nitems, size_t element_size, void* dst)
{
    // first, we to know how many bytes we are appending to dst
    // becuase src is generic, we dont know the type (cant use sizeof(*(src_type)src))
    size_t src_size = (nitems * element_size);

    // next, we need to resize the dst pointer to fit this new data
    // first, find out how much memory we are currenly holding
    struct Data* mem = (struct Data*) dst;

    // now get the new size, '+1' for '/0'
    char* new_memory = realloc(mem->memory, (mem->size + src_size + 1));
    if (!new_memory) {
        printf("not enough memory, realloc returned null\n");
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
struct Data http_get(const char* url)
{
    struct Data chunk;

    // where the result is to be stored
    chunk.memory = NULL;
    
    // current size (in bytes)
    chunk.size = 0;
    
    // start the curl session
    curl_global_init(CURL_GLOBAL_ALL);

    CURL* curl_handle = curl_easy_init();

    // specifying parameters for easy curl handle
    curl_easy_setopt(curl_handle, CURLOPT_URL, url); // where to request data
    curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, write_data); // how to write requested data 
    curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)&chunk); // where to write requested data

    // store the result of the GET request
    CURLcode res = curl_easy_perform(curl_handle);

    if (res != CURLE_OK) {
        fprintf(stderr, "curl_easy_perform() failed: %s\n",
                curl_easy_strerror(res));
    }

    // cleanup curl stuff 
    curl_easy_cleanup(curl_handle);

    // end the curl session
    curl_global_cleanup();
    
    return chunk;
}

void create_file(const char* filename, const char* filedata) 
{
    FILE* fp = fopen(filename, "w");
    if (!fp) printf("could not write \"%s\" in write mode\n", filename);
    else fprintf(fp, "%s", filedata);
    fclose(fp);
}

// appends string 'b' to a list item string seperated by some delimiter
void append_list_item(int maxlen, char list_item[maxlen], const char* b, const char* delim)
{
    // current length of the dst string
    const size_t len = strlen(list_item);
    
    // only insert the delim prior to the new item if the list is not empty
    const bool list_empty = (len == 0);
    snprintf((list_item + len), (maxlen - len), "%s%s", (list_empty ? "" : delim), b);
}

// given a json object, return the YoutubeSearchNode equivalent 
struct YoutubeSearchNode create_node(cJSON* item, enum SearchResultType type)
{
    struct YoutubeSearchNode node = { 0 };
    
    cJSON* id = cJSON_GetObjectItem(item, "id");
    if (id) {
        // id
        node.id = malloc(strlen(id->valuestring) + 1);
        strcpy(node.id, id->valuestring);

        // link
        char youtube_link[256];
        snprintf(youtube_link, 256, "https://www.youtube.com/watch?v=%s", node.id);
        node.link = malloc(strlen(youtube_link) + 1);
        strcpy(node.link, youtube_link);
    }

    cJSON* snippet = cJSON_GetObjectItem(item, "snippet");
    if (snippet) {
        // title
        cJSON* title = cJSON_GetObjectItem(snippet, "title");
        if (title) {
            node.title = malloc(strlen(title->valuestring) + 1);
            strcpy(node.title, title->valuestring);
        }

        // author
        cJSON* channelTitle = cJSON_GetObjectItem(snippet, "channelTitle");
        if (channelTitle) {
            node.author = malloc(strlen(channelTitle->valuestring) + 1);
            strcpy(node.author, channelTitle->valuestring);
        }

        // publish date
        cJSON* publishedAt = cJSON_GetObjectItem(snippet, "publishedAt");
        if (publishedAt) {
            node.date = malloc(strlen(publishedAt->valuestring) + 1);
            strcpy(node.date, publishedAt->valuestring);
        }
    }

    // view count
    cJSON* statistics = cJSON_GetObjectItem(item, "statistics");
    if (statistics) {
        cJSON* viewCount = cJSON_GetObjectItem(statistics, "viewCount");
        if (viewCount) {
            node.views = malloc(strlen(viewCount->valuestring) + 1);
            strcpy(node.views, viewCount->valuestring);
        }
    }
    
    // length
    cJSON* contentDetails = cJSON_GetObjectItem(item, "contentDetails");
    if (contentDetails) {
        cJSON* duration = cJSON_GetObjectItem(contentDetails, "duration");
        if (duration) {
            node.length = malloc(strlen(duration->valuestring) + 1);
            strcpy(node.length, duration->valuestring);
        }
    }

    node.type = type;

    return node;
}

// writes the video, channel, and playlist ids into a comma delimited string
void get_ids(cJSON* items, int maxlen, char videoIDs[maxlen], char channelIDs[maxlen], char playlistIDs[maxlen])
{
    const int nitems = cJSON_GetArraySize(items);
    
    for (int i = 0; i < nitems; i++) {
        // the ith item in 'items' object
        cJSON* subitem = cJSON_GetArrayItem(items, i);
        
        // the 'id' tag of the ith item
        cJSON* id = cJSON_GetObjectItem(subitem, "id");
        
        // the id of the search item (either video, playlist, or channel)
        cJSON* videoId = cJSON_GetObjectItem(id, "videoId");

        // append the id to its proper list
        if (videoId) append_list_item(512, videoIDs, videoId->valuestring, ",");
    }
}

struct YoutubeSearchList metube_search(int maxresults, const char* query, struct YoutubeAPI API)
{
    // list holding all search data information (to be returned)
    struct YoutubeSearchList list = create_youtube_search_list();
    
    // format the url for the YouTube Data API search endpoint using the query string
    char url[1024];
    snprintf(url, 1024, "%s/%s?part=snippet&q=%s&key=%s&maxResults=%d", API.url, API.search_endpoint, query, API.key, maxresults);

    // get Youtube API information
    struct Data chunk = http_get(url);
    if (!data_ready(chunk)) return (struct YoutubeSearchList) { 0 };

    // display search results
    #ifdef DEBUG
        create_file("search_data.json", chunk.memory);
    #endif

    // write search results into cJSON object to easily preform CRUD operations (specifically 'R')
    cJSON* search_json = cJSON_Parse(chunk.memory);
    if (!search_json) {
        printf("Error: %s\n", cJSON_GetErrorPtr());
        cJSON_Delete(search_json);
        return (struct YoutubeSearchList) { 0 };
    }

    // tag that is a list of the items returned from GET request
    cJSON* items = cJSON_GetObjectItem(search_json, "items");
    if (!items) {
        printf("Error: %s\n", cJSON_GetErrorPtr());
        return (struct YoutubeSearchList) { 0 };
    }

    // get comma-delimited string of ids for bulk API calls
    char videoIDs[512] = { 0 };
    char channelIDs[512] = { 0 };
    char playlistIDs[512] = { 0 };

    get_ids(items, 512, videoIDs, channelIDs, playlistIDs);
    
    // store detailed video information into a list of youtube search data
    if (videoIDs[0] != '\0') {
        // create the url containing all video ids
        snprintf(url, sizeof(url), "%s/%s?part=snippet,statistics,contentDetails&id=%s&key=%s", API.url, API.video_endpoint, videoIDs, API.key);
        
        // use this url in the GET request
        struct Data video_chunk = http_get(url);
        if (!data_ready(video_chunk)) return (struct YoutubeSearchList) { 0 };

        #ifdef DEBUG
            create_file("video_data.json", chunk.memory);
        #endif

        // json object of video data GET request
        cJSON* video_json = cJSON_Parse(video_chunk.memory);
        if (!video_json) {
            printf("Error: %s\n", cJSON_GetErrorPtr());
            cJSON_Delete(video_json);
            return (struct YoutubeSearchList) { 0 };
        }

        // dont need chunk anymore
        unload_data(&video_chunk);

        // array of video items in json file
        cJSON* items = cJSON_GetObjectItem(video_json, "items");
        if (!items) {
            printf("Error: %s\n", cJSON_GetErrorPtr());
            return (struct YoutubeSearchList) { 0 };
        }
        
        // for every item, store relavent information in list above
        const int nitems = cJSON_GetArraySize(items);
        
        for(int i = 0; i < nitems; i++) {
            // ptr to the ith item
            cJSON* subitem = cJSON_GetArrayItem(items, i);
            
            struct YoutubeSearchNode node = create_node(subitem, VIDEO);
            
            add_node(&list, node);
        }

        // dont need inital search data anymore
        unload_data(&chunk);
        cJSON_Delete(video_json);
    }

    cJSON_Delete(search_json);

    return list;
}

int main()
{
    struct YoutubeAPI API;
    API.key = "AIzaSyA_rjuK_RqbCZpQSxGEDwNW4a8vRGy1tcY";
    API.url = "https://www.googleapis.com/youtube/v3";
    API.video_endpoint = "videos";
    API.search_endpoint = "search"; 
    API.channel_endpoint = "channels";
    API.playlist_endpoint = "playlists";

    // must be URL encoded
    const char* query = "xqc";
    const int max_results = 5;
    struct YoutubeSearchList list = metube_search(max_results, query, API);

    struct YoutubeSearchNode* current = list.head;
    while(current) {
        print_youtube_search_data(current);
        current = current->next;
    }

    unload_youtube_search_list(&list);
    return 0;
}

// store the information of videos, playlists and channels in the created struct
// only one curl session for the entire program