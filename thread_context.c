#include "include/thread_context.h"

#include <stdio.h>
#include <stdlib.h>

bool thread_context_init(ThreadContext* thread_context, const size_t nthreads)
{
    if (!thread_context)
        return false;

    if (!thread_pool_init(&thread_context->thread_pool, nthreads)) 
        return false;
    
    thread_context->application_is_running = true;
    thread_context->task_queue = init_list();

    return launch_workers(&thread_context->task_queue, &thread_context->thread_pool, &thread_context->application_is_running);
}

void thread_context_free(ThreadContext* thread_context)
{
    if (!thread_context)
        return;

    thread_context->application_is_running = false;
    pthread_cond_broadcast(&thread_context->task_queue.cond);
    thread_pool_free(&thread_context->thread_pool);
    free_list(&thread_context->task_queue);
}

bool thread_context_add_task(ThreadContext *thread_context, void *args, void *(*funct)(void *))
{
    if (!thread_context || !funct)
        return false;

    return launch_task(&thread_context->task_queue, args, funct);
}