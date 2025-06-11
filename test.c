#include "raylib.h"
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
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

struct YoutubeSearchList create_youtube_search_list() 
{
    struct YoutubeSearchList list;
    list.count = 0;
    list.head = list.tail = malloc(sizeof(struct YoutubeSearchNode));
    return list;
}

void add_youtube_search_data_node(struct YoutubeSearchList* list, struct YoutubeSearchNode node)
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

void unload_youtube_search_list(struct YoutubeSearchList* list) 
{
    struct YoutubeSearchNode* prev = NULL;
    struct YoutubeSearchNode* current = list->head;

    while(current) {
        prev = current;
        current = current->next;
        free(prev);
    }
}

void print_youtube_search_data(struct YoutubeSearchNode* node) 
{
    printf("id) %s link) %s title) %s author) %s subs) %s views) %s date) %s length) %s thumbnail id) %d type) %d\n", 
            node->id, node->link, node->title, node->author, node->subs, node->views, node->date, node->length, node->thumbnail.id, node->type);
}

int main()
{
    struct YoutubeSearchList list = create_youtube_search_list();
    for(int i = 0; i < 10; i++) {
        struct YoutubeSearchNode node = { 0 };
        node.type = i;
        add_youtube_search_data_node(&list, node);
    }

    struct YoutubeSearchNode* current = list.head;
    while(current) {
        print_youtube_search_data(current);
        current = current->next;
    }

    unload_youtube_search_list(&list);
    return 0;
}