// worker.c - ATUALIZADO para corrigir sig_atomic_t
#include "worker.h"
#include "thread_pool.h"
#include "cache.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>

volatile sig_atomic_t worker_running = 1;  // ← CORRIGIDO

void worker_signal_handler(int sig) {
    (void)sig;
    worker_running = 0;
}

void worker_main(worker_context_t* ctx) {
    signal(SIGTERM, worker_signal_handler);
    signal(SIGINT, worker_signal_handler);
    
    printf("Worker %d (PID %d) starting with cache and thread pool...\n", 
           ctx->worker_id, getpid());
    
    // Inicializa cache (será usada pelo http.c)
    lru_cache_t* cache = cache_create(MAX_CACHE_SIZE);
    if (!cache) {
        fprintf(stderr, "Worker %d: Failed to create cache\n", ctx->worker_id);
        free(ctx);
        exit(EXIT_FAILURE);
    }
    
    // Cria thread pool
    thread_pool_t* thread_pool = create_thread_pool(
        ctx->config->threads_per_worker,
        ctx->worker_id,
        ctx->shared_data,
        ctx->semaphores,
        ctx->config
    );
    
    if (!thread_pool) {
        fprintf(stderr, "Worker %d: Failed to create thread pool\n", ctx->worker_id);
        cache_destroy(cache);
        free(ctx);
        exit(EXIT_FAILURE);
    }
    
    printf("Worker %d (PID %d) ready with %d threads and cache\n", 
           ctx->worker_id, getpid(), ctx->config->threads_per_worker);
    
    // Loop principal do worker
    while (worker_running) {
        int client_fd = dequeue_connection_nonblock(ctx->shared_data,
                                                  ctx->semaphores->empty_slots,
                                                  ctx->semaphores->filled_slots,
                                                  ctx->semaphores->queue_mutex);
        
        if (client_fd >= 0 && worker_running) {
            add_task_to_pool(thread_pool, client_fd);
        } else if (worker_running) {
            usleep(100000);  // 100ms
        }
    }
    
    // Shutdown gracioso
    printf("Worker %d (PID %d) shutting down...\n", ctx->worker_id, getpid());
    
    // Mostra estatísticas da cache antes de terminar
    cache_print_stats(cache);
    
    destroy_thread_pool(thread_pool);
    cache_destroy(cache);
    
    printf("Worker %d (PID %d) shut down complete\n", ctx->worker_id, getpid());
    free(ctx);
    exit(EXIT_SUCCESS);
}