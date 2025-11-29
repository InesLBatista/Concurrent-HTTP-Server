// master.c
// Inês Batista, Maria Quinteiro

// Implementa o processo master que aceita conexões e coordena workers.
// Inclui sistema de estatísticas periódicas a cada 30 segundos.

#include "master.h"
#include "stats.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/wait.h>
#include <string.h>
#include <pthread.h>

// Variável global para controlar execução do servidor
volatile sig_atomic_t keep_running = 1;

// Handler para sinais SIGINT (Ctrl+C) e SIGTERM (kill)
void signal_handler(int sig) {
    keep_running = 0;
    printf("\nReceived signal %d - Initiating graceful shutdown...\n", sig);
}

// Função wrapper para stats periódicas (compatível com pthread)
void* stats_thread_wrapper(void* arg) {
    master_context_t* ctx = (master_context_t*)arg;
    stats_print_periodic(ctx->shared_data, ctx->semaphores, 30);
    return NULL;
}

// Cria e configura socket do servidor
int create_server_socket(int port) {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("socket creation failed");
        return -1;
    }
    
    int opt = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt failed");
        close(sockfd);
        return -1;
    }
    
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    
    if (bind(sockfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind failed");
        close(sockfd);
        return -1;
    }
    
    if (listen(sockfd, 128) < 0) {
        perror("listen failed");
        close(sockfd);
        return -1;
    }
    
    return sockfd;
}

// Inicializa contexto do master process
master_context_t* init_master(server_config_t* config) {
    master_context_t* ctx = malloc(sizeof(master_context_t));
    if (!ctx) {
        perror("malloc failed for master context");
        return NULL;
    }
    
    ctx->config = config;
    
    ctx->worker_pids = malloc(sizeof(pid_t) * config->num_workers);
    if (!ctx->worker_pids) {
        perror("malloc failed for worker PIDs");
        free(ctx);
        return NULL;
    }
    
    ctx->shared_data = create_shared_memory();
    if (!ctx->shared_data) {
        fprintf(stderr, "Failed to create shared memory\n");
        free(ctx->worker_pids);
        free(ctx);
        return NULL;
    }
    
    ctx->semaphores = malloc(sizeof(semaphores_t));
    if (!ctx->semaphores) {
        perror("malloc failed for semaphores");
        destroy_shared_memory(ctx->shared_data);
        free(ctx->worker_pids);
        free(ctx);
        return NULL;
    }
    
    if (init_semaphores(ctx->semaphores, config->max_queue_size) != 0) {
        fprintf(stderr, "Failed to initialize semaphores\n");
        free(ctx->semaphores);
        destroy_shared_memory(ctx->shared_data);
        free(ctx->worker_pids);
        free(ctx);
        return NULL;
    }
    
    // Inicializa estatísticas
    stats_init(ctx->shared_data, ctx->semaphores);
    
    ctx->num_workers = config->num_workers;
    ctx->server_fd = -1;
    
    return ctx;
}

// Função principal do master process
void run_master(master_context_t* ctx) {
    ctx->server_fd = create_server_socket(ctx->config->port);
    if (ctx->server_fd < 0) {
        fprintf(stderr, "Failed to create server socket\n");
        return;
    }
    
    printf("Master PID %d listening on port %d\n", getpid(), ctx->config->port);
    printf("Creating %d worker processes...\n", ctx->config->num_workers);
    
    // Cria processos workers
    for (int i = 0; i < ctx->config->num_workers; i++) {
        pid_t pid = fork();
        
        if (pid == 0) {
            // Processo filho (worker)
            worker_context_t* worker_ctx = malloc(sizeof(worker_context_t));
            if (!worker_ctx) {
                perror("malloc failed for worker context");
                exit(EXIT_FAILURE);
            }
            
            worker_ctx->worker_id = i;
            worker_ctx->shared_data = ctx->shared_data;
            worker_ctx->semaphores = ctx->semaphores;
            worker_ctx->config = ctx->config;
            
            worker_main(worker_ctx);
            exit(EXIT_FAILURE);
            
        } else if (pid > 0) {
            // Processo pai (master)
            ctx->worker_pids[i] = pid;
            printf("Worker %d started with PID %d\n", i, pid);
        } else {
            perror("fork failed");
            exit(EXIT_FAILURE);
        }
    }
    
    // Configura handlers de sinal
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // Inicia thread para estatísticas periódicas
    pthread_t stats_thread;
    if (pthread_create(&stats_thread, NULL, stats_thread_wrapper, ctx) != 0) {
        perror("Failed to create stats thread");
    } else {
        printf("Statistics monitor started (updates every 30 seconds)\n");
    }
    
    printf("Server ready! Press Ctrl+C to shutdown.\n\n");
    
    // Loop principal do master
    while (keep_running) {
        int client_fd = accept(ctx->server_fd, NULL, NULL);
        if (client_fd < 0) {
            if (keep_running) {
                perror("accept failed");
            }
            continue;
        }
        
        // Regista conexão aceite
        stats_connection_opened(ctx->shared_data, ctx->semaphores);
        
        int result = enqueue_connection(ctx->shared_data,
                                      ctx->semaphores->empty_slots,
                                      ctx->semaphores->filled_slots,
                                      ctx->semaphores->queue_mutex,
                                      client_fd);
        
        if (result != 0) {
            // Fila cheia - rejeita conexão
            const char* response = 
                "HTTP/1.1 503 Service Unavailable\r\n"
                "Content-Type: text/plain\r\n"
                "Content-Length: 19\r\n"
                "Connection: close\r\n"
                "\r\n"
                "Service Unavailable";
            
            send(client_fd, response, strlen(response), 0);
            close(client_fd);
            
            // Regista conexão rejeitada
            stats_connection_rejected(ctx->shared_data, ctx->semaphores);
            stats_connection_closed(ctx->shared_data, ctx->semaphores);
            
            printf("Queue full - sent 503 response\n");
        } else {
            printf("Enqueued connection (FD: %d)\n", client_fd);
        }
    }
    
    // Shutdown gracioso
    cleanup_master(ctx, stats_thread);
}

// Limpa recursos do master process
void cleanup_master(master_context_t* ctx, pthread_t stats_thread) {
    printf("\nShutting down server...\n");
    
    // Para thread de estatísticas
    pthread_cancel(stats_thread);
    pthread_join(stats_thread, NULL);
    printf("Statistics monitor stopped\n");
    
    // Mostra estatísticas finais
    printf("\n=== FINAL STATISTICS ===\n");
    stats_display(ctx->shared_data, ctx->semaphores);
    
    // Fecha socket do servidor
    if (ctx->server_fd >= 0) {
        close(ctx->server_fd);
        printf("Server socket closed\n");
    }
    
    // Termina processos workers
    printf("Terminating worker processes...\n");
    for (int i = 0; i < ctx->num_workers; i++) {
        if (ctx->worker_pids[i] > 0) {
            kill(ctx->worker_pids[i], SIGTERM);
            printf("Sent SIGTERM to worker PID %d\n", ctx->worker_pids[i]);
        }
    }
    
    // Espera que workers terminem
    for (int i = 0; i < ctx->num_workers; i++) {
        if (ctx->worker_pids[i] > 0) {
            waitpid(ctx->worker_pids[i], NULL, 0);
            printf("Worker PID %d terminated\n", ctx->worker_pids[i]);
        }
    }
    
    // Limpa recursos
    destroy_semaphores(ctx->semaphores);
    destroy_shared_memory(ctx->shared_data);
    
    free(ctx->semaphores);
    free(ctx->worker_pids);
    free(ctx);
    
    printf("Server shutdown complete!\n");
}