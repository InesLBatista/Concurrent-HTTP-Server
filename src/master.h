// master.h
// Inês Batista, Maria Quinteiro

// Define o processo master que coordena todo o sistema.
// Responsável por aceitar conexões, gerir workers e sincronização global.

#ifndef MASTER_H
#define MASTER_H

// Includes necessários para as estruturas usadas
#include "config.h"
#include "shared_memory.h"
#include "semaphores.h"
#include "worker.h"  // ← ADICIONAR ESTE INCLUDE!

// Estrutura que guarda todo o contexto do master process
typedef struct {
    pid_t* worker_pids;           // Array com PIDs de todos os workers
    int num_workers;              // Número total de workers
    int server_fd;                // Descritor do socket do servidor
    shared_data_t* shared_data;   // Ponteiro para memória partilhada
    semaphores_t* semaphores;     // Semáforos para sincronização
    server_config_t* config;      // Configurações do servidor
} master_context_t;

// Função para criar socket do servidor
// port: porta TCP onde o servidor vai escutar
// Retorna: descritor de socket ou -1 em erro
int create_server_socket(int port);

// Função para inicializar contexto do master
// config: configurações carregadas do server.conf
// Retorna: ponteiro para contexto ou NULL em erro
master_context_t* init_master(server_config_t* config);

// Função principal que executa o loop do master
// ctx: contexto inicializado do master
void run_master(master_context_t* ctx);

// Função para limpar recursos do master
// ctx: contexto do master a limpar
void cleanup_master(master_context_t* ctx);

// Handler de sinais para shutdown gracioso
// sig: sinal recebido (SIGINT, SIGTERM)
void signal_handler(int sig);

#endif