// shared_memory.h
// Inês Batista, Maria Quinteiro

// Define estruturas de dados partilhadas entre processos: fila de conexões
// e estatísticas do servidor. Permite comunicação eficiente entre o processo
// master e workers através de memória partilhada POSIX.

#ifndef SHARED_MEMORY_H
#define SHARED_MEMORY_H

#include <semaphore.h>

// Tamanho máximo da fila de conexões - conforme especificação do projeto
#define MAX_QUEUE_SIZE 100

// Estrutura para estatísticas do servidor - conforme template fornecido
typedef struct {
    long total_requests;        // Contador total de pedidos servidos
    long bytes_transferred;     // Total de bytes transferidos
    long status_200;           // Contador de respostas 200 OK
    long status_404;           // Contador de respostas 404 Not Found
    long status_500;           // Contador de respostas 500 Internal Server Error
    int active_connections;    // Número de conexões ativas
} server_stats_t;

// Estrutura para fila de conexões - bounded circular buffer
typedef struct {
    int sockets[MAX_QUEUE_SIZE];  // Array de descritores de socket
    int front;                    // Índice para remover elementos (dequeue)
    int rear;                     // Índice para adicionar elementos (enqueue)  
    int count;                    // Número atual de elementos na fila
} connection_queue_t;

// Estrutura principal de dados partilhados
typedef struct {
    connection_queue_t queue;  // Fila de conexões para workers
    server_stats_t stats;      // Estatísticas do servidor
} shared_data_t;

// Funções para gerir memória partilhada - conforme template fornecido
shared_data_t* create_shared_memory();
void destroy_shared_memory(shared_data_t* data);

// Funções para operações na fila
int enqueue_connection(shared_data_t* data, sem_t* empty_slots, sem_t* filled_slots, sem_t* queue_mutex, int client_fd);
int dequeue_connection(shared_data_t* data, sem_t* empty_slots, sem_t* filled_slots, sem_t* queue_mutex);
int dequeue_connection_nonblock(shared_data_t* data, sem_t* empty_slots, sem_t* filled_slots, sem_t* queue_mutex);
int is_queue_empty(shared_data_t* data);
int is_queue_full(shared_data_t* data);

#endif