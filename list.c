#include "list.h"
#include <stdio.h>

YoutubeSearchList create_youtube_search_list() 
{
    YoutubeSearchList list;
    list.head = list.tail = NULL;
    list.count = 0;
    return list;
}

void add_node(YoutubeSearchList* list, const YoutubeSearchNode node)
{
    // adding the first node to a list
    if (list->count == 0) {
        list->head = list->tail = malloc(sizeof(YoutubeSearchNode));
        if (!list->head) {
            printf("add_node: not enough memory, malloc returned null\n");
            return;
        }

        // set both ends of the list to the first node of the list
        *list->head = *list->tail = node;
    } 
    else {
        // allocate space for the new node
        list->tail->next = malloc(sizeof(YoutubeSearchNode));
        if (!list->tail->next) {
            printf("add_node: not enough memory, malloc returned null\n");
            return;
        }

        // append node to list
        *list->tail->next = node;
        
        // adjust tail ptr pos.
        list->tail = list->tail->next;
        list->tail->next = NULL;
    }
    
    // update list size
    list->count++;
}

void unload_node(YoutubeSearchNode* node)
{
    if (node->id) free(node->id);
    if (node->subs) free(node->subs);
    if (node->date) free(node->date);
    if (node->views) free(node->views);
    if (node->title) free(node->title);
    if (node->length) free(node->length);
    if (node->author) free(node->author);
    if (node) free(node);
}

void unload_list(YoutubeSearchList* list) 
{
    YoutubeSearchNode* prev = NULL;
    YoutubeSearchNode* current = list->head;

    while (current) {
        prev = current;
        current = current->next;
        unload_node(prev);
    }
    
    list->count = 0;
}

void print_node(const YoutubeSearchNode* node) 
{
    printf("id) %s title) %s author) %s subs) %s views) %s date) %s length) %s video count) %d thumbnail id) %d type) %d\n", 
            node->id, node->title, node->author, node->subs, node->views, node->date, node->length, node->video_count, node->thumbnail.id, node->type);
}

void print_list(const YoutubeSearchList* list)
{
    YoutubeSearchNode* current = list->head;
    while (current) {
        print_node(current);
        current = current->next;
    }
}