#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <curl/curl.h>
#include <cjson/cJSON.h>

#include "raylib.h"

#define DEBUG 

struct MemoryBlock {
    size_t size;
    char* memory;
};

enum SearchResultType {
    SEARCH_RESULT_VIDEO = 0,
    SEARCH_RESULT_CHANNEL = 1,
    SEARCH_RESULT_PLAYLIST = 2,
};

struct YoutubeSearchNode {
	char* id;
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

struct YoutubeSearchList {
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

bool is_memory_ready(struct MemoryBlock data)
{
    return ((data.size > 0) && (data.memory != NULL));
}

void unload_memory_block(struct MemoryBlock* memory_block)
{
    free(memory_block->memory);
    memory_block->size = 0;
}

struct YoutubeSearchList create_youtube_search_list() 
{
    struct YoutubeSearchList list;
    list.count = 0;
    list.head = list.tail = malloc(sizeof(struct YoutubeSearchNode));
    return list;
}

// appends node to end of the list
void add_node(struct YoutubeSearchList* list, struct YoutubeSearchNode node)
{
    // adding the first node to a list
    if (list->count == 0) *list->head = *list->tail = node;
    else {
        // allocate space for the new node
        list->tail->next = malloc(sizeof(struct YoutubeSearchNode));
        
        // append node to list
        *list->tail->next = node;
        
        // adjust tail ptr pos.
        list->tail = list->tail->next;
        list->tail->next = NULL;
    }
    
    // update list size
    list->count++;
}

void unload_node(struct YoutubeSearchNode* node)
{
    if (node->id) free(node->id);
    if (node->subs) free(node->subs);
    if (node->date) free(node->date);
    if (node->views) free(node->views);
    if (node->title) free(node->title);
    if (node->length) free(node->length);
    if (node->author) free(node->author);
    free(node);
}

void unload_list(struct YoutubeSearchList* list) 
{
    struct YoutubeSearchNode* prev = NULL;
    struct YoutubeSearchNode* current = list->head;

    while(current) {
        prev = current;
        current = current->next;
        unload_node(prev);
    }
}

void print_node(struct YoutubeSearchNode* node) 
{
    printf("id) %s title) %s author) %s subs) %s views) %s date) %s length) %s thumbnail id) %d type) %d\n", 
            node->id, node->title, node->author, node->subs, node->views, node->date, node->length, node->thumbnail.id, node->type);
}

void print_list(struct YoutubeSearchList* list)
{
    struct YoutubeSearchNode* current = list->head;
    while (current) {
        print_node(current);
        current = current->next;
    }
}

// append src to dst
size_t write_data(void* src, int nitems, size_t element_size, void* dst)
{
    // first, we to know how many bytes we are appending to dst
    // becuase src is generic, we dont know the type (cant use sizeof(*(src_type)src))
    size_t src_size = (nitems * element_size);

    // next, we need to resize the dst pointer to fit this new data
    // first, find out how much memory we are currenly holding
    struct MemoryBlock* mem = (struct MemoryBlock*) dst;

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
struct MemoryBlock fetch_url(const char* url, CURL* curl_handle)
{
    struct MemoryBlock chunk;

    // where the result is to be stored
    chunk.memory = NULL;
    
    // current size (in bytes)
    chunk.size = 0;

    // specify parameters for curl handle
    curl_easy_setopt(curl_handle, CURLOPT_URL, url); // where to request data
    curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, write_data); // how to write requested data 
    curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)&chunk); // where to write requested data

    // store the result of the GET request
    CURLcode res = curl_easy_perform(curl_handle);

    if (res != CURLE_OK) {
        fprintf(stderr, "curl_easy_perform() failed: %s\n",
                curl_easy_strerror(res));
    }
        
    return chunk;
}

void create_file_from_memory(const char* filename, const char* data) 
{
    FILE* fp = fopen(filename, "w");
    if (!fp) printf("could not write \"%s\" in write mode\n", filename);
    else {
        fprintf(fp, "%s", data);
        fclose(fp);
    } 
}

// appends the nth element encountered by a list seperated by some delimeter
void append_string_item(int element_count, int maxlen, char list_item[maxlen], const char* element, const char* delim)
{
    bool add_delim = element_count > 0;
    size_t newlen = strlen(list_item) + strlen(element) + (add_delim ? strlen(delim) : 0);

    if (newlen < maxlen) {
        if (add_delim) strcat(list_item, delim);
        strcat(list_item, element);
    }
}

// given a json item, return the node equivalent 
struct YoutubeSearchNode create_node(cJSON* item, enum SearchResultType type)
{
    struct YoutubeSearchNode node = { 0 };
    
    // id
    cJSON* id = cJSON_GetObjectItem(item, "id");
    if (id) {
        node.id = malloc(strlen(id->valuestring) + 1);
        strcpy(node.id, id->valuestring);
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

    cJSON* statistics = cJSON_GetObjectItem(item, "statistics");
    if (statistics) {
        // view count
        cJSON* viewCount = cJSON_GetObjectItem(statistics, "viewCount");
        if (viewCount) {
            node.views = malloc(strlen(viewCount->valuestring) + 1);
            strcpy(node.views, viewCount->valuestring);
        }

        // sub count
        cJSON* subscriberCount = cJSON_GetObjectItem(statistics, "subscriberCount");
        if (subscriberCount) {
            node.subs = malloc(strlen(subscriberCount->valuestring) + 1);
            strcpy(node.subs, subscriberCount->valuestring);
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

// convert the content of a fetched url to a json object
cJSON* api_to_json(const char* url, CURL* curl_handle, const char* debug_filename)
{
    // the data fetched from the URL
    struct MemoryBlock fetched = fetch_url(url, curl_handle);
    if (!is_memory_ready(fetched)) return NULL;

    // print fetched data to better understand future cJSON opertations
    #ifdef DEBUG
        create_file_from_memory(debug_filename, fetched.memory);
    #endif

    // the json obj of this data
    // used to preform CRUD operations of .json files
    cJSON* json = cJSON_Parse(fetched.memory);
    if (!json) {
        printf("Error: %s\n", cJSON_GetErrorPtr());
        cJSON_Delete(json);
        return NULL;
    }

    // dealloc unused memory
    unload_memory_block(&fetched);
    
    return json;
}

void add_nodes_to_list(const char* url, CURL* curl_handle, const char* debug_filename, enum SearchResultType type, struct YoutubeSearchList* list)
{
    // get api infomation as a json object
    cJSON* json = api_to_json(url, curl_handle, debug_filename);
    if (!json) return;

    // the 'items' element in the json object
    cJSON* items = cJSON_GetObjectItem(json, "items");
    if (!items) {
        printf("Error: %s\n", cJSON_GetErrorPtr());
        return;
    }

    // for every item, store relavent information in the list
    int nitems = cJSON_GetArraySize(items);
    
    for (int i = 0; i < nitems; i++) {
        // the ith item
        cJSON* item = cJSON_GetArrayItem(items, i);
        struct YoutubeSearchNode node = create_node(item, type);
        add_node(list, node);
    }

    // delloc unused memory
    cJSON_Delete(json);
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
        cJSON* channelId = cJSON_GetObjectItem(id, "channelId");
        // cJSON* playlistId = cJSON_GetObjectItem(id, "playlistId");

        // append the id to its proper list
        if (videoId) append_string_item(i, maxlen, videoIDs, videoId->valuestring, ",");
        else if (channelId) append_string_item(i, maxlen, channelIDs, channelId->valuestring, ",");
        // else if (playlistId) append_string_item(i, playlistIDs, playlistId->valuestring, ",");
    }
}

// return a list of results generated from a query
struct YoutubeSearchList metube_search(int maxresults, const char* query, struct YoutubeAPI API, CURL* curl_handle)
{
    // list holding all search data information (to be returned)
    struct YoutubeSearchList list = create_youtube_search_list();
    
    // format the url for the YouTube Data API search endpoint using the query string
    char url[1024];
    snprintf(url, 1024, "%s/%s?part=snippet&q=%s&key=%s&maxResults=%d", API.url, API.search_endpoint, query, API.key, maxresults);

    // get api infomation as a json object
    cJSON* search_json = api_to_json(url, curl_handle, "search.json");
    if (!search_json) return (struct YoutubeSearchList) { 0 };

    // tag that is a list of the items returned from GET request
    cJSON* items = cJSON_GetObjectItem(search_json, "items");
    if (!items) {
        printf("Error: %s\n", cJSON_GetErrorPtr());
        return (struct YoutubeSearchList) { 0 };
    }

    char videoIDs[512] = { 0 };
    char channelIDs[512] = { 0 };
    char playlistIDs[512] = { 0 };

    // get comma-delimited string of ids of all types for bulk API calls
    get_ids(items, 512, videoIDs, channelIDs, playlistIDs);
    
    // done with this json obj
    cJSON_Delete(search_json);

    // store detailed video information into a list of youtube search data
    if (videoIDs[0] != '\0') {
        // create the url containing all ids of the corresp. type
        snprintf(url, sizeof(url), "%s/%s?part=snippet,statistics,contentDetails&id=%s&key=%s", API.url, API.video_endpoint, videoIDs, API.key);
        
        // add all items fetched from this url to the list of search results
        add_nodes_to_list(url, curl_handle, "video.json", SEARCH_RESULT_VIDEO, &list);
    }

    if (channelIDs[0] != '\0') {
        snprintf(url, sizeof(url), "%s/%s?part=snippet,statistics&id=%s&key=%s", API.url, API.channel_endpoint, channelIDs, API.key);
        add_nodes_to_list(url, curl_handle, "channel.json", SEARCH_RESULT_CHANNEL, &list);
    }

    // TODO: add playlist support!

    return list;
}

int main(int argc, char** argv)
{
    if (argc != 2) {
        printf("Invalid number of arguments.\nPlease provide your YouTube API key after the executable. (ie ./metube \"YOUR_API_KEY\")\n");
        return 1;
    }

    struct YoutubeAPI API;
    // AIzaSyA_rjuK_RqbCZpQSxGEDwNW4a8vRGy1tcY
    API.key = argv[1];
    API.url = "https://www.googleapis.com/youtube/v3";
    API.video_endpoint = "videos";
    API.search_endpoint = "search"; 
    API.channel_endpoint = "channels";
    API.playlist_endpoint = "playlistItems";
    
    // start the curl session
    curl_global_init(CURL_GLOBAL_ALL);
    
    CURL* curl_handle = curl_easy_init();
    
    // must be URL encoded
    const char* query = "xqc";
    
    const int max_results = 5;
    
    struct YoutubeSearchList list = metube_search(max_results, query, API, curl_handle);
    print_list(&list);
    
    // dealloc app
    {
        // cleanup curl stuff 
        curl_easy_cleanup(curl_handle);
        curl_global_cleanup();
        unload_list(&list);
        return 0;
    }
}

// to do
    // display information properly in raylib
    // do playlists


// for read me
    // need curl, cjson, and raylib
    // need to pass your youtube api key as an argument to the program