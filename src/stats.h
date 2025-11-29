// stats.h
// Inês Batista, Maria Quinteiro

// Define sistema completo de estatísticas partilhadas entre processos.
// Inclui tracking de performance, cálculo de métricas em tempo real
// e display periódico com sincronização atómica.

#ifndef STATS_H
#define STATS_H

#include "shared_memory.h"
#include "semaphores.h"
#include <time.h>

// Estrutura expandida para estatísticas detalhadas
typedef struct {
    // Contadores básicos
    long total_requests;        // Total de pedidos servidos
    long bytes_transferred;     // Total de bytes transferidos
    
    // Distribuição de status codes
    long status_200;           // Respostas 200 OK
    long status_404;           // Respostas 404 Not Found  
    long status_403;           // Respostas 403 Forbidden
    long status_500;           // Respostas 500 Internal Server Error
    long status_503;           // Respostas 503 Service Unavailable
    
    // Métricas de performance
    int active_connections;    // Conexões ativas atualmente
    long total_connections;    // Total de conexões aceites
    long rejected_connections; // Conexões rejeitadas (queue cheia)
    
    // Timing para cálculo de average response time
    struct timespec start_time; // Timestamp de início do servidor
    long total_processing_time; // Tempo total de processamento em microssegundos
    
    // Métricas de cache (se aplicável)
    long cache_hits;           // Cache hits
    long cache_misses;         // Cache misses
    
} server_stats_t;

// Estrutura principal de dados partilhados (atualizada)
typedef struct {
    connection_queue_t queue;  // Fila de conexões para workers
    server_stats_t stats;      // Estatísticas expandidas do servidor
} shared_data_t;

// Funções para atualizar estatísticas
void stats_init(shared_data_t* shared_data, semaphores_t* semaphores);
void stats_increment_request(shared_data_t* shared_data, semaphores_t* semaphores);
void stats_increment_status(shared_data_t* shared_data, semaphores_t* semaphores, int status_code);
void stats_add_bytes(shared_data_t* shared_data, semaphores_t* semaphores, long bytes);
void stats_add_processing_time(shared_data_t* shared_data, semaphores_t* semaphores, long microseconds);
void stats_connection_opened(shared_data_t* shared_data, semaphores_t* semaphores);
void stats_connection_closed(shared_data_t* shared_data, semaphores_t* semaphores);
void stats_connection_rejected(shared_data_t* shared_data, semaphores_t* semaphores);

// Funções de leitura e display
void stats_display(shared_data_t* shared_data, semaphores_t* semaphores);
void stats_print_periodic(shared_data_t* shared_data, semaphores_t* semaphores, int interval_seconds);
double stats_get_uptime(shared_data_t* shared_data, semaphores_t* semaphores);
double stats_get_avg_response_time(shared_data_t* shared_data, semaphores_t* semaphores);
double stats_get_requests_per_second(shared_data_t* shared_data, semaphores_t* semaphores);

#endif