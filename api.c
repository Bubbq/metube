#include <string.h>
#include "api.h"

YoutubeAPI init_youtube_api(char* key)
{
    YoutubeAPI API;
    API.key = key;
    API.url = "https://www.googleapis.com/youtube/v3";
    API.video_endpoint = "videos";
    API.search_endpoint = "search"; 
    API.channel_endpoint = "channels";
    API.playlist_endpoint = "playlists";
    return API;
}

bool is_memory_ready(const MemoryBlock chunk)
{
    return ((chunk.size > 0) && (chunk.memory != NULL));
}

void unload_memory_block(MemoryBlock* chunk)
{
    free(chunk->memory);
    chunk->size = 0;
}

void create_file_from_memory(const char* filename, const MemoryBlock chunk) 
{
    FILE* fp = fopen(filename, "w");
    if (!fp) printf("could not write \"%s\" in write mode\n", filename);
    else {
        fprintf(fp, "%s", chunk.memory);
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
MemoryBlock fetch_url(const char* url, CURL* curl_handle)
{
    MemoryBlock chunk;

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

// convert the content of a fetched url to a json object
cJSON* api_to_json(const char* url, CURL* curl_handle, const char* debug_filename)
{
    // the data fetched from the URL
    MemoryBlock fetched = fetch_url(url, curl_handle);
    if (!is_memory_ready(fetched)) return NULL;

    // print fetched data to better understand future cJSON opertations
    // create_file_from_memory(debug_filename, fetched);

    // the json obj of this data
    // used to preform CRUD operations of .json files
    cJSON* json = cJSON_Parse(fetched.memory);
    if (!json) {
        printf("Error: %s\n", cJSON_GetErrorPtr());
        cJSON_Delete(json);
        return NULL;
    }

    // dealloc unused memory
    if (is_memory_ready(fetched)) unload_memory_block(&fetched);
    
    return json;
}
