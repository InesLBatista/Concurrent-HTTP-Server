#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <pthread.h>

typedef struct local_queue {
    int *fds;
    int head;
    int tail;
    int max_size;
    int shutting_down;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
} local_queue_t;

int local_queue_init(local_queue_t *q, int max_size);
void local_queue_destroy(local_queue_t *q);
int local_queue_enqueue(local_queue_t *q, int client_fd);
int local_queue_dequeue(local_queue_t *q);

void *worker_thread(void *arg);

#endif