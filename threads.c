#include "include/threads.h"

#include "include/list.h"
#include "include/thread_task.h"

#include <stdlib.h>
#include <pthread.h>

WorkerArgs* worker_args_init(List* task_queue, bool* app_running)
{
    if ((task_queue == NULL) || (app_running == NULL)) return NULL;

    WorkerArgs* wargs = malloc(sizeof(WorkerArgs));
    if (wargs) {
        wargs->task_queue = task_queue;
        wargs->is_application_running = app_running;
    }

    return wargs;
}

void* worker(void* args)
{
    WorkerArgs* wargs = (WorkerArgs*) args;
    
    if (wargs == NULL) return NULL;
    
    List* task_queue = wargs->task_queue;

    bool* is_application_running = wargs->is_application_running; 

    if ((task_queue == NULL) || (is_application_running == NULL)) {
        free(wargs); wargs = NULL;
        return NULL;
    }
    
    while (*is_application_running) {
        pthread_mutex_lock(&task_queue->mutex);

        while ((task_queue->count == 0) && (*is_application_running)) 
            pthread_cond_wait(&task_queue->cond, &task_queue->mutex);

        if ((*is_application_running) == false) {
            pthread_mutex_unlock(&task_queue->mutex);
            break;
        }

        Node* node = list_dequeue(task_queue);

        pthread_mutex_unlock(&task_queue->mutex);
         
        if (node->type == NODE_TYPE_THREAD_TASK) {
            ThreadTask* task = (ThreadTask*) node->content;
            if (task) 
                task->funct(task->args); 
        }

        node_free(node); // NOTE: all alloc'ed arguemnts are freed here, not in the thread function executed
    }

    free(wargs); wargs = NULL;
    return NULL;
}

bool launch_task(List* task_queue, void* targs, void* (*tfunct)(void*))
{
    if ((task_queue == NULL) || (targs == NULL) || (tfunct == NULL)) return false;

    Node* node = node_init(NODE_TYPE_THREAD_TASK);
    if (node == NULL) 
        return false;

    ThreadTask* task = (ThreadTask*) node->content;
    task->args = targs;
    task->funct = tfunct;

    pthread_mutex_lock(&task_queue->mutex);

    list_append(task_queue, node);

    pthread_cond_signal(&task_queue->cond);
    
    pthread_mutex_unlock(&task_queue->mutex);

    return true;
}

void launch_workers(pthread_t* pool, const size_t nthreads, List* task_queue, bool* app_running)
{
    if ((pool == NULL) || (task_queue == NULL) || (app_running == NULL)) return;

    for (size_t t = 0; t < nthreads; t++) {
        WorkerArgs* wargs = worker_args_init(task_queue, app_running);
        if (wargs) 
            pthread_create(&pool[t], NULL, worker, wargs);
    }
}

void thread_pool_free(pthread_t* pool, const size_t nthreads)
{
    if (pool == NULL) return;

    for (size_t t = 0; t < nthreads; t++)
        pthread_join(pool[t], NULL);
}