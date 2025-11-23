// stats.h
// Inês Batista, Maria Quinteiro

// Define sistema de estatísticas partilhadas entre processos.
// Permite tracking de métricas de performance em tempo real com
// atualizações atómicas usando semáforos POSIX.

#ifndef STATS_H
#define STATS_H

#include "shared_memory.h"
#include "semaphores.h"

// Funções para atualizar estatísticas
void stats_init(shared_data_t* shared_data, semaphores_t* semaphores);
void stats_increment_request(shared_data_t* shared_data, semaphores_t* semaphores);
void stats_increment_status(shared_data_t* shared_data, semaphores_t* semaphores, int status_code);
void stats_add_bytes(shared_data_t* shared_data, semaphores_t* semaphores, long bytes);

// Funções de leitura e display
void stats_display(shared_data_t* shared_data, semaphores_t* semaphores);
void stats_print_periodic(shared_data_t* shared_data, semaphores_t* semaphores, int interval_seconds);

#endif