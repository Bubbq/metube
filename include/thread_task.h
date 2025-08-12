#ifndef THREAD_TASK_H
#define THREAD_TASK_H

typedef struct
{
    void *(*funct)(void *);
    void *args;
} ThreadTask;

ThreadTask* thread_task_init();
void thread_task_free(ThreadTask* task);

#endif
