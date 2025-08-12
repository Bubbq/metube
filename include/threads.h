#ifndef THREADS_H
#define THREADS_H

#define MAX_THREADS 4

#include "list.h"

#include <stdbool.h>
#include <pthread.h>

typedef struct
{
    List* task_queue;
    bool* is_application_running;
} WorkerArgs;

WorkerArgs* worker_args_init(List* task_queue, bool* app_running);

void* worker(void* args);
bool launch_task(List* task_queue, void* targs, void* (*funct)(void*));
void launch_workers(pthread_t* pool, const size_t nthreads, List* task_queue, bool* app_running);

void thread_pool_free(pthread_t* pool, const size_t nthreads);

#endif