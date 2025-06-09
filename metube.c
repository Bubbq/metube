#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include <cjson/cJSON.h>

struct MemoryBlock {
    size_t size;
    char* memory;
};

// write sizeof(src) bytes to dst
size_t write_function(void* src, int nelements, size_t element_size, void* dst)
{
    // first, we to know how many bytes we are appending to dst
    // becuase src is generic, we dont know the type (cant use sizeof(*(src_type)src))
    size_t src_size = (nelements * element_size);

    // next, we need to resize the dst pointer to fit this new data
    // first, find out how much memory we are currenly holding
    struct MemoryBlock* mem = (struct MemoryBlock*) dst;

    // now get the new size, '+1' for '/0'
    char* new_memory = realloc(mem->memory, (mem->size + src_size + 1));
    if(!new_memory) {
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

    /* init the curl session */
    CURL* curl_handle = curl_easy_init();

    // specifying parameters for easy curl handle
    curl_easy_setopt(curl_handle, CURLOPT_URL, url); // where to request data
    curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, write_function); // how to write requested data 
    curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)&chunk); // where to write requested data

    // store the result of the GET request
    CURLcode res = curl_easy_perform(curl_handle);

    if(res != CURLE_OK) {
        fprintf(stderr, "curl_easy_perform() failed: %s\n",
                curl_easy_strerror(res));
    }

    // cleanup curl stuff 
    curl_easy_cleanup(curl_handle);

    // end the curl session
    curl_global_cleanup();
    
    return chunk;
}

int main()
{
    const char* base_url = "https://www.googleapis.com/youtube/v3";

    // the youtube equivalent of looking something up in the search bar
    const char* search_endpoint = "search";

    // what youtube does behind the sences when you press a video
    // this retrives the info of a video id (desc, thumbnail, statistics)
    const char* video_endpoint = "video";

    // same as above but for channels
    const char* channel_endpoint = "channels";

    // must be URL encoded
    const char* query = "asmongold";

    const char* api_key = "AIzaSyA_rjuK_RqbCZpQSxGEDwNW4a8vRGy1tcY";

    // maximum nelements to fetch from a search
    const int max_results = 10;

    char url[256];
    snprintf(url, 256, "%s/%s?part=snippet&q=%s&key=%s&maxResults=%d", base_url, search_endpoint, query, api_key, max_results);

    // first, get the information of the search
    struct MemoryBlock chunk = http_get(url);

    if(chunk.size > 0) {
        // write into json
        FILE* pFILE = fopen("test.json", "w");
        if(pFILE) 
            fprintf(pFILE, "%s", chunk.memory);
        else {
            printf("error opening file\n");
            return 1;
        }

        // read information into json format
        cJSON* test_json = cJSON_Parse(chunk.memory);
        if(cJSON_IsNull(test_json)) {
            printf("Error: %s\n", cJSON_GetErrorPtr());
            cJSON_Delete(test_json);
            return 1;
        }
        else {
            cJSON* items = cJSON_GetObjectItem(test_json, "items");
            if(items == NULL)
                printf("Error: %s\n", cJSON_GetErrorPtr());
            
            const int nelements = cJSON_GetArraySize(items);

            // video/channel id needed for 2nd API call 
            for(int i = 0; i < nelements; i++) {
                cJSON* subitem = cJSON_GetArrayItem(items, i);
                cJSON* id_tag = cJSON_GetObjectItem(subitem, "id");
                
                cJSON* videoId = cJSON_GetObjectItem(id_tag, "videoId");
                cJSON* channelId = cJSON_GetObjectItem(id_tag, "channelId");

                if(videoId) 
                    printf("videoID: %s\n", videoId->valuestring);
                else if(channelId)
                    printf("channelID: %s\n", channelId->valuestring);
            }

            cJSON_Delete(test_json);
        }
    }
    
    free(chunk.memory);

    return 0;
}