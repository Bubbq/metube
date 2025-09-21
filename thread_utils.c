#include "include/thread_utils.h"

#include <stdio.h>
#include <stdlib.h>

ThreadTask* thread_task_init()
{
    return calloc(1, sizeof(ThreadTask));
}

void thread_task_free(ThreadTask* task)
{
    if (!task)
        return;

    if (task->targs) {
        free(task->targs); task->targs = NULL;
    }
    
    free(task); task = NULL;
}

bool thread_task_launch(LinkedList* task_queue, const ThreadArgs targs, const ThreadFunction tfunct)
{
    if (!task_queue || !targs || !tfunct) 
        return false;

    ThreadTask * task = thread_task_init() ;
    Node* node = node_init(task, sizeof(ThreadTask), (void*) thread_task_free, NULL);
    if (!node) 
        return false;

    task->targs = targs;
    task->tfunct = tfunct;

    pthread_mutex_lock(&task_queue->mutex);
    linked_list_append(task_queue, node);
    pthread_cond_signal(&task_queue->cond);
    pthread_mutex_unlock(&task_queue->mutex);

    return true;
}

bool thread_pool_init(ThreadPool* thread_pool, const size_t nthreads)
{
    if (!thread_pool)
        return false;

    thread_pool->nthreads = nthreads;
    thread_pool->threads = malloc(nthreads * sizeof(pthread_t));
    if (!thread_pool->threads) {
        fprintf(stderr, "thread_pool_init: malloc returned null\n");
        return false;
    }

    return true;
}

void thread_pool_free(ThreadPool* thread_pool)
{
    if (!thread_pool)
        return;

    for (size_t t = 0; t < thread_pool->nthreads; t++)
        pthread_join(thread_pool->threads[t], NULL);

    free(thread_pool->threads); thread_pool->threads = NULL;
}

void* worker(ThreadArgs targs)
{
    WorkerArgs* wargs = (WorkerArgs*) targs;
    
    if (!wargs) 
        return NULL;
    
    LinkedList* task_queue = wargs->task_queue;

    bool* is_application_running = wargs->is_application_running; 

    if (!task_queue || !is_application_running) {
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

        Node* node = linked_list_dequeue(task_queue);

        pthread_mutex_unlock(&task_queue->mutex);
         
        ThreadTask* task = (ThreadTask*) node->data;
        if (task) 
            task->tfunct(task->targs); 

        node_free(node); // NOTE: all alloced args that were launched using 'launch_task' are freed here
    }

    free(wargs); wargs = NULL;
    return NULL;
}

WorkerArgs* worker_args_init(LinkedList* task_queue, bool* app_running)
{
    if (!task_queue|| !app_running) return NULL;

    WorkerArgs* wargs = malloc(sizeof(WorkerArgs));
    if (wargs) {
        wargs->task_queue = task_queue;
        wargs->is_application_running = app_running;
    }

    return wargs;
}

bool launch_workers(LinkedList* task_queue, ThreadPool* thread_pool, bool* application_is_running)
{
    if (!task_queue || !thread_pool || !application_is_running)
        return false;

    for (size_t t = 0; t < thread_pool->nthreads; t++) {
        WorkerArgs* wargs = worker_args_init(task_queue, application_is_running);
        if (wargs)
            pthread_create(&thread_pool->threads[t], NULL, worker, wargs);
    }

    return true;
}

bool thread_context_init(ThreadContext* thread_context, const size_t nthreads)
{
    if (!thread_context || !thread_pool_init(&thread_context->thread_pool, nthreads))
        return false;

    thread_context->application_is_running = true;
    thread_context->task_queue = linked_list_init();

    return launch_workers(&thread_context->task_queue, &thread_context->thread_pool, &thread_context->application_is_running);
}

void thread_context_free(ThreadContext* thread_context)
{
    if (!thread_context)
        return;

    thread_context->application_is_running = false;
    pthread_cond_broadcast(&thread_context->task_queue.cond);
    thread_pool_free(&thread_context->thread_pool);
    linked_list_free(&thread_context->task_queue);
}

bool thread_context_add_task(ThreadContext *thread_context, const ThreadArgs targs, const ThreadFunction tfunct)
{
    if (!thread_context || !tfunct)
        return false;

    return thread_task_launch(&thread_context->task_queue, targs, tfunct);
}