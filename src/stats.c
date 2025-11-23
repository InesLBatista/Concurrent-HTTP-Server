// stats.c
// Inês Batista, Maria Quinteiro

// Implementa sistema de estatísticas partilhadas entre processos.
// Todas as operações são atómicas usando semáforos POSIX para evitar
// race conditions no acesso aos contadores partilhados.

#include "stats.h"
#include <stdio.h>
#include <unistd.h>
#include <time.h>

// Inicializa estrutura de estatísticas
// Shared_data: ponteiro para memória partilhada com estatísticas
// Semaphores: semáforos para sincronização
void stats_init(shared_data_t* shared_data, semaphores_t* semaphores) {
    // Inicializa todos os contadores a zero
    // Já está feito no create_shared_memory(), mas explicitamos aqui
    sem_wait(semaphores->stats_mutex);
    
    shared_data->stats.total_requests = 0;
    shared_data->stats.bytes_transferred = 0;
    shared_data->stats.status_200 = 0;
    shared_data->stats.status_404 = 0;
    shared_data->stats.status_500 = 0;
    shared_data->stats.active_connections = 0;
    
    sem_post(semaphores->stats_mutex);
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
// Status_code: código HTTP a incrementar (200, 404, 500)
void stats_increment_status(shared_data_t* shared_data, semaphores_t* semaphores, int status_code) {
    sem_wait(semaphores->stats_mutex);
    
    switch (status_code) {
        case 200:
            shared_data->stats.status_200++;
            break;
        case 404:
            shared_data->stats.status_404++;
            break;
        case 500:
            shared_data->stats.status_500++;
            break;
        // Pode-se adicionar mais status codes conforme necessário
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

// Mostra estatísticas atuais no console
// Shared_data: ponteiro para memória partilhada
// Semaphores: semáforos para sincronização
void stats_display(shared_data_t* shared_data, semaphores_t* semaphores) {
    sem_wait(semaphores->stats_mutex);
    
    printf("\n=== SERVER STATISTICS ===\n");
    printf("Total Requests:    %ld\n", shared_data->stats.total_requests);
    printf("Bytes Transferred: %ld\n", shared_data->stats.bytes_transferred);
    printf("Status 200:        %ld\n", shared_data->stats.status_200);
    printf("Status 404:        %ld\n", shared_data->stats.status_404);
    printf("Status 500:        %ld\n", shared_data->stats.status_500);
    printf("Active Connections: %d\n", shared_data->stats.active_connections);
    
    // Calcula taxa de sucesso se houver pedidos
    if (shared_data->stats.total_requests > 0) {
        double success_rate = (double)shared_data->stats.status_200 / shared_data->stats.total_requests * 100;
        printf("Success Rate:      %.2f%%\n", success_rate);
    }
    
    printf("========================\n");
    
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