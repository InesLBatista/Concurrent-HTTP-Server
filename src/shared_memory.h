// Inês Batista, 124877
// Maria Quinteiro, 124996

#ifndef SHARED_MEM_H
#define SHARED_MEM_H

#include <semaphore.h>  // Para sem_t - semáforos para sincronização

// Define o tamanho máximo da fila de conexões
// Isto é o número máximo de clientes que podem ficar em espera
#define MAX_QUEUE_SIZE 100

// Estrutura para guardar estatísticas do servidor
// Estas estatísticas são partilhadas entre todos os processos
typedef struct {
    long total_requests;     // Conta quantos pedidos HTTP foram processados no total
    long bytes_transferred;   // Conta quantos bytes foram enviados para clientes
    long status_200;       // Conta quantas respostas "200 OK" foram enviadas
    long status_404;             // Conta quantas respostas "404 Not Found" foram enviadas  
    long status_500;          // Conta quantas respostas "500 Internal Error" foram enviadas
    int active_connections;     // Mostra quantas ligações estão ativas AGORA mesmo
} server_stats_t;

// Estrutura para a fila de conexões (produtor-consumidor)
// O master coloca conexões aqui, os workers retiram
typedef struct {
    int sockets[MAX_QUEUE_SIZE];  // Array que guarda os descritores de socket
    int front;  // Índice do próximo elemento a ser retirado (consumidor)
    int rear;         // Índice do próximo elemento a ser inserido (produtor)
    int count;      // Número de elementos atualmente na fila
} connection_queue_t;

// Estrutura principal que vai para a memória partilhada
// Contém tanto a fila como as estatísticas
typedef struct {
    connection_queue_t queue;   // Fila de conexões para os workers processarem
    server_stats_t stats;    // Estatísticas globais do servidor
    
    // SEMÁFOROS PARA SINCRONIZAÇÃO - ISTO É O QUE FALTAVA!
    sem_t mutex;              // Semáforo para acesso exclusivo à estrutura toda
    sem_t empty_slots;        // Semáforo para slots vazios na fila (produtor espera)
    sem_t full_slots;         // Semáforo para slots preenchidos (consumidor espera)
} shared_data_t;


// Função para criar a memória partilhada
// Retorna um ponteiro para os dados partilhados ou NULL se erro
shared_data_t* create_shared_memory();


// Função para limpar a memória partilhada no final
// Deve ser chamada quando o servidor termina
void destroy_shared_memory(shared_data_t* data);

#endif