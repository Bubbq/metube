#ifndef LINKED_LIST_H
#define LINKED_LIST_H

#include <pthread.h>

typedef void (*FreeRoutine)  (void *) ;
typedef void (*PrintRoutine) (void *) ;

typedef struct Node {
    void * data ;                   
    struct Node*  next ;            
    struct Node*  prev ;            
    unsigned long size ;            
    FreeRoutine  freef ;     
    PrintRoutine printf ;    
} Node ;

Node * node_init  (void * data, const unsigned long data_size, const FreeRoutine freef, const PrintRoutine printf) ;
void   node_free  (Node * node) ;

typedef struct {
    pthread_cond_t cond ;
    pthread_mutex_t mutex ;
    Node * head ;           
    Node * tail ;          
    unsigned long count ;   
} LinkedList ;

LinkedList linked_list_init    () ;
void       linked_list_free    (LinkedList * linked_list) ;
void       linked_list_print   (LinkedList * linked_list) ;
Node *     linked_list_dequeue (LinkedList * linked_list) ;
void       linked_list_append  (LinkedList * linked_list, Node * node) ;

#endif