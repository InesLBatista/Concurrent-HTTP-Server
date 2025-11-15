// Estatisitcas do servidor

// Implementa um sistema de estatísticas com memória partilhada usando shm_open() e sincronização com semáforos POSIX.

#include "stats.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <semaphore.h>
#include <errno.h>
#include <time.h>

// ===== VARIÁVEIS GLOBAIS PRIVADAS =====

// Nomes para objetos de IPC
#define SHM_NAME "/concurrent_http_stats"
#define SEM_NAME "/concurrent_http_stats_sem"

// Ponteiro para estatísticas em memória partilhada
static server_stats_t *shared_stats = NULL;

// Semáforo para sincronização entre processos
static sem_t *stats_semaphore = NULL;

// Flag para indicar se somos o criador dos objetos IPC
static int is_creator = 0;

// ===== FUNÇÕES PRIVADAS =====

// Cria e inicializa objetos de memória partilhada
static int create_shared_memory(void) {
    int shm_fd;
    
    // Criar segmento de memória partilhada
    shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    if (shm_fd == -1) {
        perror("shm_open failed");
        return -1;
    }
    
    // Definir tamanho do segmento
    if (ftruncate(shm_fd, sizeof(server_stats_t)) == -1) {
        perror("ftruncate failed");
        close(shm_fd);
        return -1;
    }
    
    // Mapear memória partilhada
    shared_stats = mmap(NULL, sizeof(server_stats_t), 
                       PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (shared_stats == MAP_FAILED) {
        perror("mmap failed");
        close(shm_fd);
        return -1;
    }
    
    close(shm_fd);
    return 0;
}

// Liga-se a memória partilhada existente
static int attach_shared_memory(void) {
    int shm_fd;
    
    // Abrir segmento existente
    shm_fd = shm_open(SHM_NAME, O_RDWR, 0666);
    if (shm_fd == -1) {
        perror("shm_open failed");
        return -1;
    }
    
    // Mapear memória partilhada
    shared_stats = mmap(NULL, sizeof(server_stats_t), 
                       PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (shared_stats == MAP_FAILED) {
        perror("mmap failed");
        close(shm_fd);
        return -1;
    }
    
    close(shm_fd);
    return 0;
}

// Cria semáforo para sincronização
static int create_semaphore(void) {
    // Criar semáforo com valor inicial 1 (disponível)
    stats_semaphore = sem_open(SEM_NAME, O_CREAT | O_EXCL, 0666, 1);
    
    if (stats_semaphore == SEM_FAILED) {
        if (errno == EEXIST) {
            // Semáforo já existe, apenas abrir
            stats_semaphore = sem_open(SEM_NAME, O_RDWR);
        }
        
        if (stats_semaphore == SEM_FAILED) {
            perror("sem_open failed");
            return -1;
        }
    } else {
        is_creator = 1; // Somos os criadores do semáforo
    }
    
    return 0;
}


// Inicializa estrutura de estatísticas com valores zero
static void initialize_stats(void) {
    memset(shared_stats, 0, sizeof(server_stats_t));
    shared_stats->server_start_time = time(NULL);
    shared_stats->average_response_time = 0.0;
}



// IMPLEMENTAÇÃO DA API PÚBLICA
int stats_init(void) {
    int ret;
    
    // Tentar anexar à memória partilhada existente primeiro
    ret = attach_shared_memory();
    if (ret != 0) {
        // Se não existe, criar nova
        printf("Creating new shared memory segment...\n");
        ret = create_shared_memory();
        if (ret != 0) {
            return -1;
        }
        is_creator = 1;
    }
    
    // Inicializar semáforo
    if (create_semaphore() != 0) {
        return -1;
    }
    
    // Se somos os criadores, inicializar a estrutura
    if (is_creator) {
        sem_wait(stats_semaphore); // Lock
        initialize_stats();
        sem_post(stats_semaphore); // Unlock
        
        printf("Statistics system initialized (creator)\n");
    } else {
        printf("Statistics system attached to existing segment\n");
    }
    
    return 0;
}

void stats_cleanup(void) {
    if (shared_stats) {
        munmap(shared_stats, sizeof(server_stats_t));
        shared_stats = NULL;
    }
    
    if (stats_semaphore) {
        sem_close(stats_semaphore);
        
        // Se fomos os criadores, remover objetos IPC
        if (is_creator) {
            sem_unlink(SEM_NAME);
            shm_unlink(SHM_NAME);
            printf("Statistics system cleaned up (creator)\n");
        }
        
        stats_semaphore = NULL;
    }
}

void stats_increment_request(int status_code) {
    if (!shared_stats || !stats_semaphore) return;
    
    sem_wait(stats_semaphore); // Lock
    
    shared_stats->total_requests++;
    
    // Incrementar contador específico do status code
    switch (status_code) {
        case 200: shared_stats->status_200++; break;
        case 404: shared_stats->status_404++; break;
        case 403: shared_stats->status_403++; break;
        case 500: shared_stats->status_500++; break;
        case 503: shared_stats->status_503++; break;
        case 400: shared_stats->status_400++; break;
        case 501: shared_stats->status_501++; break;
        default: break; // Outros status codes não contabilizados separadamente
    }
    
    sem_post(stats_semaphore); // Unlock
}

void stats_add_bytes(size_t bytes) {
    if (!shared_stats || !stats_semaphore) return;
    
    sem_wait(stats_semaphore); // Lock
    shared_stats->total_bytes += bytes;
    sem_post(stats_semaphore); // Unlock
}

void stats_update_response_time(long response_time_ms) {
    if (!shared_stats || !stats_semaphore) return;
    
    sem_wait(stats_semaphore); // Lock
    
    // Atualizar soma acumulada
    shared_stats->total_response_time_ms += response_time_ms;
    
    // Calcular nova média
    if (shared_stats->total_requests > 0) {
        shared_stats->average_response_time = 
            (double)shared_stats->total_response_time_ms / shared_stats->total_requests;
    }
    
    sem_post(stats_semaphore); // Unlock
}

void stats_set_active_connections(unsigned long count) {
    if (!shared_stats || !stats_semaphore) return;
    
    sem_wait(stats_semaphore); // Lock
    shared_stats->active_connections = count;
    
    // Atualizar máximo simultâneo
    if (count > shared_stats->max_concurrent) {
        shared_stats->max_concurrent = count;
    }
    
    sem_post(stats_semaphore); // Unlock
}

const server_stats_t* stats_get(void) {
    // Retorna ponteiro para leitura (caller deve fazer sua própria sincronização se necessário)
    return shared_stats;
}

void stats_print(void) {
    if (!shared_stats) {
        printf("Statistics system not initialized\n");
        return;
    }
    
    // Fazer cópia local para evitar inconsistências durante leitura
    server_stats_t local_stats;
    
    sem_wait(stats_semaphore); // Lock
    memcpy(&local_stats, shared_stats, sizeof(server_stats_t));
    sem_post(stats_semaphore); // Unlock
    
    printf("=== SERVER STATISTICS ===\n");
    printf("Uptime: %ld seconds\n", stats_get_uptime());
    printf("Total Requests: %lu\n", local_stats.total_requests);
    printf("Requests/sec: %.2f\n", stats_get_requests_per_second());
    printf("Total Bytes: %lu (%.2f MB)\n", 
           local_stats.total_bytes, local_stats.total_bytes / (1024.0 * 1024.0));
    printf("Active Connections: %lu\n", local_stats.active_connections);
    printf("Max Concurrent: %lu\n", local_stats.max_concurrent);
    printf("Avg Response Time: %.2f ms\n", local_stats.average_response_time);
    printf("\nStatus Codes:\n");
    printf("  200 OK: %lu\n", local_stats.status_200);
    printf("  404 Not Found: %lu\n", local_stats.status_404);
    printf("  403 Forbidden: %lu\n", local_stats.status_403);
    printf("  500 Internal Error: %lu\n", local_stats.status_500);
    printf("  503 Service Unavailable: %lu\n", local_stats.status_503);
    printf("  400 Bad Request: %lu\n", local_stats.status_400);
    printf("  501 Not Implemented: %lu\n", local_stats.status_501);
    printf("\nErrors:\n");
    printf("  Connection Errors: %lu\n", local_stats.connection_errors);
    printf("  Timeout Errors: %lu\n", local_stats.timeout_errors);
}

void stats_display(void) {
    if (!shared_stats) return;
    
    // Versão mais compacta para output periódico
    server_stats_t local_stats;
    
    sem_wait(stats_semaphore); // Lock
    memcpy(&local_stats, shared_stats, sizeof(server_stats_t));
    sem_post(stats_semaphore); // Unlock
    
    long uptime = stats_get_uptime();
    double rps = stats_get_requests_per_second();
    
    printf("\n"
           "┌─────────────────────────────────────────────────────────┐\n"
           "│                 CONCURRENT HTTP SERVER STATS            │\n"
           "├─────────────────────────────────────────────────────────┤\n"
           "│ Uptime: %8ld s │ Reqs/sec: %8.2f │ Active: %4lu │\n"
           "│ Total: %10lu │ Bytes: %8.2f MB │ MaxConc: %4lu │\n"
           "├─────────────────────────────────────────────────────────┤\n"
           "│ 200: %6lu │ 404: %6lu │ 403: %6lu │ 500: %6lu │\n"
           "│ Avg Time: %8.2f ms │ Errors: %6lu │ Timeouts: %6lu │\n"
           "└─────────────────────────────────────────────────────────┘\n",
           uptime, rps, local_stats.active_connections,
           local_stats.total_requests, 
           local_stats.total_bytes / (1024.0 * 1024.0),
           local_stats.max_concurrent,
           local_stats.status_200, local_stats.status_404,
           local_stats.status_403, local_stats.status_500,
           local_stats.average_response_time,
           local_stats.connection_errors,
           local_stats.timeout_errors);
}

long stats_get_uptime(void) {
    if (!shared_stats) return 0;
    return time(NULL) - shared_stats->server_start_time;
}

double stats_get_requests_per_second(void) {
    if (!shared_stats) return 0.0;
    
    long uptime = stats_get_uptime();
    if (uptime == 0) return 0.0;
    
    return (double)shared_stats->total_requests / uptime;
}

void stats_increment_connection_error(void) {
    if (!shared_stats || !stats_semaphore) return;
    
    sem_wait(stats_semaphore); // Lock
    shared_stats->connection_errors++;
    sem_post(stats_semaphore); // Unlock
}

void stats_increment_timeout_error(void) {
    if (!shared_stats || !stats_semaphore) return;
    
    sem_wait(stats_semaphore); // Lock
    shared_stats->timeout_errors++;
    sem_post(stats_semaphore); // Unlock
}