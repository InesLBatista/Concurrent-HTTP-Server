#include "worker.h"
#include "thread_pool.h"
#include "http.h"
#include "logger.h"
#include "shared_mem.h"  /* ADICIONADO para estatísticas compartilhadas */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <sys/time.h>    /* Para gettimeofday */

/* Definir constantes se não definidas no header */
#ifndef MAX_QUEUE_SIZE
#define MAX_QUEUE_SIZE 100
#endif

volatile sig_atomic_t worker_running = 1;

void worker_signal_handler(int signum) {
    worker_running = 0;
    printf("Worker received signal %d, shutting down...\n", signum);
}

int dequeue_connection(shared_data_t* data, semaphores_t* sems) {
    if (sem_wait(sems->filled_slots) == -1) {
        if (errno == EINTR) return -1;
        perror("sem_wait filled_slots");
        return -1;
    }
    
    if (sem_wait(sems->queue_mutex) == -1) {
        sem_post(sems->filled_slots);
        if (errno == EINTR) return -1;
        perror("sem_wait queue_mutex");
        return -1;
    }
    
    int client_fd = -1;
    
    /* Usar função sincronizada para desenfileirar */
    client_fd = shared_queue_dequeue(&data->queue);
    
    if (client_fd == -1) {
        /* Fila vazia */
        sem_post(sems->queue_mutex);
        sem_post(sems->filled_slots);
        return -1;
    }
    
    printf("Worker dequeued connection (fd: %d), queue size: %d\n", 
           client_fd, data->queue.size);
    
    sem_post(sems->queue_mutex);
    sem_post(sems->empty_slots);
    
    return client_fd;
}

/* Função auxiliar para medir tempo em milissegundos */
static double get_time_ms() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000.0 + tv.tv_usec / 1000.0;
}

/* Função principal do thread worker - ATUALIZADA com tracking de tempo */
void* worker_thread_func(void* arg) {
    worker_thread_arg_t* thread_arg = (worker_thread_arg_t*)arg;
    thread_pool_t* pool = (thread_pool_t*)thread_arg->pool;
    
    printf("Worker %d thread %lu started\n", 
           thread_arg->worker_id, (unsigned long)pthread_self());
    
    while (worker_running) {
        pthread_mutex_lock(&pool->mutex);
        
        while (!pool->shutdown && pool->queue_count == 0) {
            pthread_cond_wait(&pool->cond, &pool->mutex);
        }
        
        if (pool->shutdown) {
            pthread_mutex_unlock(&pool->mutex);
            break;
        }
        
        if (pool->queue_count > 0) {
            int client_fd = pool->queue[pool->queue_front];
            pool->queue_front = (pool->queue_front + 1) % pool->queue_size;
            pool->queue_count--;
            pthread_mutex_unlock(&pool->mutex);
            
            /* Medir tempo de início */
            double start_time_ms = get_time_ms();
            
            /* Processar a requisição HTTP */
            process_http_request(client_fd, thread_arg->config,
                                 thread_arg->shared_data,
                                 thread_arg->semaphores);
            
            /* Medir tempo de fim e calcular duração */
            double end_time_ms = get_time_ms();
            double response_time_ms = end_time_ms - start_time_ms;
            
            /* Fechar socket após processamento */
            close(client_fd);
            
            /* ATUALIZAR: NÃO precisamos mais chamar update_statistics aqui
               porque o process_http_request já deve fazer isso */
            
        } else {
            pthread_mutex_unlock(&pool->mutex);
        }
    }
    
    printf("Worker %d thread %lu exiting\n", 
           thread_arg->worker_id, (unsigned long)pthread_self());
    return NULL;
}

/* Versão atualizada de update_statistics para usar shared_mem.h */
void update_statistics(shared_data_t* data, semaphores_t* sems, 
                       int status_code, size_t bytes) {
    /* Esta função agora é uma wrapper para shared_stats_update_request
       Mantemos por compatibilidade com código existente */
    
    /* Calcular tempo de resposta aproximado (0.0 pois não temos o tempo real aqui)
       Em um cenário real, você passaria o tempo de resposta como parâmetro */
    double response_time_ms = 0.0;
    
    /* Usar a nova função sincronizada */
    shared_stats_update_request(data, status_code, bytes, response_time_ms);
}

/* Função para atualizar cache statistics */
void update_cache_statistics(shared_data_t* data, int cache_hit) {
    if (!data) return;
    
    shared_stats_update_cache(data, cache_hit);
}

/* Função para atualizar error statistics */
void update_error_statistics(shared_data_t* data) {
    if (!data) return;
    
    shared_stats_update_error(data);
}

void worker_main(int worker_id, shared_data_t* shared_data, 
                 semaphores_t* semaphores, server_config_t* config) {
    /* Set up signal handler */
    if (signal(SIGINT, worker_signal_handler) == SIG_ERR) {
        perror("signal SIGINT");
    }
    
    if (signal(SIGTERM, worker_signal_handler) == SIG_ERR) {
        perror("signal SIGTERM");
    }
    
    printf("Worker %d started (PID: %d)\n", worker_id, getpid());
    
    /* Create thread pool - SEM verificação de retorno */
    thread_pool_t pool;
    thread_pool_init(&pool, config->threads_per_worker, config->max_queue_size);
    
    /* Create thread arguments */
    worker_thread_arg_t thread_arg;
    thread_arg.worker_id = worker_id;
    thread_arg.shared_data = shared_data;
    thread_arg.semaphores = semaphores;
    thread_arg.config = config;
    thread_arg.pool = &pool;
    
    /* Create worker threads */
    pthread_t* threads = malloc(sizeof(pthread_t) * config->threads_per_worker);
    if (!threads) {
        perror("malloc");
        thread_pool_destroy(&pool);
        return;
    }
    
    for (int i = 0; i < config->threads_per_worker; i++) {
        if (pthread_create(&threads[i], NULL, worker_thread_func, &thread_arg) != 0) {
            perror("pthread_create");
            
            /* Shutdown already created threads */
            for (int j = 0; j < i; j++) {
                pthread_join(threads[j], NULL);
            }
            
            free(threads);
            thread_pool_destroy(&pool);
            return;
        }
    }
    
    /* Statistics display timer */
    time_t last_stat_display = time(NULL);
    int stats_interval = 60; /* Mostrar estatísticas a cada 60 segundos nos workers */
    
    /* Main worker loop */
    while (worker_running) {
        /* Get connection from shared queue */
        int client_fd = dequeue_connection(shared_data, semaphores);
        
        if (client_fd >= 0) {
            /* Add to thread pool queue */
            pthread_mutex_lock(&pool.mutex);
            
            if (pool.queue_count < pool.queue_size) {
                pool.queue[pool.queue_rear] = client_fd;
                pool.queue_rear = (pool.queue_rear + 1) % pool.queue_size;
                pool.queue_count++;
                
                /* Signal waiting threads */
                pthread_cond_signal(&pool.cond);
            } else {
                /* Thread pool queue is full - atualizar estatísticas de erro */
                send_http_error(client_fd, 503, "Service Unavailable", config);
                
                /* Registrar erro de fila cheia */
                update_error_statistics(shared_data);
                
                close(client_fd);
            }
            
            pthread_mutex_unlock(&pool.mutex);
        } else if (client_fd == -1 && errno == EINTR) {
            /* Interrupted by signal */
            continue;
        } else {
            /* No connection available, sleep a bit */
            usleep(10000); /* 10ms */
        }
        
        /* Opcional: Mostrar estatísticas periódicas nos workers também */
        time_t now = time(NULL);
        if (difftime(now, last_stat_display) >= stats_interval) {
            printf("\n[Worker %d] Worker statistics at %ld seconds:\n", 
                   worker_id, now);
            printf("  Thread pool queue: %d/%d\n", 
                   pool.queue_count, pool.queue_size);
            printf("  Shared queue size: %d/%d\n",
                   shared_data->queue.size, shared_data->queue.capacity);
            last_stat_display = now;
        }
    }
    
    /* Shutdown thread pool */
    printf("Worker %d shutting down thread pool...\n", worker_id);
    thread_pool_shutdown(&pool);
    
    /* Wait for threads to finish */
    for (int i = 0; i < config->threads_per_worker; i++) {
        pthread_join(threads[i], NULL);
    }
    
    free(threads);
    thread_pool_destroy(&pool);
    
    printf("Worker %d exited\n", worker_id);
}

/* Função auxiliar para logging de cache (para Feature 4) */
void log_cache_operation(int worker_id, const char* filename, int cache_hit) {
    printf("[Worker %d] Cache %s for file: %s\n", 
           worker_id, cache_hit ? "HIT" : "MISS", filename);
}

/* Função auxiliar para medir e logar tempo de operação */
void log_operation_time(int worker_id, const char* operation, 
                       double start_time_ms, double end_time_ms) {
    double duration_ms = end_time_ms - start_time_ms;
    if (duration_ms > 100.0) { /* Log apenas operações lentas */
        printf("[Worker %d] Slow operation: %s took %.2f ms\n", 
               worker_id, operation, duration_ms);
    }
}