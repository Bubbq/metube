#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <curl/curl.h>
#include <cjson/cJSON.h>
#include "api.h"
#include "list.h"
#include "raylib.h"

// given a json item, return the node equivalent 
YoutubeSearchNode create_node(const cJSON* item, const SearchResultType type)
{
    YoutubeSearchNode node = { 0 };
    if (!item) return node;

    // id
    cJSON* id = cJSON_GetObjectItem(item, "id");
    if (id && cJSON_IsString(id)) node.id = strdup(id->valuestring);

    cJSON* snippet = cJSON_GetObjectItem(item, "snippet");
    if (snippet) {
        // title
        cJSON* title = cJSON_GetObjectItem(snippet, "title");
        if (title && cJSON_IsString(title)) node.title = strdup(title->valuestring);

        // author
        cJSON* channelTitle = cJSON_GetObjectItem(snippet, "channelTitle");
        if (channelTitle && cJSON_IsString(channelTitle)) node.author = strdup(channelTitle->valuestring);

        // publish date
        cJSON* publishedAt = cJSON_GetObjectItem(snippet, "publishedAt");
        if (publishedAt && cJSON_IsString(publishedAt)) node.date = strdup(publishedAt->valuestring);
    }

    cJSON* statistics = cJSON_GetObjectItem(item, "statistics");
    if (statistics) {
        // view count
        cJSON* viewCount = cJSON_GetObjectItem(statistics, "viewCount");
        if (viewCount && cJSON_IsString(viewCount)) node.views = strdup(viewCount->valuestring);

        // sub count
        cJSON* subscriberCount = cJSON_GetObjectItem(statistics, "subscriberCount");
        if (subscriberCount && cJSON_IsString(subscriberCount)) node.subs = strdup(subscriberCount->valuestring);
    }
    
    cJSON* contentDetails = cJSON_GetObjectItem(item, "contentDetails");
    if (contentDetails) {
        // video length
        cJSON* duration = cJSON_GetObjectItem(contentDetails, "duration");
        if (duration && cJSON_IsString(duration)) node.length = strdup(duration->valuestring);

        // amount of playlist items (videos)
        cJSON* itemCount = cJSON_GetObjectItem(contentDetails, "itemCount");
        if (itemCount && cJSON_IsNumber(itemCount)) node.video_count = itemCount->valueint;
    }

    node.type = type;

    return node;
}

void add_nodes_to_list(const char* url, CURL* curl_handle, const char* debug_filename, const SearchResultType type, YoutubeSearchList* list)
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
        YoutubeSearchNode node = create_node(item, type);
        add_node(list, node);
    }

    // delloc unused memory
    cJSON_Delete(json);
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

// writes the video, channel, and playlist ids into a comma delimited string
void get_ids(const cJSON* items, const int maxlen, char videoIDs[maxlen], char channelIDs[maxlen], char playlistIDs[maxlen])
{
    const int nitems = cJSON_GetArraySize(items);
    
    for (int i = 0; i < nitems; i++) {
        // the ith item in 'items' object
        cJSON* item = cJSON_GetArrayItem(items, i);
        
        // the 'id' tag of the ith item
        cJSON* id = cJSON_GetObjectItem(item, "id");
        
        // the id of the search item (either video, playlist, or channel)
        cJSON* videoId = cJSON_GetObjectItem(id, "videoId");
        cJSON* channelId = cJSON_GetObjectItem(id, "channelId");
        cJSON* playlistId = cJSON_GetObjectItem(id, "playlistId");

        // append the id to its proper list
        if (videoId) append_string_item(i, maxlen, videoIDs, videoId->valuestring, ",");
        else if (channelId) append_string_item(i, maxlen, channelIDs, channelId->valuestring, ",");
        else if (playlistId) append_string_item(i, maxlen, playlistIDs, playlistId->valuestring, ",");
    }
}

// return a list of results generated from a query
YoutubeSearchList metube_search(const int maxresults, const char* query, const YoutubeAPI API, CURL* curl_handle)
{
    // list holding all search data information (to be returned)
    YoutubeSearchList list = create_youtube_search_list();
    
    // format the url for the YouTube Data API search endpoint using the query string
    char url[1024];
    snprintf(url, 1024, "%s/%s?part=snippet&q=%s&key=%s&maxResults=%d", API.url, API.search_endpoint, query, API.key, maxresults);

    // get api infomation as a json object
    cJSON* search_json = api_to_json(url, curl_handle, "search.json");
    if (!search_json) return (YoutubeSearchList) { 0 };

    // tag that is a list of the items returned from GET request
    cJSON* items = cJSON_GetObjectItem(search_json, "items");
    if (!items) {
        printf("Error: %s\n", cJSON_GetErrorPtr());
        return (YoutubeSearchList) { 0 };
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

    // do the same for other search result mediums
    if (channelIDs[0] != '\0') {
        snprintf(url, sizeof(url), "%s/%s?part=snippet,statistics&id=%s&key=%s", API.url, API.channel_endpoint, channelIDs, API.key);
        add_nodes_to_list(url, curl_handle, "channel.json", SEARCH_RESULT_CHANNEL, &list);
    }

    if (playlistIDs[0] != '\0') {
        snprintf(url, sizeof(url), "%s/%s?part=snippet,contentDetails&id=%s&key=%s", API.url, API.playlist_endpoint, playlistIDs, API.key);
        add_nodes_to_list(url, curl_handle, "playlist.json", SEARCH_RESULT_PLAYLIST, &list);
    }

    return list;
}

int main(int argc, char** argv)
{
    if (argc != 2) {
        printf("Invalid number of command arguments.\nPlease provide your YouTube API key after the executable. (ie ./metube \"YOUR_API_KEY\")\n");
        return 1;
    }

    const YoutubeAPI API = init_youtube_api(argv[1]);
    
    // start the curl session
    curl_global_init(CURL_GLOBAL_ALL);
    
    CURL* curl_handle = curl_easy_init();
    
    // must be URL encoded
    char* query = curl_easy_escape(curl_handle, "eldin ring playthough", 0);
    
    const int max_results = 5;
    
    YoutubeSearchList list = metube_search(max_results, query, API, curl_handle);
    print_list(&list);
    
    // dealloc app
    {
        // cleanup curl stuff
        curl_free(query); 
        curl_easy_cleanup(curl_handle);
        curl_global_cleanup();
        unload_list(&list);
        return 0;
    }
}

// to do
    // display information properly in raylib
    // search function
    // pagination support

// for read me
    // need curl, cjson, and raylib
    // need to pass your youtube api key as an argument to the program