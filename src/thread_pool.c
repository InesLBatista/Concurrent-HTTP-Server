#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include "config.h"
#include "logger.h"
#include "shared_mem.h"
#include "worker.h"
#include "thread_pool.h"

#include "http.h"
#include "cache.h"

/* Access global configuration and shared queue structure */
extern server_config_t config;
extern connection_queue_t *queue;

/*
 * Start Worker Process
 * Purpose: This is the main entry point for a Worker process. It initializes 
 * process-local resources (cache, thread pool, logger thread) and enters 
 * a loop to receive client connections from the Master process.
 *
 * Parameters:
 * - ipc_socket: The UNIX domain socket used to receive File Descriptors 
 * from the Master process.
 */
/*
 * Initialize Local Worker Queue
 * Purpose: Prepares the circular buffer used by the thread pool.
 */
int local_queue_init(local_queue_t *q, int max_size)
{
    q->fds = malloc(sizeof(int) * max_size);
    if (!q->fds) return -1;
    q->head = 0;
    q->tail = 0;
    q->max_size = max_size;
    q->shutting_down = 0;
    if (pthread_mutex_init(&q->mutex, NULL) != 0) return -1;
    if (pthread_cond_init(&q->cond, NULL) != 0) return -1;
    return 0;
}

void local_queue_destroy(local_queue_t *q)
{
    if (!q) return;
    free(q->fds);
    pthread_mutex_destroy(&q->mutex);
    pthread_cond_destroy(&q->cond);
}

/*
 * Enqueue (Producer: Worker Main Thread)
 * Purpose: Adds a client FD to the pool.
 * Return: -1 if full (Master will send 503).
 */
int local_queue_enqueue(local_queue_t *q, int client_fd)
{
    pthread_mutex_lock(&q->mutex);
    int next = (q->tail + 1) % q->max_size;
    if (next == q->head) {
        pthread_mutex_unlock(&q->mutex);
        return -1; /* Queue Full */
    }
    q->fds[q->tail] = client_fd;
    q->tail = next;
    /* Signal waiting threads that work is available */
    pthread_cond_signal(&q->cond);
    pthread_mutex_unlock(&q->mutex);
    return 0;
}

/*
 * Dequeue (Consumer: Worker Threads)
 * Purpose: Retrieves a FD to process.
 * Logic: Blocks on condition variable if queue is empty.
 */
int local_queue_dequeue(local_queue_t *q)
{
    pthread_mutex_lock(&q->mutex);
    while (q->head == q->tail && !q->shutting_down) {
        pthread_cond_wait(&q->cond, &q->mutex);
    }
    if (q->head == q->tail && q->shutting_down) {
        pthread_mutex_unlock(&q->mutex);
        return -1; /* Shutdown signal received */
    }
    int fd = q->fds[q->head];
    q->head = (q->head + 1) % q->max_size;
    pthread_mutex_unlock(&q->mutex);
    return fd;
}

/*
 * Worker Thread Entry Point
 * Purpose: Continuously pulls requessts from the local queue and handles them.
 */
void *worker_thread(void *arg)
{
    local_queue_t *q = (local_queue_t *)arg;
    while (1)
    {
        int client_socket = local_queue_dequeue(q);
        if (client_socket < 0) {
            break; /* shutdown signaled */
        }

        handle_client(client_socket);
    }
    return NULL;
}