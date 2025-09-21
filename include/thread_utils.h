#ifndef THREAD_UTILS_H
#define THREAD_UTILS_H

#include "linked_list.h"

#include <stdbool.h>

#define MAX_THREADS 4

typedef void* ThreadArgs;
typedef void* (*ThreadFunction)(void*);

typedef struct
{
    ThreadFunction tfunct;
    ThreadArgs targs;
} ThreadTask;

ThreadTask* thread_task_init();
void thread_task_free(ThreadTask* task);
bool thread_task_launch(LinkedList* task_queue, const ThreadArgs targs, const ThreadFunction tfunct); // rename to thread_task_launch?

typedef struct
{
    size_t nthreads;
    pthread_t* threads;
} ThreadPool;

bool thread_pool_init(ThreadPool* thread_pool, const size_t nthreads);
void thread_pool_free(ThreadPool* thread_pool);

typedef struct
{
    LinkedList* task_queue;
    bool* is_application_running;
} WorkerArgs;

void* worker(ThreadArgs wargs);
WorkerArgs* worker_args_init(LinkedList* task_queue, bool* app_running);
bool launch_workers(LinkedList* task_queue, ThreadPool* thread_pool, bool* application_is_running);

typedef struct
{
    LinkedList task_queue;
    ThreadPool thread_pool;
    bool application_is_running;
} ThreadContext;

void thread_context_free(ThreadContext* thread_context);
bool thread_context_init(ThreadContext* thread_context, const size_t nthreads);
bool thread_context_add_task(ThreadContext* thread_context, const ThreadArgs targs, const ThreadFunction tfunct);

#endif