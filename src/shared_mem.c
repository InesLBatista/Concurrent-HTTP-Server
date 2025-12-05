#include "shared_mem.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

/* Criar memória compartilhada */
shared_data_t *shared_memory_create(size_t queue_size) {
    size_t shm_size = sizeof(shared_data_t);
    
    /* Criar segmento de memória compartilhada */
    int shm_fd = shm_open("/httpserver_shm", O_CREAT | O_RDWR, 0644);
    if (shm_fd == -1) {
        perror("shm_open");
        return NULL;
    }
    
    /* Definir tamanho */
    if (ftruncate(shm_fd, shm_size) == -1) {
        perror("ftruncate");
        close(shm_fd);
        return NULL;
    }
    
    /* Mapear na memória */
    shared_data_t *data = mmap(NULL, shm_size, 
                               PROT_READ | PROT_WRITE, 
                               MAP_SHARED, shm_fd, 0);
    if (data == MAP_FAILED) {
        perror("mmap");
        close(shm_fd);
        return NULL;
    }
    
    close(shm_fd);
    
    /* Inicializar estrutura */
    memset(data, 0, sizeof(shared_data_t));
    
    /* Inicializar fila */
    data->queue.capacity = (queue_size < SHARED_QUEUE_MAX_SIZE) ? 
                          queue_size : SHARED_QUEUE_MAX_SIZE;
    data->queue.size = 0;
    data->queue.front = 0;
    data->queue.rear = 0;
    
    /* Inicializar estatísticas */
    stats_init(&data->stats);
    
    /* Inicializar mutex para memória compartilhada */
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
    pthread_mutex_init(&data->stats_mutex, &attr);
    pthread_mutexattr_destroy(&attr);
    
    strncpy(data->shm_name, "/httpserver_shm", sizeof(data->shm_name) - 1);
    
    return data;
}

/* Anexar a memória compartilhada existente */
shared_data_t *shared_memory_attach(const char *shm_name) {
    if (!shm_name) {
        shm_name = "/httpserver_shm";
    }
    
    int shm_fd = shm_open(shm_name, O_RDWR, 0);
    if (shm_fd == -1) {
        perror("shm_open");
        return NULL;
    }
    
    shared_data_t *data = mmap(NULL, sizeof(shared_data_t), 
                               PROT_READ | PROT_WRITE, 
                               MAP_SHARED, shm_fd, 0);
    close(shm_fd);
    
    if (data == MAP_FAILED) {
        perror("mmap");
        return NULL;
    }
    
    return data;
}

/* Destruir memória compartilhada */
void shared_memory_destroy(shared_data_t *data) {
    if (!data) return;
    
    /* Destruir mutex */
    pthread_mutex_destroy(&data->stats_mutex);
    
    /* Desmapear */
    munmap(data, sizeof(shared_data_t));
    
    /* Remover segmento */
    shm_unlink(data->shm_name);
}

/* Operações de fila sincronizadas */
int shared_queue_enqueue(shared_queue_t *queue, int socket_fd) {
    if (!queue || queue->size >= queue->capacity) {
        return -1; /* Fila cheia */
    }
    
    queue->sockets[queue->rear] = socket_fd;
    queue->rear = (queue->rear + 1) % queue->capacity;
    queue->size++;
    
    return 0;
}

int shared_queue_dequeue(shared_queue_t *queue) {
    if (!queue || queue->size == 0) {
        return -1; /* Fila vazia */
    }
    
    int socket_fd = queue->sockets[queue->front];
    queue->front = (queue->front + 1) % queue->capacity;
    queue->size--;
    
    return socket_fd;
}

int shared_queue_is_empty(const shared_queue_t *queue) {
    return (!queue || queue->size == 0);
}

int shared_queue_is_full(const shared_queue_t *queue) {
    return (queue && queue->size >= queue->capacity);
}

/* Funções de estatísticas sincronizadas */
void shared_stats_update_request(shared_data_t *data, int status_code, 
                                size_t bytes_sent, double response_time_ms) {
    if (!data) return;
    
    pthread_mutex_lock(&data->stats_mutex);
    stats_update_request(&data->stats, status_code, bytes_sent, response_time_ms);
    pthread_mutex_unlock(&data->stats_mutex);
}

void shared_stats_update_connection(shared_data_t *data, int is_new_connection) {
    if (!data) return;
    
    pthread_mutex_lock(&data->stats_mutex);
    stats_update_request(&data->stats, 
                        is_new_connection ? 0 : -1, /* 0 = nova, -1 = fechada */
                        0, 0.0);
    pthread_mutex_unlock(&data->stats_mutex);
}

void shared_stats_update_cache(shared_data_t *data, int cache_hit) {
    if (!data) return;
    
    pthread_mutex_lock(&data->stats_mutex);
    stats_update_cache(&data->stats, cache_hit);
    pthread_mutex_unlock(&data->stats_mutex);
}

void shared_stats_update_error(shared_data_t *data) {
    if (!data) return;
    
    pthread_mutex_lock(&data->stats_mutex);
    stats_update_error(&data->stats);
    pthread_mutex_unlock(&data->stats_mutex);
}

/* Debugging */
void shared_memory_print_status(const shared_data_t *data) {
    if (!data) {
        printf("Shared memory: NULL\n");
        return;
    }
    
    printf("Shared Memory Status:\n");
    printf("  Queue: %d/%d connections\n", 
           data->queue.size, data->queue.capacity);
    printf("  Stats: %ld total requests\n", data->stats.total_requests);
}

void shared_memory_print_stats(const shared_data_t *data) {
    if (!data) return;
    
    pthread_mutex_lock(&data->stats_mutex);
    stats_print(&data->stats);
    pthread_mutex_unlock(&data->stats_mutex);
}