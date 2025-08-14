#ifndef THREAD_CONTEXT_H
#define THREAD_CONTEXT_H

#include "list.h"
#include "threads.h"

#include <stdbool.h>

typedef struct
{
    List task_queue;
    ThreadPool thread_pool;
    bool application_is_running;
} ThreadContext;

void thread_context_free(ThreadContext* thread_context);
bool thread_context_init(ThreadContext* thread_context, const size_t nthreads);
bool thread_context_add_task(ThreadContext* thread_context, void* args, void* (*funct)(void*));

#endif