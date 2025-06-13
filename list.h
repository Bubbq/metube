#pragma once
#include <stdlib.h>
#include "raylib.h"

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
    int video_count;
    Texture thumbnail;
    SearchResultType type;
    struct YoutubeSearchNode* next;
} YoutubeSearchNode;

typedef struct {
    size_t count;
    YoutubeSearchNode* head;
    YoutubeSearchNode* tail;
} YoutubeSearchList;

YoutubeSearchList create_youtube_search_list(); 
void add_node(YoutubeSearchList*, const YoutubeSearchNode);
void unload_node(YoutubeSearchNode*);
void unload_list(YoutubeSearchList*);
void print_node(const YoutubeSearchNode*);
void print_list(const YoutubeSearchList*);