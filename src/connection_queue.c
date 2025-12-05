/* 
 * connection_queue.c
 * Thread-safe bounded circular buffer queue for connection management
 * with timeout support and inter-process synchronization
 */

/* Define POSIX feature test macro before any includes */
#define _POSIX_C_SOURCE 200112L  /* For clock_gettime, sem_timedwait */

#include "connection_queue.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/time.h>
#include <unistd.h>
#include <fcntl.h>
#include <semaphore.h>
#include <time.h>

/* Helper function to get current time in milliseconds */
static uint64_t get_current_time_ms() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

/* Helper function to calculate time difference in milliseconds */
static int time_diff_ms(struct timespec *start, struct timespec *end) {
    return (end->tv_sec - start->tv_sec) * 1000 + 
           (end->tv_nsec - start->tv_nsec) / 1000000;
}

/* Initialize connection info structure */
static void init_connection_info(connection_info_t *conn, int socket_fd) {
    if (!conn) return;
    
    memset(conn, 0, sizeof(connection_info_t));
    conn->socket_fd = socket_fd;
    conn->arrival_time = time(NULL);
    conn->client_ip = 0;
    conn->client_port = 0;
    conn->worker_id = -1;  /* Not assigned yet */
    conn->priority = 0;    /* Default priority */
}

/* Create a new connection queue */
connection_queue_t *queue_create(int capacity, const char *sem_prefix) {
    if (capacity <= 0) {
        capacity = DEFAULT_QUEUE_CAPACITY;
    }
    
    /* Allocate queue structure */
    connection_queue_t *queue = malloc(sizeof(connection_queue_t));
    if (!queue) {
        perror("Failed to allocate queue");
        return NULL;
    }
    
    /* Initialize queue fields */
    memset(queue, 0, sizeof(connection_queue_t));
    
    /* Allocate items array */
    queue->items = malloc(sizeof(connection_info_t) * capacity);
    if (!queue->items) {
        perror("Failed to allocate queue items");
        free(queue);
        return NULL;
    }
    
    /* Initialize items */
    for (int i = 0; i < capacity; i++) {
        init_connection_info(&queue->items[i], -1);
    }
    
    queue->capacity = capacity;
    queue->size = 0;
    queue->front = 0;
    queue->rear = 0;
    
    /* Initialize synchronization primitives */
    if (pthread_mutex_init(&queue->lock, NULL) != 0) {
        perror("Failed to initialize mutex");
        free(queue->items);
        free(queue);
        return NULL;
    }
    
    if (pthread_cond_init(&queue->not_empty, NULL) != 0) {
        perror("Failed to initialize condition variable");
        pthread_mutex_destroy(&queue->lock);
        free(queue->items);
        free(queue);
        return NULL;
    }
    
    if (pthread_cond_init(&queue->not_full, NULL) != 0) {
        perror("Failed to initialize condition variable");
        pthread_cond_destroy(&queue->not_empty);
        pthread_mutex_destroy(&queue->lock);
        free(queue->items);
        free(queue);
        return NULL;
    }
    
    if (pthread_cond_init(&queue->shutdown_cv, NULL) != 0) {
        perror("Failed to initialize condition variable");
        pthread_cond_destroy(&queue->not_full);
        pthread_cond_destroy(&queue->not_empty);
        pthread_mutex_destroy(&queue->lock);
        free(queue->items);
        free(queue);
        return NULL;
    }
    
    /* Create unique semaphore names */
    char empty_name[256], filled_name[256];
    if (sem_prefix) {
        snprintf(empty_name, sizeof(empty_name), "%s_empty", sem_prefix);
        snprintf(filled_name, sizeof(filled_name), "%s_filled", sem_prefix);
    } else {
        static int counter = 0;
        snprintf(empty_name, sizeof(empty_name), "/queue_empty_%d_%d", getpid(), counter);
        snprintf(filled_name, sizeof(filled_name), "/queue_filled_%d_%d", getpid(), counter);
        counter++;
    }
    
    /* Create semaphores (POSIX named semaphores for process sharing) */
    queue->empty_slots = sem_open(empty_name, O_CREAT | O_EXCL, 0644, capacity);
    if (queue->empty_slots == SEM_FAILED) {
        /* Try without O_EXCL if it already exists */
        queue->empty_slots = sem_open(empty_name, O_CREAT, 0644, capacity);
        if (queue->empty_slots == SEM_FAILED) {
            perror("Failed to create empty slots semaphore");
            pthread_cond_destroy(&queue->shutdown_cv);
            pthread_cond_destroy(&queue->not_full);
            pthread_cond_destroy(&queue->not_empty);
            pthread_mutex_destroy(&queue->lock);
            free(queue->items);
            free(queue);
            return NULL;
        }
    }
    
    queue->filled_slots = sem_open(filled_name, O_CREAT | O_EXCL, 0644, 0);
    if (queue->filled_slots == SEM_FAILED) {
        /* Try without O_EXCL if it already exists */
        queue->filled_slots = sem_open(filled_name, O_CREAT, 0644, 0);
        if (queue->filled_slots == SEM_FAILED) {
            perror("Failed to create filled slots semaphore");
            sem_close(queue->empty_slots);
            sem_unlink(empty_name);
            pthread_cond_destroy(&queue->shutdown_cv);
            pthread_cond_destroy(&queue->not_full);
            pthread_cond_destroy(&queue->not_empty);
            pthread_mutex_destroy(&queue->lock);
            free(queue->items);
            free(queue);
            return NULL;
        }
    }
    
    /* Initialize statistics */
    memset(&queue->stats, 0, sizeof(queue_stats_t));
    queue->stats.start_time = time(NULL);
    queue->stats.peak_size = 0;
    
    /* Initialize performance tracking */
    queue->avg_enqueue_time_ms = 0.0;
    queue->avg_dequeue_time_ms = 0.0;
    queue->total_enqueue_time_ms = 0;
    queue->total_dequeue_time_ms = 0;
    
    /* Initialize state flags */
    queue->shutdown = 0;
    queue->closed = 0;
    
    printf("Queue created with capacity %d\n", capacity);
    return queue;
}

/* Destroy a connection queue */
void queue_destroy(connection_queue_t *queue) {
    if (!queue) return;
    
    /* Close the queue first */
    queue_close(queue);
    
    /* Close and unlink semaphores */
    if (queue->empty_slots != SEM_FAILED) {
        sem_close(queue->empty_slots);
        /* Note: We don't unlink here because other processes might still use them */
    }
    
    if (queue->filled_slots != SEM_FAILED) {
        sem_close(queue->filled_slots);
        /* Note: We don't unlink here because other processes might still use them */
    }
    
    /* Destroy synchronization primitives */
    pthread_cond_destroy(&queue->shutdown_cv);
    pthread_cond_destroy(&queue->not_full);
    pthread_cond_destroy(&queue->not_empty);
    pthread_mutex_destroy(&queue->lock);
    
    /* Free allocated memory */
    if (queue->items) {
        free(queue->items);
    }
    
    free(queue);
    
    printf("Queue destroyed\n");
}

/* Enqueue a connection (blocking) */
int queue_enqueue(connection_queue_t *queue, connection_info_t *conn_info) {
    if (!queue || !conn_info || queue->closed) {
        if (queue) queue->stats.error_count++;
        return -1;
    }
    
    struct timespec start_time, end_time;
    clock_gettime(CLOCK_MONOTONIC, &start_time);
    
    /* Use semaphores for inter-process synchronization */
    if (sem_wait(queue->empty_slots) == -1) {
        queue->stats.error_count++;
        return -1;
    }
    
    pthread_mutex_lock(&queue->lock);
    
    /* Check if queue is shutting down */
    if (queue->shutdown) {
        pthread_mutex_unlock(&queue->lock);
        sem_post(queue->empty_slots);  /* Release the semaphore */
        queue->stats.reject_count++;
        return -1;
    }
    
    /* Add connection to queue */
    memcpy(&queue->items[queue->rear], conn_info, sizeof(connection_info_t));
    queue->rear = (queue->rear + 1) % queue->capacity;
    queue->size++;
    
    /* Update statistics */
    queue->stats.enqueue_count++;
    if (queue->size > queue->stats.peak_size) {
        queue->stats.peak_size = queue->size;
    }
    
    /* Signal that queue is not empty */
    pthread_cond_signal(&queue->not_empty);
    
    pthread_mutex_unlock(&queue->lock);
    
    /* Post to filled slots semaphore */
    sem_post(queue->filled_slots);
    
    /* Update performance metrics */
    clock_gettime(CLOCK_MONOTONIC, &end_time);
    int operation_time = time_diff_ms(&start_time, &end_time);
    queue->total_enqueue_time_ms += operation_time;
    queue->avg_enqueue_time_ms = queue->total_enqueue_time_ms / 
                                 (double)queue->stats.enqueue_count;
    
    return 0;
}

/* Dequeue a connection (blocking) */
int queue_dequeue(connection_queue_t *queue, connection_info_t *conn_info) {
    if (!queue || !conn_info || queue->closed) {
        if (queue) queue->stats.error_count++;
        return -1;
    }
    
    struct timespec start_time, end_time;
    clock_gettime(CLOCK_MONOTONIC, &start_time);
    
    /* Use semaphores for inter-process synchronization */
    if (sem_wait(queue->filled_slots) == -1) {
        queue->stats.error_count++;
        return -1;
    }
    
    pthread_mutex_lock(&queue->lock);
    
    /* Check if queue is shutting down and empty */
    while (queue->shutdown && queue->size == 0) {
        pthread_mutex_unlock(&queue->lock);
        sem_post(queue->filled_slots);  /* Release the semaphore */
        return -1;  /* Queue is shutting down and empty */
    }
    
    /* Remove connection from queue */
    memcpy(conn_info, &queue->items[queue->front], sizeof(connection_info_t));
    init_connection_info(&queue->items[queue->front], -1);  /* Clear the slot */
    queue->front = (queue->front + 1) % queue->capacity;
    queue->size--;
    
    /* Update statistics */
    queue->stats.dequeue_count++;
    
    /* Signal that queue is not full */
    pthread_cond_signal(&queue->not_full);
    
    pthread_mutex_unlock(&queue->lock);
    
    /* Post to empty slots semaphore */
    sem_post(queue->empty_slots);
    
    /* Update performance metrics */
    clock_gettime(CLOCK_MONOTONIC, &end_time);
    int operation_time = time_diff_ms(&start_time, &end_time);
    queue->total_dequeue_time_ms += operation_time;
    queue->avg_dequeue_time_ms = queue->total_dequeue_time_ms / 
                                 (double)queue->stats.dequeue_count;
    
    return 0;
}

/* Try to enqueue with timeout */
int queue_try_enqueue(connection_queue_t *queue, connection_info_t *conn_info, int timeout_ms) {
    if (!queue || !conn_info || queue->closed) {
        if (queue) queue->stats.error_count++;
        return -1;
    }
    
    struct timespec timeout;
    clock_gettime(CLOCK_REALTIME, &timeout);
    timeout.tv_sec += timeout_ms / 1000;
    timeout.tv_nsec += (timeout_ms % 1000) * 1000000;
    
    /* Normalize nanoseconds */
    if (timeout.tv_nsec >= 1000000000) {
        timeout.tv_sec += timeout.tv_nsec / 1000000000;
        timeout.tv_nsec %= 1000000000;
    }
    
    /* Try to acquire empty slot with timeout */
    if (sem_timedwait(queue->empty_slots, &timeout) == -1) {
        if (errno == ETIMEDOUT) {
            queue->stats.timeout_count++;
            return -2;  /* Timeout */
        }
        queue->stats.error_count++;
        return -1;  /* Error */
    }
    
    pthread_mutex_lock(&queue->lock);
    
    /* Check if queue is shutting down */
    if (queue->shutdown) {
        pthread_mutex_unlock(&queue->lock);
        sem_post(queue->empty_slots);
        queue->stats.reject_count++;
        return -1;
    }
    
    /* Add connection to queue */
    memcpy(&queue->items[queue->rear], conn_info, sizeof(connection_info_t));
    queue->rear = (queue->rear + 1) % queue->capacity;
    queue->size++;
    
    /* Update statistics */
    queue->stats.enqueue_count++;
    if (queue->size > queue->stats.peak_size) {
        queue->stats.peak_size = queue->size;
    }
    
    /* Signal that queue is not empty */
    pthread_cond_signal(&queue->not_empty);
    
    pthread_mutex_unlock(&queue->lock);
    
    /* Post to filled slots semaphore */
    sem_post(queue->filled_slots);
    
    return 0;
}

/* Try to dequeue with timeout */
int queue_try_dequeue(connection_queue_t *queue, connection_info_t *conn_info, int timeout_ms) {
    if (!queue || !conn_info || queue->closed) {
        if (queue) queue->stats.error_count++;
        return -1;
    }
    
    struct timespec timeout;
    clock_gettime(CLOCK_REALTIME, &timeout);
    timeout.tv_sec += timeout_ms / 1000;
    timeout.tv_nsec += (timeout_ms % 1000) * 1000000;
    
    /* Normalize nanoseconds */
    if (timeout.tv_nsec >= 1000000000) {
        timeout.tv_sec += timeout.tv_nsec / 1000000000;
        timeout.tv_nsec %= 1000000000;
    }
    
    /* Try to acquire filled slot with timeout */
    if (sem_timedwait(queue->filled_slots, &timeout) == -1) {
        if (errno == ETIMEDOUT) {
            queue->stats.timeout_count++;
            return -2;  /* Timeout */
        }
        queue->stats.error_count++;
        return -1;  /* Error */
    }
    
    pthread_mutex_lock(&queue->lock);
    
    /* Check if queue is shutting down and empty */
    if (queue->shutdown && queue->size == 0) {
        pthread_mutex_unlock(&queue->lock);
        sem_post(queue->filled_slots);
        return -1;
    }
    
    /* Remove connection from queue */
    memcpy(conn_info, &queue->items[queue->front], sizeof(connection_info_t));
    init_connection_info(&queue->items[queue->front], -1);  /* Clear the slot */
    queue->front = (queue->front + 1) % queue->capacity;
    queue->size--;
    
    /* Update statistics */
    queue->stats.dequeue_count++;
    
    /* Signal that queue is not full */
    pthread_cond_signal(&queue->not_full);
    
    pthread_mutex_unlock(&queue->lock);
    
    /* Post to empty slots semaphore */
    sem_post(queue->empty_slots);
    
    return 0;
}

/* Non-blocking enqueue */
int queue_try_enqueue_nonblock(connection_queue_t *queue, connection_info_t *conn_info) {
    if (!queue || !conn_info || queue->closed) {
        if (queue) queue->stats.error_count++;
        return -1;
    }
    
    /* Try to acquire empty slot without blocking */
    if (sem_trywait(queue->empty_slots) == -1) {
        if (errno == EAGAIN) {
            queue->stats.reject_count++;
            return -2;  /* Queue full */
        }
        queue->stats.error_count++;
        return -1;  /* Error */
    }
    
    pthread_mutex_lock(&queue->lock);
    
    /* Check if queue is shutting down */
    if (queue->shutdown) {
        pthread_mutex_unlock(&queue->lock);
        sem_post(queue->empty_slots);
        queue->stats.reject_count++;
        return -1;
    }
    
    /* Add connection to queue */
    memcpy(&queue->items[queue->rear], conn_info, sizeof(connection_info_t));
    queue->rear = (queue->rear + 1) % queue->capacity;
    queue->size++;
    
    /* Update statistics */
    queue->stats.enqueue_count++;
    if (queue->size > queue->stats.peak_size) {
        queue->stats.peak_size = queue->size;
    }
    
    /* Signal that queue is not empty */
    pthread_cond_signal(&queue->not_empty);
    
    pthread_mutex_unlock(&queue->lock);
    
    /* Post to filled slots semaphore */
    sem_post(queue->filled_slots);
    
    return 0;
}

/* Non-blocking dequeue */
int queue_try_dequeue_nonblock(connection_queue_t *queue, connection_info_t *conn_info) {
    if (!queue || !conn_info || queue->closed) {
        if (queue) queue->stats.error_count++;
        return -1;
    }
    
    /* Try to acquire filled slot without blocking */
    if (sem_trywait(queue->filled_slots) == -1) {
        if (errno == EAGAIN) {
            return -2;  /* Queue empty */
        }
        queue->stats.error_count++;
        return -1;  /* Error */
    }
    
    pthread_mutex_lock(&queue->lock);
    
    /* Check if queue is shutting down and empty */
    if (queue->shutdown && queue->size == 0) {
        pthread_mutex_unlock(&queue->lock);
        sem_post(queue->filled_slots);
        return -1;
    }
    
    /* Remove connection from queue */
    memcpy(conn_info, &queue->items[queue->front], sizeof(connection_info_t));
    init_connection_info(&queue->items[queue->front], -1);  /* Clear the slot */
    queue->front = (queue->front + 1) % queue->capacity;
    queue->size--;
    
    /* Update statistics */
    queue->stats.dequeue_count++;
    
    /* Signal that queue is not full */
    pthread_cond_signal(&queue->not_full);
    
    pthread_mutex_unlock(&queue->lock);
    
    /* Post to empty slots semaphore */
    sem_post(queue->empty_slots);
    
    return 0;
}

/* Check if queue is empty */
int queue_is_empty(const connection_queue_t *queue) {
    if (!queue) return 1;
    
    int empty;
    /* We need to lock to get consistent state */
    pthread_mutex_lock((pthread_mutex_t*)&queue->lock);
    empty = (queue->size == 0);
    pthread_mutex_unlock((pthread_mutex_t*)&queue->lock);
    
    return empty;
}

/* Check if queue is full */
int queue_is_full(const connection_queue_t *queue) {
    if (!queue) return 1;
    
    int full;
    pthread_mutex_lock((pthread_mutex_t*)&queue->lock);
    full = (queue->size >= queue->capacity);
    pthread_mutex_unlock((pthread_mutex_t*)&queue->lock);
    
    return full;
}

/* Get current queue size */
int queue_get_size(const connection_queue_t *queue) {
    if (!queue) return 0;
    
    int size;
    pthread_mutex_lock((pthread_mutex_t*)&queue->lock);
    size = queue->size;
    pthread_mutex_unlock((pthread_mutex_t*)&queue->lock);
    
    return size;
}

/* Get queue capacity */
int queue_get_capacity(const connection_queue_t *queue) {
    return queue ? queue->capacity : 0;
}

/* Shutdown the queue (stop accepting new connections) */
void queue_shutdown(connection_queue_t *queue) {
    if (!queue) return;
    
    pthread_mutex_lock(&queue->lock);
    queue->shutdown = 1;
    
    /* Wake up all waiting threads */
    pthread_cond_broadcast(&queue->not_empty);
    pthread_cond_broadcast(&queue->not_full);
    pthread_cond_broadcast(&queue->shutdown_cv);
    
    pthread_mutex_unlock(&queue->lock);
    
    printf("Queue shutdown initiated\n");
}

/* Close the queue (stop all operations) */
void queue_close(connection_queue_t *queue) {
    if (!queue) return;
    
    queue_shutdown(queue);  /* First shutdown */
    
    pthread_mutex_lock(&queue->lock);
    queue->closed = 1;
    
    /* Clear all pending connections */
    for (int i = 0; i < queue->capacity; i++) {
        if (queue->items[i].socket_fd != -1) {
            close(queue->items[i].socket_fd);
            init_connection_info(&queue->items[i], -1);
        }
    }
    
    queue->size = 0;
    queue->front = 0;
    queue->rear = 0;
    
    pthread_mutex_unlock(&queue->lock);
    
    printf("Queue closed\n");
}

/* Check if queue is shutdown */
int queue_is_shutdown(const connection_queue_t *queue) {
    if (!queue) return 1;
    
    int shutdown;
    pthread_mutex_lock((pthread_mutex_t*)&queue->lock);
    shutdown = queue->shutdown;
    pthread_mutex_unlock((pthread_mutex_t*)&queue->lock);
    
    return shutdown;
}

/* Check if queue is closed */
int queue_is_closed(const connection_queue_t *queue) {
    if (!queue) return 1;
    
    int closed;
    pthread_mutex_lock((pthread_mutex_t*)&queue->lock);
    closed = queue->closed;
    pthread_mutex_unlock((pthread_mutex_t*)&queue->lock);
    
    return closed;
}

/* Clear all connections from queue */
void queue_clear(connection_queue_t *queue) {
    if (!queue) return;
    
    pthread_mutex_lock(&queue->lock);
    
    /* Close all socket connections */
    for (int i = 0; i < queue->capacity; i++) {
        if (queue->items[i].socket_fd != -1) {
            close(queue->items[i].socket_fd);
            init_connection_info(&queue->items[i], -1);
        }
    }
    
    queue->size = 0;
    queue->front = 0;
    queue->rear = 0;
    
    /* Reset semaphores to initial state */
    int sem_value;
    
    /* Drain all filled slots */
    while (sem_getvalue(queue->filled_slots, &sem_value) == 0 && sem_value > 0) {
        sem_trywait(queue->filled_slots);
    }
    
    /* Fill all empty slots */
    while (sem_getvalue(queue->empty_slots, &sem_value) == 0 && sem_value < queue->capacity) {
        sem_post(queue->empty_slots);
    }
    
    pthread_mutex_unlock(&queue->lock);
    
    printf("Queue cleared\n");
}

/* Get queue statistics */
void queue_get_stats(const connection_queue_t *queue, queue_stats_t *stats) {
    if (!queue || !stats) return;
    
    pthread_mutex_lock((pthread_mutex_t*)&queue->lock);
    memcpy(stats, &queue->stats, sizeof(queue_stats_t));
    pthread_mutex_unlock((pthread_mutex_t*)&queue->lock);
}

/* Print queue statistics */
void queue_print_stats(const connection_queue_t *queue) {
    if (!queue) return;
    
    pthread_mutex_lock((pthread_mutex_t*)&queue->lock);
    
    time_t now = time(NULL);
    double uptime = difftime(now, queue->stats.start_time);
    
    printf("\n=== Connection Queue Statistics ===\n");
    printf("Queue Status: %s%s\n", 
           queue->shutdown ? "SHUTDOWN " : "RUNNING ",
           queue->closed ? "CLOSED" : "");
    printf("Capacity: %d, Current Size: %d\n", queue->capacity, queue->size);
    printf("Uptime: %.0f seconds\n", uptime);
    printf("Enqueued: %lu, Dequeued: %lu\n", 
           queue->stats.enqueue_count, queue->stats.dequeue_count);
    printf("Rejected: %lu, Timeouts: %lu, Errors: %lu\n",
           queue->stats.reject_count, queue->stats.timeout_count, queue->stats.error_count);
    printf("Peak Size: %lu\n", queue->stats.peak_size);
    printf("Average Enqueue Time: %.2f ms\n", queue->avg_enqueue_time_ms);
    printf("Average Dequeue Time: %.2f ms\n", queue->avg_dequeue_time_ms);
    printf("Queue Utilization: %.1f%%\n", queue_get_utilization(queue) * 100);
    printf("Enqueue Throughput: %.1f/sec\n", queue_get_throughput_enqueue(queue));
    printf("Dequeue Throughput: %.1f/sec\n", queue_get_throughput_dequeue(queue));
    printf("===================================\n");
    
    pthread_mutex_unlock((pthread_mutex_t*)&queue->lock);
}

/* Calculate queue utilization (0.0 to 1.0) */
double queue_get_utilization(const connection_queue_t *queue) {
    if (!queue || queue->capacity == 0) return 0.0;
    
    double utilization;
    pthread_mutex_lock((pthread_mutex_t*)&queue->lock);
    utilization = (double)queue->size / queue->capacity;
    pthread_mutex_unlock((pthread_mutex_t*)&queue->lock);
    
    return utilization;
}

/* Calculate enqueue throughput (operations per second) */
double queue_get_throughput_enqueue(const connection_queue_t *queue) {
    if (!queue) return 0.0;
    
    time_t now = time(NULL);
    double uptime = difftime(now, queue->stats.start_time);
    
    if (uptime <= 0) return 0.0;
    
    return queue->stats.enqueue_count / uptime;
}

/* Calculate dequeue throughput (operations per second) */
double queue_get_throughput_dequeue(const connection_queue_t *queue) {
    if (!queue) return 0.0;
    
    time_t now = time(NULL);
    double uptime = difftime(now, queue->stats.start_time);
    
    if (uptime <= 0) return 0.0;
    
    return queue->stats.dequeue_count / uptime;
}

/* Calculate average wait time in queue */
double queue_get_avg_wait_time(const connection_queue_t *queue) {
    if (!queue || queue->stats.dequeue_count == 0) return 0.0;
    
    /* This is a simplified calculation */
    /* In a real implementation, you would track actual wait times */
    return queue->avg_enqueue_time_ms + queue->avg_dequeue_time_ms;
}

/* Peek at the front connection without removing it */
int queue_peek_front(const connection_queue_t *queue, connection_info_t *conn_info) {
    if (!queue || !conn_info || queue->size == 0) {
        return -1;
    }
    
    pthread_mutex_lock((pthread_mutex_t*)&queue->lock);
    
    if (queue->size == 0) {
        pthread_mutex_unlock((pthread_mutex_t*)&queue->lock);
        return -1;
    }
    
    memcpy(conn_info, &queue->items[queue->front], sizeof(connection_info_t));
    
    pthread_mutex_unlock((pthread_mutex_t*)&queue->lock);
    
    return 0;
}

/* Remove connections older than specified age */
int queue_remove_oldest(connection_queue_t *queue, int max_age_seconds) {
    if (!queue || max_age_seconds <= 0) return 0;
    
    int removed = 0;
    time_t now = time(NULL);
    
    pthread_mutex_lock(&queue->lock);
    
    /* Check each connection in queue */
    for (int i = 0; i < queue->size; i++) {
        int idx = (queue->front + i) % queue->capacity;
        
        if (difftime(now, queue->items[idx].arrival_time) > max_age_seconds) {
            /* Close socket and remove */
            close(queue->items[idx].socket_fd);
            init_connection_info(&queue->items[idx], -1);
            
            /* Shift remaining elements */
            for (int j = i + 1; j < queue->size; j++) {
                int src_idx = (queue->front + j) % queue->capacity;
                int dst_idx = (queue->front + j - 1) % queue->capacity;
                memcpy(&queue->items[dst_idx], &queue->items[src_idx], 
                       sizeof(connection_info_t));
                init_connection_info(&queue->items[src_idx], -1);
            }
            
            queue->size--;
            queue->rear = (queue->rear - 1 + queue->capacity) % queue->capacity;
            removed++;
            i--;  /* Check same index again (new element moved here) */
        }
    }
    
    /* Update semaphores to reflect removed connections */
    if (removed > 0) {
        for (int i = 0; i < removed; i++) {
            sem_post(queue->empty_slots);
            sem_trywait(queue->filled_slots);
        }
    }
    
    pthread_mutex_unlock(&queue->lock);
    
    return removed;
}

/* Check if queue contains specific socket */
int queue_contains(const connection_queue_t *queue, int socket_fd) {
    if (!queue || socket_fd < 0) return 0;
    
    int found = 0;
    
    pthread_mutex_lock((pthread_mutex_t*)&queue->lock);
    
    for (int i = 0; i < queue->size; i++) {
        int idx = (queue->front + i) % queue->capacity;
        if (queue->items[idx].socket_fd == socket_fd) {
            found = 1;
            break;
        }
    }
    
    pthread_mutex_unlock((pthread_mutex_t*)&queue->lock);
    
    return found;
}

/* Set priority for a connection in queue */
void queue_set_priority(connection_queue_t *queue, int socket_fd, int priority) {
    if (!queue || socket_fd < 0) return;
    
    pthread_mutex_lock(&queue->lock);
    
    for (int i = 0; i < queue->size; i++) {
        int idx = (queue->front + i) % queue->capacity;
        if (queue->items[idx].socket_fd == socket_fd) {
            queue->items[idx].priority = priority;
            
            /* If priority is high, move to front */
            if (priority > 0) {
                /* Simple bubble-up based on priority */
                for (int j = i; j > 0; j--) {
                    int curr_idx = (queue->front + j) % queue->capacity;
                    int prev_idx = (queue->front + j - 1) % queue->capacity;
                    
                    if (queue->items[curr_idx].priority > queue->items[prev_idx].priority) {
                        /* Swap */
                        connection_info_t temp = queue->items[curr_idx];
                        queue->items[curr_idx] = queue->items[prev_idx];
                        queue->items[prev_idx] = temp;
                    } else {
                        break;
                    }
                }
            }
            break;
        }
    }
    
    pthread_mutex_unlock(&queue->lock);
}

/* Thread safety utility functions */
void queue_lock(connection_queue_t *queue) {
    if (queue) pthread_mutex_lock(&queue->lock);
}

void queue_unlock(connection_queue_t *queue) {
    if (queue) pthread_mutex_unlock(&queue->lock);
}

void queue_wait_not_empty(connection_queue_t *queue) {
    if (!queue) return;
    
    while (queue->size == 0 && !queue->shutdown) {
        pthread_cond_wait(&queue->not_empty, &queue->lock);
    }
}

void queue_wait_not_full(connection_queue_t *queue) {
    if (!queue) return;
    
    while (queue->size >= queue->capacity && !queue->shutdown) {
        pthread_cond_wait(&queue->not_full, &queue->lock);
    }
}

void queue_signal_not_empty(connection_queue_t *queue) {
    if (queue) pthread_cond_signal(&queue->not_empty);
}

void queue_signal_not_full(connection_queue_t *queue) {
    if (queue) pthread_cond_signal(&queue->not_full);
}

/* Debugging: Dump queue contents */
void queue_dump(const connection_queue_t *queue) {
    if (!queue) {
        printf("Queue is NULL\n");
        return;
    }
    
    pthread_mutex_lock((pthread_mutex_t*)&queue->lock);
    
    printf("\n=== Queue Dump ===\n");
    printf("Capacity: %d, Size: %d, Front: %d, Rear: %d\n",
           queue->capacity, queue->size, queue->front, queue->rear);
    printf("Shutdown: %d, Closed: %d\n", queue->shutdown, queue->closed);
    printf("\nContents:\n");
    
    if (queue->size == 0) {
        printf("  [Empty]\n");
    } else {
        for (int i = 0; i < queue->size; i++) {
            int idx = (queue->front + i) % queue->capacity;
            printf("  [%d] Socket: %d, Priority: %d, Age: %lds\n",
                   i, queue->items[idx].socket_fd,
                   queue->items[idx].priority,
                   time(NULL) - queue->items[idx].arrival_time);
        }
    }
    
    printf("==================\n");
    
    pthread_mutex_unlock((pthread_mutex_t*)&queue->lock);
}

/* Validate queue consistency */
void queue_validate(const connection_queue_t *queue) {
    if (!queue) {
        printf("Queue validation: NULL queue\n");
        return;
    }
    
    pthread_mutex_lock((pthread_mutex_t*)&queue->lock);
    
    int errors = 0;
    
    /* Check size consistency */
    int calculated_size = (queue->rear - queue->front + queue->capacity) % queue->capacity;
    if (queue->size != calculated_size) {
        printf("ERROR: Size mismatch: stored=%d, calculated=%d\n",
               queue->size, calculated_size);
        errors++;
    }
    
    /* Check bounds */
    if (queue->front < 0 || queue->front >= queue->capacity) {
        printf("ERROR: Front index out of bounds: %d\n", queue->front);
        errors++;
    }
    
    if (queue->rear < 0 || queue->rear >= queue->capacity) {
        printf("ERROR: Rear index out of bounds: %d\n", queue->rear);
        errors++;
    }
    
    if (queue->size < 0 || queue->size > queue->capacity) {
        printf("ERROR: Size out of bounds: %d\n", queue->size);
        errors++;
    }
    
    /* Check for holes in occupied slots */
    int occupied_count = 0;
    for (int i = 0; i < queue->capacity; i++) {
        if (queue->items[i].socket_fd != -1) {
            occupied_count++;
        }
    }
    
    if (occupied_count != queue->size) {
        printf("ERROR: Occupied count mismatch: occupied=%d, size=%d\n",
               occupied_count, queue->size);
        errors++;
    }
    
    /* Check semaphore values */
    int empty_val, filled_val;
    sem_getvalue(queue->empty_slots, &empty_val);
    sem_getvalue(queue->filled_slots, &filled_val);
    
    if (empty_val + filled_val != queue->capacity) {
        printf("ERROR: Semaphore sum mismatch: empty=%d, filled=%d, capacity=%d\n",
               empty_val, filled_val, queue->capacity);
        errors++;
    }
    
    if (filled_val != queue->size) {
        printf("ERROR: Filled slots mismatch: semaphore=%d, queue=%d\n",
               filled_val, queue->size);
        errors++;
    }
    
    pthread_mutex_unlock((pthread_mutex_t*)&queue->lock);
    
    if (errors == 0) {
        printf("Queue validation: PASSED\n");
    } else {
        printf("Queue validation: FAILED (%d errors)\n", errors);
    }
}