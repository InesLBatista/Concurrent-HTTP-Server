// worker.c
// Inês Batista, Maria Quinteiro

// Implementa o processo worker que retira conexões da fila e as processa
// usando o motor HTTP completo. Inclui parsing de pedidos, serving de
// ficheiros e tracking de estatísticas.

#include "worker.h"
#include "http.h"
#include "logger.h"
#include "stats.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <sys/socket.h>

// Variável global para controlar a execução do worker
volatile sig_atomic_t worker_running = 1;

// Handler para sinais SIGTERM e SIGINT
void worker_signal_handler(int sig) {
    (void)sig;
    worker_running = 0;
}

// Processa um pedido HTTP completo
// Client_fd: descritor de socket do cliente
// Config: configurações do servidor
// Semaphores: semáforos para sincronização
// Shared_data: estatísticas partilhadas
void process_http_request(int client_fd, server_config_t* config, 
                         semaphores_t* semaphores, shared_data_t* shared_data) {
    char buffer[8192];
    http_request_t request;
    
    // Lê pedido do cliente
    ssize_t bytes_read = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
    if (bytes_read <= 0) {
        close(client_fd);
        return;
    }
    
    buffer[bytes_read] = '\0';
    
    // Parseia pedido HTTP
    if (parse_http_request(buffer, &request) != 0) {
        send_error_response(client_fd, 400);  // Bad Request
        stats_increment_status(shared_data, semaphores, 400);
        close(client_fd);
        return;
    }
    
    // Valida método HTTP
    if (!validate_http_method(request.method)) {
        send_error_response(client_fd, 501);  // Not Implemented
        stats_increment_status(shared_data, semaphores, 501);
        close(client_fd);
        return;
    }
    
    // Constrói caminho completo do ficheiro
    build_full_path(config->document_root, request.path, request.full_path);
    
    // Verifica se o ficheiro/diretório existe
    if (!file_exists(request.full_path)) {
        send_error_response(client_fd, 404);  // Not Found
        stats_increment_status(shared_data, semaphores, 404);
        
        // Log do pedido
        log_request(semaphores->log_mutex, "127.0.0.1", request.method, 
                   request.path, 404, 0);
        close(client_fd);
        return;
    }
    
    // Se for diretório, verifica se tem index.html
    if (is_directory(request.full_path)) {
        char index_path[1024];
        snprintf(index_path, sizeof(index_path), "%s/index.html", request.full_path);
        
        if (!file_exists(index_path)) {
            send_error_response(client_fd, 403);  // Forbidden (lista de diretório não permitida)
            stats_increment_status(shared_data, semaphores, 403);
            
            log_request(semaphores->log_mutex, "127.0.0.1", request.method, 
                       request.path, 403, 0);
            close(client_fd);
            return;
        }
        
        // Serve index.html
        strcpy(request.full_path, index_path);
    }
    
    // Serve o ficheiro
    send_file_response(client_fd, request.full_path);
    
    // Atualiza estatísticas
    stats_increment_request(shared_data, semaphores);
    stats_increment_status(shared_data, semaphores, 200);
    
    size_t file_size = get_file_size(request.full_path);
    stats_add_bytes(shared_data, semaphores, file_size);
    
    // Log do pedido bem sucedido
    log_request(semaphores->log_mutex, "127.0.0.1", request.method, 
               request.path, 200, file_size);
    
    close(client_fd);
}

// Função principal do worker process
void worker_main(worker_context_t* ctx) {
    signal(SIGTERM, worker_signal_handler);
    signal(SIGINT, worker_signal_handler);
    
    printf("Worker %d (PID %d) started and ready for work\n", 
           ctx->worker_id, getpid());
    
    // Loop principal do worker
    while (worker_running) {
        // Tenta retirar uma conexão da fila sem bloquear
        int client_fd = dequeue_connection_nonblock(ctx->shared_data,
                                                  ctx->semaphores->empty_slots,
                                                  ctx->semaphores->filled_slots,
                                                  ctx->semaphores->queue_mutex);
        
        if (client_fd >= 0 && worker_running) {
            printf("Worker %d processing connection (FD: %d)\n", 
                   ctx->worker_id, client_fd);
            
            // Processa pedido HTTP completo
            process_http_request(client_fd, ctx->config, ctx->semaphores, ctx->shared_data);
            
            printf("Worker %d finished processing (FD: %d)\n", 
                   ctx->worker_id, client_fd);
        } else if (worker_running) {
            // Fila vazia - pausa curta
            usleep(100000);  // 100ms
        }
    }
    
    printf("Worker %d (PID %d) shutting down\n", ctx->worker_id, getpid());
    free(ctx);
    exit(EXIT_SUCCESS);
}