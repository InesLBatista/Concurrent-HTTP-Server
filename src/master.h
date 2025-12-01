// Inês Batista, 124877
// Maria Quinteiro, 124996

#ifndef MASTER_H
#define MASTER_H

#include  "config.h" // Para server_cofig_t

#include "shared_memory.h"     // Para shared_data_t
#include "semaphores.h"     // Para semaphores_t

// Função para criar o socket do servidor
int create_server_socket(int port);

// Função para criar processos worker usando fork()
int create_worker_processes(server_config_t* config);

// Função para terminar todos os processos worker graciosamente
void terminate_worker_processes(void);

// Handler para sinais (SIGINT, SIGTERM)
void signal_handler(int signum);

// Função principal do processo master
int master_main(server_config_t* config);

#endif