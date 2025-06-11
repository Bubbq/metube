#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <curl/curl.h>
#include <cjson/cJSON.h>

#include "raylib.h"

struct MemoryBlock {
    size_t size;
    char* memory;
};

enum SearchResultType {
    VIDEO = 0,
    CHANNEL = 1,
    PLAYLIST = 2,
};

struct YoutubeSearchData {
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
};

struct YoutubeAPI {
    const char* key;
    const char* url;
    const char* video_endpoint;
    const char* search_endpoint;
    const char* channel_endpoint;
    const char* playlist_endpoint;
};

void unload_memory_block(struct MemoryBlock* mem)
{
    free(mem->memory);
    mem->size = 0;
}

// write sizeof(src) bytes to dst
size_t write_function(void* src, int nitems, size_t element_size, void* dst)
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
struct MemoryBlock http_get(const char* url)
{
    struct MemoryBlock chunk;

    // where the result is to be stored
    chunk.memory = NULL;
    
    // current size (in bytes)
    chunk.size = 0;
    
    // start the curl session
    curl_global_init(CURL_GLOBAL_ALL);

    CURL* curl_handle = curl_easy_init();

    // specifying parameters for easy curl handle
    curl_easy_setopt(curl_handle, CURLOPT_URL, url); // where to request data
    curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, write_function); // how to write requested data 
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

void get_youtube_search_results(int maxresults, const char* query, struct YoutubeAPI API)
{
    // construct the appropriate url for GET request
    char url[1024];
    snprintf(url, 1024, "%s/%s?part=snippet&q=%s&key=%s&maxResults=%d", API.url, API.search_endpoint, query, API.key, maxresults);

    // then, get the information of the search
    struct MemoryBlock chunk = http_get(url);
    if (chunk.size == 0) return;

    // // debugging
    // create_file("search_data.json", chunk.memory);
    
    // read information into json format
    cJSON* cjson = cJSON_Parse(chunk.memory);
    if (!cjson) {
        printf("Error: %s\n", cJSON_GetErrorPtr());
        cJSON_Delete(cjson);
        return;
    }

    // ptr to items fetched from GET request
    cJSON* items_tag = cJSON_GetObjectItem(cjson, "items");
    if (!items_tag) {
        printf("Error: %s\n", cJSON_GetErrorPtr());
        return;
    }

    const int nitems = cJSON_GetArraySize(items_tag);
    struct YoutubeSearchData* search_data = malloc((sizeof(struct YoutubeSearchData) * nitems));
    
    // the list of ids from search endpoint
    const int list_size = 512;

    char video_id_str[list_size];
    char channel_id_str[list_size];
    char playlist_id_str[list_size];

    // clear garbage values
    memset(video_id_str, 0, sizeof(video_id_str));
    memset(channel_id_str, 0, sizeof(channel_id_str));
    memset(playlist_id_str, 0, sizeof(playlist_id_str));

    for (int i = 0; i < nitems; i++) {
        // the ith item in 'items' object
        cJSON* subitem = cJSON_GetArrayItem(items_tag, i);
        
        // the 'id' tag of the ith item
        cJSON* id_tag = cJSON_GetObjectItem(subitem, "id");
        
        // the id of the search item (either video, playlist, or channel)
        cJSON* videoId_tag = cJSON_GetObjectItem(id_tag, "videoId");
        cJSON* channelId_tag = cJSON_GetObjectItem(id_tag, "channelId");
        cJSON* playlistId_tag = cJSON_GetObjectItem(id_tag, "playlistId");

        // append the ids to the proper list (comma delimited)
        if (videoId_tag) append_list_item(list_size, video_id_str, videoId_tag->valuestring, ",");
        else if (channelId_tag) append_list_item(list_size, channel_id_str, channelId_tag->valuestring, ",");
        else if (playlistId_tag) append_list_item(list_size, playlist_id_str, playlistId_tag->valuestring, ",");
    }

    // printf("%s\n", video_id_str);
    // printf("%s\n", channel_id_str);
    // printf("%s\n", playlist_id_str);

    // now get detailed information of the ids fetched from the GET request
    if (video_id_str[0] != '\0') {
        printf("video made\n");
        snprintf(url, sizeof(url), "%s/%s?part=snippet,statistics&id=%s&key=%s", API.url, API.video_endpoint, video_id_str, API.key);
        struct MemoryBlock video_chunk = http_get(url);
        // create_file("video_data.json", video_chunk.memory);
        unload_memory_block(&video_chunk);
    }

    if (channel_id_str[0] != '\0') {
        printf("channel made\n");
        snprintf(url, sizeof(url), "%s/%s?part=snippet,statistics&id=%s&key=%s", API.url, API.channel_endpoint, channel_id_str, API.key);
        struct MemoryBlock channel_chunk = http_get(url);
        // create_file("channel_data.json", channel_chunk.memory);
        unload_memory_block(&channel_chunk);
    }

    if (playlist_id_str[0] != '\0') {
        printf("playlist made\n");
        snprintf(url, sizeof(url), "%s/%s?part=snippet,statistics&id=%s&key=%s", API.url, API.playlist_endpoint, playlist_id_str, API.key);
        struct MemoryBlock playlist_chunk = http_get(url);
        // create_file("playlist_data.json", playlist_chunk.memory);
        unload_memory_block(&playlist_chunk);
    }

    unload_memory_block(&chunk);
    cJSON_Delete(cjson);
    free(search_data);
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

    const char* api_key = "AIzaSyA_rjuK_RqbCZpQSxGEDwNW4a8vRGy1tcY";

    // maximum nitems to fetch from a search
    const int max_results = 5;

    get_youtube_search_results(max_results, query, API);

    return 0;
}

// store the information of videos in the created struct