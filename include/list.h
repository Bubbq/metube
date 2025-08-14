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

Node* init_node(const content_init_funct init_funct, void* init_params, const content_free_funct free_funct, const content_print_funct print_funct);
void free_node(Node* node);
void print_node(const Node* node);

typedef struct
{
    pthread_cond_t cond;
    pthread_mutex_t mutex;
    size_t count;
    Node* head; 
    Node* tail; 
} List;

List init_list();
void free_list(List* list);
void print_list(const List* list);
Node* dequeue_list(List* list);
void append_list(List* list, Node* node);

#endif