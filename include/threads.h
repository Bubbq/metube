#ifndef THREADS_H
#define THREADS_H

#define MAX_THREADS 4

#include "list.h"

#include <stdbool.h>
#include <pthread.h>

typedef struct
{
    pthread_t* threads;
    size_t nthreads;
} ThreadPool;

bool thread_pool_init(ThreadPool* thread_pool, const size_t nthreads);
void thread_pool_free(ThreadPool* thread_pool);

bool launch_task(List* task_queue, void* targs, void* (*funct)(void*));

typedef struct
{
    List* task_queue;
    bool* is_application_running;
} WorkerArgs;

void* worker(void* args);
WorkerArgs* worker_args_init(List* task_queue, bool* app_running);
bool launch_workers(List* task_queue, ThreadPool* thread_pool, bool* application_is_running);

#endif