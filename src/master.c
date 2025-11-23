// master.c
// Inês Batista, Maria Quinteiro

// Implementa o processo master que aceita conexões e coordena workers.
// Segue arquitetura producer-consumer: master é producer, workers são consumers.

#include "master.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/wait.h>
#include <string.h>

// Variável global para controlar execução do servidor
// volatile: impede otimizações do compilador que poderiam afetar acesso entre sinais
// sig_atomic_t: garante acesso atómico em handlers de sinal
volatile sig_atomic_t keep_running = 1;

// Handler para sinais SIGINT (Ctrl+C) e SIGTERM (kill)
// Permite shutdown gracioso quando o servidor recebe sinal de terminação
void signal_handler(int sig) {
    keep_running = 0;  // Sinaliza para parar o loop principal
    printf("\nReceived signal %d - Initiating graceful shutdown...\n", sig);
}

// Cria e configura socket do servidor
// port: porta TCP onde o servidor vai escutar conexões
// Retorna: descritor de socket ou -1 em caso de erro
int create_server_socket(int port) {
    // Cria socket TCP IPv4
    // AF_INET: IPv4, SOCK_STREAM: TCP, 0: protocolo padrão
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("socket creation failed");
        return -1;
    }
    
    // Permite reutilizar porta imediatamente após shutdown
    // Evita erro "Address already in use" ao reiniciar servidor rapidamente
    int opt = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt failed");
        close(sockfd);
        return -1;
    }
    
    // Configura endereço do servidor
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;           // Família IPv4
    addr.sin_addr.s_addr = INADDR_ANY;   // Aceita conexões de qualquer interface
    addr.sin_port = htons(port);         // Porta em network byte order
    
    // Associa socket ao endereço especificado
    if (bind(sockfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind failed");
        close(sockfd);
        return -1;
    }
    
    // Coloca socket em modo listening - pronto para aceitar conexões
    // 128: tamanho da fila de conexões pendentes
    if (listen(sockfd, 128) < 0) {
        perror("listen failed");
        close(sockfd);
        return -1;
    }
    
    return sockfd;  // Retorna socket configurado e pronto
}

// Inicializa contexto do master process
// config: configurações carregadas do ficheiro server.conf
// Retorna: ponteiro para contexto inicializado ou NULL em erro
master_context_t* init_master(server_config_t* config) {
    // Aloca memória para estrutura do master
    master_context_t* ctx = malloc(sizeof(master_context_t));
    if (!ctx) {
        perror("malloc failed for master context");
        return NULL;
    }
    
    // Guarda ponteiro para configurações
    ctx->config = config;
    
    // Aloca array para guardar PIDs dos workers
    ctx->worker_pids = malloc(sizeof(pid_t) * config->num_workers);
    if (!ctx->worker_pids) {
        perror("malloc failed for worker PIDs");
        free(ctx);
        return NULL;
    }
    
    // Inicializa memória partilhada para fila e estatísticas
    ctx->shared_data = create_shared_memory();
    if (!ctx->shared_data) {
        fprintf(stderr, "Failed to create shared memory\n");
        free(ctx->worker_pids);
        free(ctx);
        return NULL;
    }
    
    // Aloca e inicializa estrutura de semáforos
    ctx->semaphores = malloc(sizeof(semaphores_t));
    if (!ctx->semaphores) {
        perror("malloc failed for semaphores");
        destroy_shared_memory(ctx->shared_data);
        free(ctx->worker_pids);
        free(ctx);
        return NULL;
    }
    
    // Inicializa todos os semáforos POSIX
    if (init_semaphores(ctx->semaphores, config->max_queue_size) != 0) {
        fprintf(stderr, "Failed to initialize semaphores\n");
        free(ctx->semaphores);
        destroy_shared_memory(ctx->shared_data);
        free(ctx->worker_pids);
        free(ctx);
        return NULL;
    }
    
    // Inicializa restantes campos
    ctx->num_workers = config->num_workers;
    ctx->server_fd = -1;  // Será inicializado em run_master
    
    return ctx;  // Retorna contexto inicializado
}

// Função principal do master process
// ctx: contexto inicializado do master
// Esta função executa o loop principal até receber sinal de terminação
void run_master(master_context_t* ctx) {
    // Cria e configura socket do servidor
    ctx->server_fd = create_server_socket(ctx->config->port);
    if (ctx->server_fd < 0) {
        fprintf(stderr, "Failed to create server socket\n");
        return;
    }
    
    printf("Master PID %d listening on port %d\n", getpid(), ctx->config->port);
    printf("Creating %d worker processes...\n", ctx->config->num_workers);
    
    // Cria processos workers usando fork()
    for (int i = 0; i < ctx->config->num_workers; i++) {
        pid_t pid = fork();  // Cria novo processo
        
        if (pid == 0) {
            // PROCESSO FILHO (WORKER) - CODIGO CORRIGIDO
            // Aloca e inicializa contexto do worker
            worker_context_t* worker_ctx = malloc(sizeof(worker_context_t));
            if (!worker_ctx) {
                perror("malloc failed for worker context");
                exit(EXIT_FAILURE);
            }
            
            // Preenche contexto do worker com dados do master
            worker_ctx->worker_id = i;                      // ID único do worker
            worker_ctx->shared_data = ctx->shared_data;     // Memória partilhada
            worker_ctx->semaphores = ctx->semaphores;       // Semáforos
            worker_ctx->config = ctx->config;               // Configurações
            
            // Inicia função principal do worker
            // IMPORTANTE: worker_main NÃO RETORNA - termina com exit()
            worker_main(worker_ctx);
            
            // Esta linha nunca é alcançada por causa do exit() em worker_main
            exit(EXIT_FAILURE);
            
        } else if (pid > 0) {
            // Processo pai (master) - guarda PID do worker criado
            ctx->worker_pids[i] = pid;
            printf("Worker %d started with PID %d\n", i, pid);
        } else {
            // Erro no fork() - não foi possível criar processo
            perror("fork failed");
            exit(EXIT_FAILURE);
        }
    }
    
    // Configura handlers de sinal para shutdown gracioso
    signal(SIGINT, signal_handler);   // Ctrl+C
    signal(SIGTERM, signal_handler);  // Comando kill
    
    printf("Server ready! Press Ctrl+C to shutdown.\n\n");
    
    // Loop principal do master - aceita e enfileira conexões
    while (keep_running) {
        // Aceita nova conexão de cliente
        // NULL: não guarda informações do cliente
        int client_fd = accept(ctx->server_fd, NULL, NULL);
        if (client_fd < 0) {
            // Ignora erros de accept durante shutdown
            if (keep_running) {
                perror("accept failed");
            }
            continue;  // Continua loop mesmo com erro
        }
        
        // Tenta adicionar conexão à fila partilhada
        int result = enqueue_connection(ctx->shared_data,
                                      ctx->semaphores->empty_slots,
                                      ctx->semaphores->filled_slots,
                                      ctx->semaphores->queue_mutex,
                                      client_fd);
        
        if (result != 0) {
            // Fila cheia - não foi possível enfileirar conexão
            // Envia resposta HTTP 503 Service Unavailable
            const char* response = 
                "HTTP/1.1 503 Service Unavailable\r\n"
                "Content-Type: text/plain\r\n"
                "Content-Length: 19\r\n"
                "Connection: close\r\n"
                "\r\n"
                "Service Unavailable";
            
            send(client_fd, response, strlen(response), 0);
            close(client_fd);
            printf("Queue full - sent 503 response\n");
        } else {
            // Conexão enfileirada com sucesso - workers vão processá-la
            printf("Enqueued connection (FD: %d)\n", client_fd);
        }
    }
    
    // Shutdown gracioso - limpa todos os recursos
    cleanup_master(ctx);
}

// Limpa recursos do master process
// ctx: contexto do master a limpar
void cleanup_master(master_context_t* ctx) {
    printf("\nShutting down server...\n");
    
    // Fecha socket do servidor - para de aceitar novas conexões
    if (ctx->server_fd >= 0) {
        close(ctx->server_fd);
        printf("Server socket closed\n");
    }
    
    // Termina processos workers graciosamente
    printf("Terminating worker processes...\n");
    for (int i = 0; i < ctx->num_workers; i++) {
        if (ctx->worker_pids[i] > 0) {
            kill(ctx->worker_pids[i], SIGTERM);  // Envia sinal de terminação
            printf("Sent SIGTERM to worker PID %d\n", ctx->worker_pids[i]);
        }
    }
    
    // Espera que todos os workers terminem
    for (int i = 0; i < ctx->num_workers; i++) {
        if (ctx->worker_pids[i] > 0) {
            waitpid(ctx->worker_pids[i], NULL, 0);  // Espera término do processo
            printf("Worker PID %d terminated\n", ctx->worker_pids[i]);
        }
    }
    
    // Limpa recursos de sincronização e memória partilhada
    destroy_semaphores(ctx->semaphores);
    destroy_shared_memory(ctx->shared_data);
    
    // Liberta memória alocada
    free(ctx->semaphores);
    free(ctx->worker_pids);
    free(ctx);
    
    printf("Server shutdown complete!\n");
}