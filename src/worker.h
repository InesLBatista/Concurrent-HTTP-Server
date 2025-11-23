// worker.h
// Inês Batista, Maria Quinteiro

// Define o processo worker que processa conexões da fila partilhada.
// Cada worker é um processo independente que consome conexões da fila
// e as processa concorrentemente usando um thread pool (a implementar).

#ifndef WORKER_H
#define WORKER_H

// Includes necessários para as estruturas usadas
#include "config.h"
#include "shared_memory.h"
#include "semaphores.h"

// Estrutura que contém todo o contexto necessário para um worker operar
typedef struct {
    int worker_id;                  // ID único do worker (0, 1, 2, ...)
    shared_data_t* shared_data;     // Ponteiro para memória partilhada com master
    semaphores_t* semaphores;       // Semáforos para sincronização entre processos
    server_config_t* config;        // Configurações do servidor
} worker_context_t;

// Função principal do worker process
// ctx: contexto com toda a informação necessária para o worker operar
// Esta função não retorna - o worker termina com exit() quando recebe sinal
void worker_main(worker_context_t* ctx);

#endif