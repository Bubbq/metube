#ifndef LIST_H
#define LIST_H

#include <pthread.h>

typedef enum
{
    NODE_TYPE_SEACH_RESULT,
    NODE_TYPE_RAW_THUMBNAIL,
    NODE_TYPE_THREAD_TASK,
    NODE_TYPE_UNDF,
} NodeType;

typedef struct Node 
{
    void* content;
    struct Node* next;
    NodeType type;
} Node;

Node* node_init(const NodeType node_type);
void node_free(Node* node);

typedef struct
{
    pthread_cond_t cond;
    pthread_mutex_t mutex;
    Node* head; 
    Node* tail; 
    int count;
} List;

List list_init();
void list_free(List* list);
Node* list_dequeue(List* list);
void list_append(List* list, Node* node);

#endif