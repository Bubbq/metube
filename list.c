#include "include/list.h"

#include <stdio.h>
#include <stdlib.h>

Node* node_init(const content_init_funct init_funct, void* init_params, const content_free_funct free_funct, const content_print_funct print_funct)
{
    Node* node = malloc(sizeof(Node));
    if (!node) {
        fprintf(stderr, "init_node: malloc returned null\n");
        return NULL;
    }

    node->next = NULL;
    node->init = init_funct;
    node->free = free_funct;
    node->print = print_funct;

    if (node->init) {
        node->content = node->init(init_params);
        if (!node->content) {
            fprintf(stderr, "init_node: failed to instantiate node content\n");
            free(node); node = NULL;
            return NULL;
        }
    }

    return node;
}

void node_free(Node* node)
{
    if (!node)
        return;

    if (node->free && node->content) 
        node->free(node->content);

    free(node); node = NULL;
}

void node_print(const Node *node)
{
    if (!node || !node->print)
        return;

    node->print(node->content);
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
    if (!list)
        return;

    while (list->head)
        node_free(list_dequeue(list));

    list->head = list->tail = NULL;
    pthread_cond_destroy(&list->cond);
    pthread_mutex_destroy(&list->mutex);
}

void list_print(const List* list)
{
    if (!list)
        return;

    for (Node* node = list->head; node; node = node->next)
        node_print(node);
}

void list_append(List* list, Node* node)
{
    if (!list || !node)
        return;

    node->next = NULL;

    if (!list->head) 
        list->head = list->tail = node;

    else {
        list->tail->next = node;
        list->tail = node;
    }

    list->count++;
}

Node* list_dequeue(List* list)
{
    if (!list)
        return NULL;

    Node* detached = list->head;

    list->head = list->head->next;
    if (!list->head) 
        list->tail = NULL;

    list->count--;
    
    return detached;
}