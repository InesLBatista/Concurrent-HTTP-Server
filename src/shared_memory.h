// Inês Batista, 124877
// Maria Quinteiro, 124996


#ifndef SHARED_MEMORY_H
#define SHARED_MEMORY_H

#include <semaphore.h>
#include <unistd.h> // Necessário para size_t e tipos POSIX.

#define MAX_QUEUE_SIZE 100 // Tamanho máximo da fila de conexões.
#define SHM_NAME "/server_shm_queue" // Nome do segmento de memória partilhada (usado no .c).



// Estrutura para a fila circular de descritores de sockets.
typedef struct {
    int sockets[MAX_QUEUE_SIZE]; // Array para guardar os FDs.
    int head;                    // Índice da próxima conexão a consumir (dequeue).
    int rear;                    // Índice para a próxima conexão a produzir (enqueue).
    int count;                   // Número atual de elementos na fila.
} connection_queue_t;

// Estrutura principal da memória partilhada.
typedef struct {
    sem_t mutex;            // Mutex para acesso exclusivo à fila e estatísticas.
    sem_t empty_slots;      // Conta slots vazios (usado pelo Produtor - Master).
    sem_t full_slots;       // Conta slots preenchidos (usado pelo Consumidor - Worker).
    connection_queue_t queue; // A fila de conexões.
    server_stats_t stats;     // Estatísticas globais.
} shared_data_t;

// Variável global que aponta para o segmento mapeado de memória partilhada.
extern shared_data_t *g_shared_data;

// Funções de inicialização e limpeza.
shared_data_t *create_shared_memory();
void destroy_shared_memory(shared_data_t *data);


// Adiciona um descritor de ficheiro à fila de forma segura (Produtor: Master).
int enqueue_connection(int client_fd); 

// Retira um descritor de ficheiro da fila de forma segura (Consumidor: Worker).
int dequeue_connection(void); 

#endif 