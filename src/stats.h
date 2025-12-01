// Inês Batista, 124877
// Maria Quinteiro, 124996

#ifndef STATS_H
#define STATS_H

#include <stddef.h> // Para size_t
#include <time.h>   // Para time_t (timestamp de início)
#include <semaphore.h> // Para sem_t (o mutex deve estar na shared memory)

// Estrutura que armazena todas as estatísticas globais do servidor.
typedef struct {
    long total_requests;         // Número total de pedidos processados.
    long successful_responses;   // Respostas 2xx (e.g., 200 OK).
    long client_errors;          // Erros 4xx (e.g., 404, 403).
    long server_errors;          // Erros 5xx (e.g., 500).
    size_t total_bytes_sent;     // Bytes totais enviados para clientes.
    time_t server_start_time;    // Timestamp do início do Master Process.
} server_stats_t;

// O semáforo 'stats_mutex' deve ser inicializado na shared_data_t 
// (em shared_memory.h) para proteger o acesso a esta estrutura.

// Função para atualizar as estatísticas globais.
void update_stats(server_stats_t *stats, int status_code, size_t bytes_sent, sem_t *stats_mutex);

#endif 