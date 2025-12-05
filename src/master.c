#include "master.h"
#include "shared_mem.h"  /* ADICIONAR */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>

/* Definir constantes se não definidas no header */
#ifndef MAX_QUEUE_SIZE
#define MAX_QUEUE_SIZE 100
#endif

volatile sig_atomic_t master_running = 1;

void master_signal_handler(int signum) {
    master_running = 0;
    printf("Received signal %d, shutting down...\n", signum);
}

int create_server_socket(int port) {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("socket");
        return -1;
    }
    
    /* Set socket options */
    int opt = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt");
        close(sockfd);
        return -1;
    }
    
    /* Bind to port */
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    
    if (bind(sockfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(sockfd);
        return -1;
    }
    
    /* Listen for connections */
    if (listen(sockfd, 128) < 0) {
        perror("listen");
        close(sockfd);
        return -1;
    }
    
    printf("Server listening on port %d\n", port);
    return sockfd;
}

void enqueue_connection(shared_data_t* data, semaphores_t* sems, int client_fd) {
    /* Usar semáforos para sincronização */
    if (sem_wait(sems->empty_slots) == -1) {
        perror("sem_wait empty_slots");
        close(client_fd);
        return;
    }
    
    if (sem_wait(sems->queue_mutex) == -1) {
        sem_post(sems->empty_slots);
        perror("sem_wait queue_mutex");
        close(client_fd);
        return;
    }
    
    /* Usar função sincronizada de enfileiramento */
    if (shared_queue_enqueue(&data->queue, client_fd) == 0) {
        printf("Enqueued connection (fd: %d), queue size: %d\n", 
               client_fd, data->queue.size);
    } else {
        /* Fila cheia (não deve acontecer devido aos semáforos) */
        close(client_fd);
        printf("Queue full, rejected connection (fd: %d)\n", client_fd);
    }
    
    /* Atualizar estatísticas - nova conexão */
    shared_stats_update_connection(data, 1); /* 1 = nova conexão */
    
    sem_post(sems->queue_mutex);
    sem_post(sems->filled_slots);
}

void display_statistics(shared_data_t* data, semaphores_t* sems) {
    /* Exibir estatísticas usando a função do stats.c */
    printf("\n");
    shared_memory_print_stats(data);
    
    /* Mostrar também status da fila */
    printf("\nQueue Status: %d/%d connections waiting\n", 
           data->queue.size, data->queue.capacity);
    printf("=============================================\n\n");
}

void master_main(shared_data_t* data, semaphores_t* sems, server_config_t* config) {
    /* Versão simplificada com signal() */
    if (signal(SIGINT, master_signal_handler) == SIG_ERR) {
        perror("signal SIGINT");
    }
    
    if (signal(SIGTERM, master_signal_handler) == SIG_ERR) {
        perror("signal SIGTERM");
    }
    
    /* Create server socket */
    int server_fd = create_server_socket(config->port);
    if (server_fd < 0) {
        fprintf(stderr, "Failed to create server socket\n");
        return;
    }
    
    /* Set socket to non-blocking */
    int flags = fcntl(server_fd, F_GETFL, 0);
    if (flags == -1) {
        perror("fcntl F_GETFL");
        close(server_fd);
        return;
    }
    
    if (fcntl(server_fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        perror("fcntl F_SETFL");
        close(server_fd);
        return;
    }
    
    printf("Master process ready (PID: %d)\n", getpid());
    printf("Press Ctrl+C to stop the server\n\n");
    
    time_t last_stat_display = time(NULL);
    
    /* Main accept loop */
    while (master_running) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        
        int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
        
        if (client_fd >= 0) {
            char client_ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
            printf("Accepted connection from %s:%d\n", 
                   client_ip, ntohs(client_addr.sin_port));
            
            enqueue_connection(data, sems, client_fd);
        } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
            perror("accept");
            break;
        }
        
        /* Display statistics every 30 seconds */
        time_t now = time(NULL);
        if (difftime(now, last_stat_display) >= 30.0) {
            display_statistics(data, sems);
            last_stat_display = now;
        }
        
        usleep(10000); /* 10ms */
    }
    
    /* Cleanup */
    printf("\nMaster process shutting down...\n");
    
    /* Mostrar estatísticas finais */
    printf("\n=== FINAL STATISTICS ===\n");
    shared_memory_print_stats(data);
    
    shutdown(server_fd, SHUT_RDWR);
    close(server_fd);
    
    sleep(2);
    
    printf("Master process terminated\n");
}