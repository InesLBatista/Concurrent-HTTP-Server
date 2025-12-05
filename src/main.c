#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include "config.h"
#include "master.h"
#include "worker.h"
#include "shared_mem.h"
#include "semaphores.h"

server_config_t config;
shared_data_t* shared_data = NULL;
semaphores_t semaphores;
pid_t* worker_pids = NULL;

void cleanup() {
    printf("\nPerforming cleanup...\n");
    
    // Kill worker processes
    if (worker_pids) {
        for (int i = 0; i < config.num_workers; i++) {
            if (worker_pids[i] > 0) {
                kill(worker_pids[i], SIGTERM);
            }
        }
        free(worker_pids);
    }
    
    // Cleanup semaphores
    destroy_semaphores(&semaphores);
    
    // Cleanup shared memory
    if (shared_data) {
        destroy_shared_memory(shared_data);
    }
    
    printf("Cleanup completed.\n");
}

void signal_handler(int signum) {
    printf("\nReceived signal %d, shutting down...\n", signum);
    cleanup();
    exit(0);
}

int main(int argc, char* argv[]) {
    // Set up signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // Load configuration
    if (load_config("server.conf", &config) != 0) {
        fprintf(stderr, "Failed to load configuration\n");
        return 1;
    }
    
    printf("=== Concurrent HTTP Server ===\n");
    printf("Port: %d\n", config.port);
    printf("Document Root: %s\n", config.document_root);
    printf("Workers: %d\n", config.num_workers);
    printf("Threads per Worker: %d\n", config.threads_per_worker);
    printf("==============================\n\n");
    
    // Create shared memory
    shared_data = create_shared_memory();
    if (!shared_data) {
        fprintf(stderr, "Failed to create shared memory\n");
        return 1;
    }
    
    // Initialize semaphores
    if (init_semaphores(&semaphores, config.max_queue_size) != 0) {
        fprintf(stderr, "Failed to initialize semaphores\n");
        destroy_shared_memory(shared_data);
        return 1;
    }
    
    // Initialize shared data
    shared_data->queue.front = 0;
    shared_data->queue.rear = 0;
    shared_data->queue.size = 0;  // Corrigido: 'size' em vez de 'count'
    
    // Inicializar estatísticas usando stats_init() se disponível
    // ou inicializar manualmente conforme sua estrutura
    stats_init(&shared_data->stats);  // Se stats_init() estiver disponível
    

    
    // Inicializar o mutex das estatísticas
    pthread_mutex_init(&shared_data->stats_mutex, NULL);
    
    // Create worker processes
    worker_pids = malloc(sizeof(pid_t) * config.num_workers);
    
    for (int i = 0; i < config.num_workers; i++) {
        pid_t pid = fork();
        
        if (pid == 0) {
            // Child process - worker
            worker_main(i, shared_data, &semaphores, &config);
            exit(0);
        } else if (pid > 0) {
            // Parent process
            worker_pids[i] = pid;
            printf("Started worker process %d (PID: %d)\n", i, pid);
        } else {
            fprintf(stderr, "Failed to fork worker process\n");
            cleanup();
            return 1;
        }
        
        sleep(1); // Stagger worker startup
    }
    
    // Run master process
    printf("\nMaster process starting...\n");
    master_main(shared_data, &semaphores, &config);
    
    // Cleanup (should not reach here unless error)
    cleanup();
    return 0;
}