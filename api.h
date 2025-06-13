#pragma once
#include <cjson/cJSON.h>
#include <curl/curl.h>
#include <stdbool.h>
#include <stdlib.h>

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

YoutubeAPI init_youtube_api(char*);
bool is_memory_ready(const MemoryBlock);
void unload_memory_block(MemoryBlock*);
void create_file_from_memory(const char*, const MemoryBlock); 
size_t write_data(void*, int, size_t, void*);
MemoryBlock fetch_url(const char*, CURL*);
cJSON* api_to_json(const char*, CURL*, const char*);