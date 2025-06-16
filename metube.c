#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <curl/curl.h>
#include <cjson/cJSON.h>
#include "api.h"
#include "list.h"
// #include "raylib.h"

// #define RAYGUI_IMPLEMENTATION
// #include "raygui.h"

// extracts the yt data from the search result page's html
char* extract_ytInitalData(const char* html) 
{
    const char* needle = "ytInitialData = ";

    // the beginning of the data 
    const char* needle_loc = strstr(html, needle);
    if (!needle_loc) return NULL;
    
    // starts from the opening '{'
    const char* start = (needle_loc + strlen(needle));

    // ends at the closing "}"
    const char* end = strstr(start, "};");
    if (!end) return NULL;

    // return a duplicate of this section of data found in the html
    const size_t len = end - start + 1;
    char* ret = malloc(len + 1); // for null terminator
    strncpy(ret, start, len);
    ret[len] = '\0';

    return ret;
}

// writes a list of search result nodes to list
void metube_search_results(const char* url_encoded_query, CURL *handle, YoutubeSearchList *search_results)
{
    // first, format the url that will be fetched
    char url[512];
    snprintf(url, 512, "https://www.youtube.com/results?search_query=%s", url_encoded_query);

    // then, fetch the page source of this url
    MemoryBlock search_chunk = fetch_url(url, handle);
    // create_file_from_memory("test.json", search_chunk);

    if (!is_memory_ready(search_chunk)) return;

    char* cjson_data = extract_ytInitalData(search_chunk.memory);
    if (!cjson_data) return;

    // now, get the json obj of the search data
    cJSON* search_json = cJSON_Parse(cjson_data);
    if (!search_json) {
        printf("Error: %s\n", cJSON_GetErrorPtr());
        return;
    }
    
    // go down the parent objects 
    cJSON *contents = cJSON_GetObjectItemCaseSensitive(search_json, "contents");
    cJSON *twoColumnSearchResultsRenderer = contents ? cJSON_GetObjectItemCaseSensitive(contents, "twoColumnSearchResultsRenderer") : NULL;
    cJSON *primaryContents = twoColumnSearchResultsRenderer ? cJSON_GetObjectItemCaseSensitive(twoColumnSearchResultsRenderer, "primaryContents") : NULL;
    cJSON *sectionListRenderer = primaryContents ? cJSON_GetObjectItemCaseSensitive(primaryContents, "sectionListRenderer") : NULL;
    cJSON *sections = sectionListRenderer ? cJSON_GetObjectItemCaseSensitive(sectionListRenderer, "contents") : NULL;

    if (sections && cJSON_IsArray(sections)) {
        cJSON *first_section = cJSON_GetArrayItem(sections, 0);
        cJSON *itemSectionRenderer = first_section ? cJSON_GetObjectItemCaseSensitive(first_section, "itemSectionRenderer") : NULL;
        cJSON *contents = itemSectionRenderer ? cJSON_GetObjectItemCaseSensitive(itemSectionRenderer, "contents") : NULL;

        if (contents && cJSON_IsArray(contents)) {
            // loop through every item and get the node equivalent 
            cJSON *item;
            cJSON_ArrayForEach (item, contents) {
                YoutubeSearchNode node = { 0 };
                
                // if the ith item is a video
                cJSON *videoRenderer = cJSON_GetObjectItem(item, "videoRenderer");
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

                // // if the ith item is a channel
                cJSON *channelRenderer = cJSON_GetObjectItem(item, "channelRenderer");
                if (channelRenderer) {
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
                
                // for playlist:
                    // title
                    // thumbnail
                    // nvideos 
                
                add_node(search_results, node);
            }
        }
    }

    // dealloc
    free(cjson_data);
    cJSON_Delete(search_json);
    unload_memory_block(&search_chunk);
}

int main()
{
    // start the curl session
    curl_global_init(CURL_GLOBAL_ALL);
    
    CURL* curl_handle = curl_easy_init();
    
    YoutubeSearchList search_results = { 0 };
    char* query = curl_easy_escape(curl_handle, "elden ring playthrough", 0);
    metube_search_results(query, curl_handle, &search_results);
    print_list(&search_results);

    // dealloc app
    {
        // cleanup curl stuff
        curl_free(query); 
        curl_easy_cleanup(curl_handle);
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