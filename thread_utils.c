#include "include/thread_utils.h"

#include <stdlib.h>
#include <string.h>

Task * task_init (void * targs, ThreadArgFree tfreef, ThreadFunction tfunct)
{
    if ( !tfunct)
        return NULL ;
    
    Task * task = malloc(sizeof(Task)) ;
    if ( !task)
        return NULL ;
    
    task->targs = targs ;
    task->tfreef = tfreef ;
    task->tfunct = tfunct ;

    return task ;
}   

static Node * task_node_init (void * targs, const ThreadArgFree tfreef, const ThreadFunction tfunct)
{
    if ( !tfunct)
        return NULL ;

    Task * task = task_init(targs, tfreef, tfunct) ;
    if ( !task)
        return NULL ;

    Node * task_node = node_init(task, sizeof(Task), (void *) task_free, NULL) ;
    if ( !task_node) {
        task_free(task) ;
        return NULL ;
    }

    return task_node ;
}

void task_free (Task * task)
{
    if ( !task)
        return ;

    if (task->tfreef && task->targs)
        task->tfreef(task->targs) ;

    free(task) ; task = NULL ;
}

typedef struct
{
    LinkedList * task_queue ;
    bool * is_application_running ;
} WorkerArgs ;

static void * worker (void * targs)
{
    WorkerArgs * wargs = (WorkerArgs *) targs ; 
    
    if ( !wargs) 
        return NULL ;
    
    LinkedList * task_queue = wargs->task_queue ;

    bool * is_application_running = wargs->is_application_running ;   

    if ( !task_queue || !is_application_running) {
        free(wargs) ; wargs = NULL ; 
        return NULL ;
    }

    while (true) {
        pthread_mutex_lock(&task_queue->mutex) ;

        while ( (*is_application_running) && (task_queue->count == 0)) 
            pthread_cond_wait(&task_queue->cond, &task_queue->mutex) ;

        if ( !(*is_application_running)) {
            pthread_mutex_unlock(&task_queue->mutex) ;
            break ;
        }

        Node * node = linked_list_dequeue(task_queue) ;

        pthread_mutex_unlock(&task_queue->mutex) ;
        
        if (node && node->data) {
            Task * task = (Task *) node->data ;
            task->tfunct(task->targs) ; 
            node_free(node) ;
        }
    }

    free(wargs) ; wargs = NULL ;

    return NULL ;
}

static WorkerArgs * worker_args_init (LinkedList * task_queue, bool * is_application_running)
{
    if ( !task_queue || !is_application_running)
        return NULL ;

    WorkerArgs * wargs = malloc(sizeof(WorkerArgs)) ;
    if ( !wargs)
        return NULL ;

    wargs->task_queue = task_queue ;
    wargs->is_application_running = is_application_running ;

    return wargs ;
}

bool thread_context_init (ThreadContext * thread_context, const size_t nthreads)
{
    if ( !thread_context)
        return false ;

    thread_context->task_queue = linked_list_init() ;
    thread_context->is_application_running = true ;
    thread_context->nthreads = nthreads ;

    const size_t thread_pool_size = nthreads * sizeof(pthread_t) ;

    thread_context->threads = malloc(thread_pool_size) ;
    if ( !thread_context->threads)
        return false ;

    memset(thread_context->threads, 0, thread_context->nthreads * sizeof(pthread_t)) ;

    for (size_t i = 0; i < thread_context->nthreads; i++) {
        WorkerArgs * wargs = worker_args_init(&thread_context->task_queue, &thread_context->is_application_running) ;
        if ( !wargs) 
            return false ;
        
        pthread_create(&thread_context->threads[i], NULL, worker, wargs) ;
    }

    return true ;
}

bool thread_context_add_task (ThreadContext * thread_context, void * targs, const ThreadArgFree tfreef, const ThreadFunction tfunct)
{
    if ( !thread_context) 
        return false;

    Node * task_node = task_node_init(targs, tfreef, tfunct) ;
    if ( !task_node)
        return false ;

    LinkedList * task_queue = &thread_context->task_queue ;

    pthread_mutex_lock(&task_queue->mutex) ;

    linked_list_append(task_queue, task_node) ;

    pthread_mutex_unlock(&task_queue->mutex) ;

    pthread_cond_signal(&task_queue->cond) ;

    return true ;
}

void thread_context_free (ThreadContext * thread_context)
{
    if ( !thread_context)
        return ;

    pthread_mutex_lock(&thread_context->task_queue.mutex) ;
    
    thread_context->is_application_running = false ;

    pthread_cond_broadcast(&thread_context->task_queue.cond) ;

    pthread_mutex_unlock(&thread_context->task_queue.mutex) ;

    for (size_t i = 0; i < thread_context->nthreads; i++)
        pthread_join(thread_context->threads[i], NULL) ;
    
    free(thread_context->threads); thread_context->threads = NULL ;

    linked_list_free(&thread_context->task_queue) ;
}