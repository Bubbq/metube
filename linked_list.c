#include "include/linked_list.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

Node * node_init (void * data, const unsigned long data_size, const FreeRoutine freef, const PrintRoutine printf)
{
    Node * node = malloc(sizeof(Node)) ;
    if ( !node)
        return NULL ;
    
    node->data = data ;
    node->size = data_size ;

    node->freef = freef ;
    node->printf = printf ;

    node->prev = node->next = NULL ;  

    return node ;
}

void node_free (Node * node)
{
    if ( !node)
        return ; 

    if (node->freef)
        node->freef(node->data) ;

    free(node) ; node = NULL ;
}

LinkedList linked_list_init ()
{
    LinkedList list ;

    list.count = 0 ;
    list.head = list.tail = NULL ;
    pthread_cond_init(&list.cond, NULL) ;
    pthread_mutex_init(&list.mutex, NULL) ;

    return list ;
}

void linked_list_free (LinkedList * linked_list)
{
    if ( !linked_list)
        return ;

    Node * node ;

    while ( (node = linked_list_dequeue(linked_list)))
        node_free(node) ;
    
    linked_list->head = linked_list->tail = NULL ;

    pthread_cond_destroy(&linked_list->cond) ;
    pthread_mutex_destroy(&linked_list->mutex) ;
}

void linked_list_append (LinkedList * linked_list, Node * node)
{
    if ( !linked_list || !node)
        return ;

    if (linked_list->count == 0) 
        linked_list->head = linked_list->tail = node ;

    else {
        node->next = NULL ;
        node->prev = linked_list->tail ;

        linked_list->tail->next = node ;
        linked_list->tail = node ;
    }

    linked_list->count++ ;
}

Node * linked_list_dequeue (LinkedList * linked_list)
{
    if ( !linked_list || (linked_list->count == 0))
        return NULL ;

    Node * detached = linked_list->head ;

    linked_list->head = linked_list->head->next ;

    if ( !linked_list->head)
        linked_list->tail = NULL ;

    else
        linked_list->head->prev = NULL ;

    linked_list->count-- ;

    detached->prev = detached->next = NULL ;

    return detached ;
}

void linked_list_print (LinkedList * linked_list)
{
    if ( !linked_list)
        return ;

    for (Node * node = linked_list->head; node; node = node->next)
        if (node->printf)
            node->printf(node->data) ;
}

Node * linked_list_find (LinkedList * linked_list, const void * data, const compar comparator)
{
    if ( !linked_list || !data || !comparator)
        return NULL ;

    for (Node * node = linked_list->head; node; node = node->next) {
        if (comparator(data, node->data))
            return node ;
    }

    return NULL ;
}

void linked_list_insert (LinkedList * list, Node * node, const size_t position)
{
    if ( !list || !node || (position > list->count))
        return ;

    if (position == list->count) {
        linked_list_append(list, node) ;    
        return ;
    }

    size_t i = 0 ;
    
    Node * current = list->head ;
    
    for (; current && (i < position); current = current->next, i++)
        ;
    
    if (current && (i == position)) {
        Node * prev = current->prev ;
        
        if (prev)
            prev->next = node ;
    
        else 
            list->head = node ;
    
        node->next = current ;
        
        prev = node ;

        list->count++ ;
    }
}

void node_detach (LinkedList * list, Node * node)
{
    if ( !list || !node)
        return ;

    Node * prev = node->prev ;

    Node * next = node->next ;

    if (prev) 
        prev->next = next ;
    
    else if (node == list->head) {
        linked_list_dequeue(list) ;
        return ;
    }

    if (next) 
        next->prev = prev ;

    else if (node == list->tail) {
        list->tail = node->prev ;

        if ( !list->tail)
            list->head = NULL ;
        
        else
            list->tail->next = NULL ;
    }
    
    node->prev = node->next = NULL ;

    list->count-- ;
}