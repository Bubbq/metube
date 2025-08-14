#include "include/threads.h"

#include "include/list.h"
#include "include/thread_task.h"

#include <stdio.h>
#include <stdlib.h>

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

        Node* node = dequeue_list(task_queue);

        pthread_mutex_unlock(&task_queue->mutex);
         
        ThreadTask* task = (ThreadTask*) node->content;
        if (task) 
            task->funct(task->args); 

        free_node(node); // NOTE: all alloced args that were launched using 'launch_task' are freed here
    }

    free(wargs); wargs = NULL;
    return NULL;
}

bool thread_pool_init(ThreadPool* thread_pool, const size_t nthreads)
{
    if (thread_pool == NULL)
        return false;

    thread_pool->nthreads = nthreads;
    thread_pool->threads = malloc(nthreads * sizeof(pthread_t));

    if (thread_pool->threads == NULL) {
        fprintf(stderr, "thread_pool_init: malloc returned null\n");
        return false;
    }

    return true;
}

void thread_pool_free(ThreadPool* thread_pool)
{
    if (thread_pool == NULL)
        return;

    for (size_t t = 0; t < thread_pool->nthreads; t++)
        pthread_join(thread_pool->threads[t], NULL);

    free(thread_pool->threads); thread_pool->threads = NULL;
}

bool launch_task(List* task_queue, void* targs, void* (*tfunct)(void*))
{
    if ((task_queue == NULL) || (targs == NULL) || (tfunct == NULL)) return false;

    Node* node = init_node((void*) thread_task_init, NULL, (void*) thread_task_free, NULL);
    if (!node) 
        return false;

    ThreadTask* task = (ThreadTask*) node->content;
    task->args = targs;
    task->funct = tfunct;

    pthread_mutex_lock(&task_queue->mutex);

    append_list(task_queue, node);

    pthread_cond_signal(&task_queue->cond);
    
    pthread_mutex_unlock(&task_queue->mutex);

    return true;
}

bool launch_workers(List* task_queue, ThreadPool* thread_pool, bool* application_is_running)
{
    if ((task_queue == NULL) || (thread_pool == NULL))
        return false;

    for (size_t t = 0; t < thread_pool->nthreads; t++) {
        WorkerArgs* wargs = worker_args_init(task_queue, application_is_running);
        if (wargs == NULL) {
            fprintf(stderr, "launch_workers: failed to initailize worker arguemnt\n");
            return false;
        }

        pthread_create(&thread_pool->threads[t], NULL, worker, wargs);
    }

    return true;
}