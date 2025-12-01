// Inês Batista, 124877
// Maria Quinteiro, 124996

#include "stats.h"
#include <semaphore.h>
#include <stdio.h>


// Atualiza as estatísticas do servidor de forma segura (thread-safe/process-safe).
// Utiliza 'stats_mutex' para garantir o acesso exclusivo aos dados partilhados.
void update_stats(server_stats_t *stats, int status_code, size_t bytes_sent, sem_t *stats_mutex) {
    
    // 1. Aquisição do Semáforo (Lock): Bloqueia o acesso a outros Workers.
    if (sem_wait(stats_mutex) == -1) {
        perror("ERRO: sem_wait stats_mutex");
        return; 
    }

    //  SECÇÃO CRÍTICA: Modificação Segura das Estatísticas 
    
    stats->total_requests++;
    stats->total_bytes_sent += bytes_sent;

    // Atualiza contadores com base no código de status HTTP (2xx, 4xx, 5xx)
    if (status_code >= 200 && status_code < 300) {
        stats->successful_responses++;
    } else if (status_code >= 400 && status_code < 500) {
        stats->client_errors++;
    } else if (status_code >= 500 && status_code < 600) {
        stats->server_errors++;
    }
    
    // FIM da SECÇÃO CRÍTICA 

    // 2. Libertação do Semáforo (Unlock): Permite o acesso a outros Workers.
    if (sem_post(stats_mutex) == -1) {
        perror("ERRO: sem_post stats_mutex");
    }
}