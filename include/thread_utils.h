#ifndef THREAD_UTILS_H
#define THREAD_UTILS_H

#include "linked_list.h"

#include <stdbool.h>

typedef void * (*ThreadFunction) (void *) ; // routine that thread will execute 
typedef void   (*ThreadArgFree)  (void *) ; // routine to deallocate thread argument(s) 

typedef struct
{
    void * targs ;
    ThreadArgFree tfreef ;
    ThreadFunction tfunct ;
} Task ;

Task * task_init (void * targs, ThreadArgFree tfreef, ThreadFunction tfunct) ;
void   task_free (Task * task) ;

typedef struct
{
    LinkedList task_queue ;
    pthread_t * threads ;
    size_t nthreads ;
    bool is_application_running ;
} ThreadContext;

void thread_context_free     (ThreadContext * thread_context) ;
bool thread_context_init     (ThreadContext * thread_context, const size_t thread_count) ;
bool thread_context_add_task (ThreadContext * thread_context, void * targs, ThreadArgFree tfreef, const ThreadFunction tfunct) ;

#endif