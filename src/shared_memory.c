// shared_memory.c
// Inês Batista, Maria Quinteiro

// Implementa criação e destruição de segmentos de memória partilhada usando
// shm_open e mmap. Permite que múltiplos processos acedam à mesma memória
// para coordenar trabalho e partilhar estatísticas em tempo real.

#include "shared_memory.h"
#include <sys/mman.h>      // Para mmap, munmap
#include <fcntl.h>         // Para O_CREAT, O_RDWR
#include <unistd.h>        // Para ftruncate, close
#include <string.h>        // Para memset
#include <stdio.h>         // Para perror

// Nome único para o segmento de memória partilhada
#define SHM_NAME "/webserver_shm"

// Função para criar e inicializar memória partilhada
shared_data_t* create_shared_memory() {
    // Cria ou abre segmento de memória partilhada
    // O_CREAT - cria se não existir, O_RDWR - abre para leitura/escrita
    // 0666 - permissões: dono, grupo e outros podem ler e escrever
    int shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    if (shm_fd == -1) {
        perror("shm_open failed");
        return NULL;  // Retorna NULL em caso de erro
    }
    
    // Define o tamanho do segmento de memória partilhada
    // sizeof(shared_data_t) - tamanho da nossa estrutura de dados
    if (ftruncate(shm_fd, sizeof(shared_data_t)) == -1) {
        perror("ftruncate failed");
        close(shm_fd);  // Fecha o descritor antes de retornar
        return NULL;
    }
    
    // Mapeia a memória partilhada no espaço de endereçamento do processo
    // NULL - deixa o sistema escolher o endereço
    // PROT_READ | PROT_WRITE - permissões de leitura e escrita
    // MAP_SHARED - alterações são visíveis por outros processos
    shared_data_t* data = mmap(NULL, sizeof(shared_data_t),
                              PROT_READ | PROT_WRITE, 
                              MAP_SHARED, shm_fd, 0);
    
    // Fecha o descritor de ficheiro - o mapeamento mantém-se
    close(shm_fd);
    
    // Verifica se o mapeamento foi bem sucedido
    if (data == MAP_FAILED) {
        perror("mmap failed");
        return NULL;
    }
    
    // Inicializa a memória com zeros para garantir estado consistente
    memset(data, 0, sizeof(shared_data_t));
    
    // Retorna ponteiro para a memória partilhada
    return data;
}

// Função para destruir e libertar memória partilhada
void destroy_shared_memory(shared_data_t* data) {
    // Remove o mapeamento de memória do espaço de endereçamento do processo
    if (munmap(data, sizeof(shared_data_t)) == -1) {
        perror("munmap failed");
    }
    
    // Remove o segmento de memória partilhada do sistema
    if (shm_unlink(SHM_NAME) == -1) {
        perror("shm_unlink failed");
    }
}

// Função para adicionar conexão à fila (produtor)
// Implementa o protocolo producer-consumer do design.pdf
// Retorna 0 em sucesso, -1 se fila estiver cheia
int enqueue_connection(shared_data_t* data, sem_t* empty_slots, sem_t* filled_slots, sem_t* queue_mutex, int client_fd) {
    // Tenta decrementar empty_slots (não bloqueante)
    // Se sem_trywait falhar, significa que não há slots vazios
    if (sem_trywait(empty_slots) != 0) {
        return -1;  // Fila cheia - retorna erro
    }
    
    // Adquire mutex para acesso exclusivo à fila
    sem_wait(queue_mutex);
    
    // Adiciona conexão ao final da fila (circular buffer)
    data->queue.sockets[data->queue.rear] = client_fd;
    data->queue.rear = (data->queue.rear + 1) % MAX_QUEUE_SIZE;
    data->queue.count++;
    
    // Liberta mutex da fila
    sem_post(queue_mutex);
    
    // Incrementa filled_slots - sinaliza que há trabalho disponível
    sem_post(filled_slots);
    
    return 0;  // Sucesso
}

// Função para remover conexão da fila (consumidor)
// Implementa o protocolo producer-consumer do design.pdf
// Retorna descritor de socket ou -1 em erro
int dequeue_connection(shared_data_t* data, sem_t* empty_slots, sem_t* filled_slots, sem_t* queue_mutex) {
    // Aguarda por filled_slots - bloqueia se fila vazia
    sem_wait(filled_slots);
    
    // Adquire mutex para acesso exclusivo à fila
    sem_wait(queue_mutex);
    
    // Remove conexão do início da fila (circular buffer)
    int client_fd = data->queue.sockets[data->queue.front];
    data->queue.front = (data->queue.front + 1) % MAX_QUEUE_SIZE;
    data->queue.count--;
    
    // Liberta mutex da fila
    sem_post(queue_mutex);
    
    // Incrementa empty_slots - sinaliza que há espaço disponível
    sem_post(empty_slots);
    
    return client_fd;  // Retorna descritor para processamento
}

// Verifica se a fila está vazia
// Retorna 1 se vazia, 0 se não vazia
int is_queue_empty(shared_data_t* data) {
    return data->queue.count == 0;
}

// Verifica se a fila está cheia
// Retorna 1 se cheia, 0 se não cheia
int is_queue_full(shared_data_t* data) {
    return data->queue.count >= MAX_QUEUE_SIZE;
}