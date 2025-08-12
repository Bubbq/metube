#include "include/thread_task.h"

#include <stdlib.h>

ThreadTask* thread_task_init()
{
    return calloc(1, sizeof(ThreadTask));
}

void thread_task_free(ThreadTask* task)
{
    if (task == NULL) return;

    if (task->args) {
        free(task->args); task->args = NULL;
    }
    
    free(task); task = NULL;
}