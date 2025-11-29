// stats.c
// Inês Batista, Maria Quinteiro

// Implementa sistema completo de estatísticas com métricas de performance.
// Todas as operações são atómicas usando semáforos POSIX para evitar
// race conditions no acesso aos contadores partilhados.

#include "stats.h"
#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <math.h>

// Inicializa estrutura de estatísticas
// Shared_data: ponteiro para memória partilhada com estatísticas
// Semaphores: semáforos para sincronização
void stats_init(shared_data_t* shared_data, semaphores_t* semaphores) {
    sem_wait(semaphores->stats_mutex);
    
    // Inicializa todos os contadores a zero
    shared_data->stats.total_requests = 0;
    shared_data->stats.bytes_transferred = 0;
    shared_data->stats.status_200 = 0;
    shared_data->stats.status_404 = 0;
    shared_data->stats.status_403 = 0;
    shared_data->stats.status_500 = 0;
    shared_data->stats.status_503 = 0;
    shared_data->stats.active_connections = 0;
    shared_data->stats.total_connections = 0;
    shared_data->stats.rejected_connections = 0;
    shared_data->stats.total_processing_time = 0;
    shared_data->stats.cache_hits = 0;
    shared_data->stats.cache_misses = 0;
    
    // Inicializa timestamp de início
    clock_gettime(CLOCK_MONOTONIC, &shared_data->stats.start_time);
    
    sem_post(semaphores->stats_mutex);
    
    printf("Statistics system initialized\n");
}

// Incrementa contador total de pedidos
// Shared_data: ponteiro para memória partilhada
// Semaphores: semáforos para sincronização
void stats_increment_request(shared_data_t* shared_data, semaphores_t* semaphores) {
    sem_wait(semaphores->stats_mutex);
    shared_data->stats.total_requests++;
    sem_post(semaphores->stats_mutex);
}

// Incrementa contador de status code específico
// Shared_data: ponteiro para memória partilhada
// Semaphores: semáforos para sincronização
// Status_code: código HTTP a incrementar
void stats_increment_status(shared_data_t* shared_data, semaphores_t* semaphores, int status_code) {
    sem_wait(semaphores->stats_mutex);
    
    switch (status_code) {
        case 200:
            shared_data->stats.status_200++;
            break;
        case 403:
            shared_data->stats.status_403++;
            break;
        case 404:
            shared_data->stats.status_404++;
            break;
        case 500:
            shared_data->stats.status_500++;
            break;
        case 503:
            shared_data->stats.status_503++;
            break;
        default:
            // Para outros status codes, podemos adicionar mais casos se necessário
            break;
    }
    
    sem_post(semaphores->stats_mutex);
}

// Adiciona bytes transferidos ao total
// Shared_data: ponteiro para memória partilhada
// Semaphores: semáforos para sincronização
// Bytes: número de bytes a adicionar
void stats_add_bytes(shared_data_t* shared_data, semaphores_t* semaphores, long bytes) {
    sem_wait(semaphores->stats_mutex);
    shared_data->stats.bytes_transferred += bytes;
    sem_post(semaphores->stats_mutex);
}

// Adiciona tempo de processamento para cálculo de average response time
// Shared_data: ponteiro para memória partilhada
// Semaphores: semáforos para sincronização
// Microseconds: tempo de processamento em microssegundos
void stats_add_processing_time(shared_data_t* shared_data, semaphores_t* semaphores, long microseconds) {
    sem_wait(semaphores->stats_mutex);
    shared_data->stats.total_processing_time += microseconds;
    sem_post(semaphores->stats_mutex);
}

// Regista abertura de uma nova conexão
// Shared_data: ponteiro para memória partilhada
// Semaphores: semáforos para sincronização
void stats_connection_opened(shared_data_t* shared_data, semaphores_t* semaphores) {
    sem_wait(semaphores->stats_mutex);
    shared_data->stats.active_connections++;
    shared_data->stats.total_connections++;
    sem_post(semaphores->stats_mutex);
}

// Regista fecho de uma conexão
// Shared_data: ponteiro para memória partilhada
// Semaphores: semáforos para sincronização
void stats_connection_closed(shared_data_t* shared_data, semaphores_t* semaphores) {
    sem_wait(semaphores->stats_mutex);
    if (shared_data->stats.active_connections > 0) {
        shared_data->stats.active_connections--;
    }
    sem_post(semaphores->stats_mutex);
}

// Regista conexão rejeitada (queue cheia)
// Shared_data: ponteiro para memória partilhada
// Semaphores: semáforos para sincronização
void stats_connection_rejected(shared_data_t* shared_data, semaphores_t* semaphores) {
    sem_wait(semaphores->stats_mutex);
    shared_data->stats.rejected_connections++;
    sem_post(semaphores->stats_mutex);
}

// Calcula uptime do servidor em segundos
// Shared_data: ponteiro para memória partilhada
// Semaphores: semáforos para sincronização
// Retorna: uptime em segundos
double stats_get_uptime(shared_data_t* shared_data, semaphores_t* semaphores) {
    struct timespec current_time;
    clock_gettime(CLOCK_MONOTONIC, &current_time);
    
    sem_wait(semaphores->stats_mutex);
    double uptime = (current_time.tv_sec - shared_data->stats.start_time.tv_sec) +
                   (current_time.tv_nsec - shared_data->stats.start_time.tv_nsec) / 1e9;
    sem_post(semaphores->stats_mutex);
    
    return uptime;
}

// Calcula average response time em milissegundos
// Shared_data: ponteiro para memória partilhada
// Semaphores: semáforos para sincronização
// Retorna: tempo médio de resposta em ms, ou 0 se não houver pedidos
double stats_get_avg_response_time(shared_data_t* shared_data, semaphores_t* semaphores) {
    sem_wait(semaphores->stats_mutex);
    
    double avg_time = 0.0;
    if (shared_data->stats.total_requests > 0) {
        avg_time = (double)shared_data->stats.total_processing_time / 
                   shared_data->stats.total_requests / 1000.0; // Convert to milliseconds
    }
    
    sem_post(semaphores->stats_mutex);
    return avg_time;
}

// Calcula requests por segundo
// Shared_data: ponteiro para memória partilhada
// Semaphores: semáforos para sincronização
// Retorna: requests por segundo
double stats_get_requests_per_second(shared_data_t* shared_data, semaphores_t* semaphores) {
    double uptime = stats_get_uptime(shared_data, semaphores);
    
    sem_wait(semaphores->stats_mutex);
    double rps = (uptime > 0) ? (double)shared_data->stats.total_requests / uptime : 0.0;
    sem_post(semaphores->stats_mutex);
    
    return rps;
}

// Mostra estatísticas detalhadas no console
// Shared_data: ponteiro para memória partilhada
// Semaphores: semáforos para sincronização
void stats_display(shared_data_t* shared_data, semaphores_t* semaphores) {
    sem_wait(semaphores->stats_mutex);
    
    double uptime = stats_get_uptime(shared_data, semaphores);
    double avg_response_time = stats_get_avg_response_time(shared_data, semaphores);
    double rps = stats_get_requests_per_second(shared_data, semaphores);
    
    printf("\n"
           "╔══════════════════════════════════════════════════════════════╗\n"
           "║                   CONCURRENT HTTP SERVER STATS               ║\n"
           "╠══════════════════════════════════════════════════════════════╣\n");
    
    // Uptime and basic metrics
    printf("║ Uptime: %8.2f seconds", uptime);
    printf(" │ Active Connections: %3d", shared_data->stats.active_connections);
    printf(" ║\n");
    
    // Request statistics
    printf("║ Total Requests: %10ld", shared_data->stats.total_requests);
    printf(" │ Req/Sec: %8.2f", rps);
    printf("          ║\n");
    
    // Throughput
    printf("║ Bytes Transferred: %8.2f MB", 
           shared_data->stats.bytes_transferred / (1024.0 * 1024.0));
    printf(" │ Avg Response: %6.2f ms", avg_response_time);
    printf("      ║\n");
    
    // Status code distribution
    printf("║ Status 200: %12ld", shared_data->stats.status_200);
    printf(" │ Status 404: %8ld", shared_data->stats.status_404);
    printf("           ║\n");
    
    printf("║ Status 403: %12ld", shared_data->stats.status_403);
    printf(" │ Status 500: %8ld", shared_data->stats.status_500);
    printf("           ║\n");
    
    // Connection statistics
    printf("║ Total Connections: %7ld", shared_data->stats.total_connections);
    printf(" │ Rejected: %10ld", shared_data->stats.rejected_connections);
    printf("       ║\n");
    
    // Success rate
    if (shared_data->stats.total_requests > 0) {
        double success_rate = (double)shared_data->stats.status_200 / 
                             shared_data->stats.total_requests * 100.0;
        printf("║ Success Rate: %11.2f%%", success_rate);
        
        // Cache statistics (se disponíveis)
        if (shared_data->stats.cache_hits + shared_data->stats.cache_misses > 0) {
            double cache_hit_rate = (double)shared_data->stats.cache_hits / 
                                   (shared_data->stats.cache_hits + shared_data->stats.cache_misses) * 100.0;
            printf(" │ Cache Hit: %7.2f%%", cache_hit_rate);
        } else {
            printf(" │ Cache: %12s", "N/A");
        }
        printf(" ║\n");
    }
    
    printf("╚══════════════════════════════════════════════════════════════╝\n");
    
    sem_post(semaphores->stats_mutex);
}

// Mostra estatísticas periodicamente (para usar no master process)
// Shared_data: ponteiro para memória partilhada
// Semaphores: semáforos para sincronização
// Interval_seconds: intervalo entre displays em segundos
void stats_print_periodic(shared_data_t* shared_data, semaphores_t* semaphores, int interval_seconds) {
    while (1) {
        sleep(interval_seconds);
        stats_display(shared_data, semaphores);
    }
}