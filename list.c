#include "include/list.h"

#include "include/thread_task.h"
#include "include/raw_thumbnail.h"
#include "include/search_result.h"

#include <stdio.h>
#include <stdlib.h>

Node* node_init(const NodeType node_type)
{
    Node* node = malloc(sizeof(Node));
    if (node == NULL) {
        fprintf(stderr, "node_init: malloc returned null\n");
        return NULL;
    }

    node->next = NULL;
    node->content = NULL;
    node->type = node_type;
    
    switch (node->type) {
        case NODE_TYPE_THREAD_TASK:   node->content = thread_task_init(); break;
        case NODE_TYPE_SEACH_RESULT:  node->content = search_result_init(); break;
        case NODE_TYPE_RAW_THUMBNAIL: node->content = raw_thumbnail_init(); break;
        case NODE_TYPE_UNDF: 
            break;
    }

    if (node->content == NULL) {
        fprintf(stderr, "node_init: failed to resolve node content\n");
        free(node); node = NULL;
    }

    return node;
}

void node_free(Node* node)
{
    if (node == NULL) return;

    switch (node->type) {
        case NODE_TYPE_THREAD_TASK:   thread_task_free(node->content); break;
        case NODE_TYPE_SEACH_RESULT:  search_result_free(node->content); break;
        case NODE_TYPE_RAW_THUMBNAIL: raw_thumbnail_free(node->content); break;
        case NODE_TYPE_UNDF: break;
    }

    free(node); node = NULL;
}

List list_init()
{
    List list;
    
    list.count = 0;
    list.head = list.tail = NULL;
    
    pthread_cond_init(&list.cond, NULL);
    pthread_mutex_init(&list.mutex, NULL);

    return list;
}

void list_free(List* list)
{
    if (list == NULL) return;

    while (list->head && list->count != 0) node_free(list_dequeue(list));
    
    list->count = 0;
    list->head = list->tail = NULL;
    
    pthread_cond_destroy(&list->cond);
    pthread_mutex_destroy(&list->mutex);
}

Node* list_dequeue(List* list)
{
    if (list == NULL) return NULL;

    Node* detached = list->head;

    list->head = list->head->next;
    if (list->head == NULL) {
        list->tail = NULL;
    }

    list->count--;
    
    return detached;
}

void list_append(List* list, Node* node)
{
    if ((list == NULL) || (node == NULL)) return;

    node->next = NULL;

    if (list->head == NULL) {
        list->head = list->tail = node;
    }

    else {
        list->tail->next = node;
        list->tail = node;
    }

    list->count++;
} 