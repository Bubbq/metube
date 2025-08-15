#ifndef LINKED_LIST_H
#define LINKED_LIST_H

#include <pthread.h>

typedef void* (*content_init_funct)(void*);
typedef void (*content_free_funct)(void*);
typedef void (*content_print_funct)(void*);

typedef struct Node 
{
    content_init_funct init;
    content_free_funct free;
    content_print_funct print;
    void* content;
    struct Node* next;
} Node;

Node* node_init(const content_init_funct init_funct, void* init_params, const content_free_funct free_funct, const content_print_funct print_funct);
void node_free(Node* node);
void node_print(const Node* node);

typedef struct
{
    pthread_cond_t cond;
    pthread_mutex_t mutex;
    size_t count;
    Node* head; 
    Node* tail; 
} List;

List list_init();
void list_free(List* list);
void list_print(const List* list);
Node* list_dequeue(List* list);
void list_append(List* list, Node* node);

#endif