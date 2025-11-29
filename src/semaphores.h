// Inês Batista, 124877
// Maria Quinteiro, 124996

#ifndef SEMAPHORES_H
#define SEMAPHORES_H

#include <semaphore.h>  // Para sem_t, sem_open, etc. - semáforos POSIX

// Estrutura que guarda todos os semáforos que precisamos
// Semáforos são como controladores de trânsito para threads/processos
typedef struct {
    sem_t* empty_slots;    // Conta quantos lugares VAZIOS há na fila
    sem_t* filled_slots;   // Conta quantos lugares OCUPADOS há na fila  
    sem_t* queue_mutex;    // Mutex para acesso EXCLUSIVO à fila (só um de cada vez)
    sem_t* stats_mutex;    // Mutex para acesso EXCLUSIVO às estatísticas
    sem_t* log_mutex;      // Mutex para acesso EXCLUSIVO ao ficheiro de log
} semaphores_t;

// Função para criar e inicializar todos os semáforos
// queue_size = tamanho máximo da fila (para empty_slots)
// Retorna 0 em sucesso, -1 em erro
int init_semaphores(semaphores_t* sems, int queue_size);

// Função para destruir todos os semáforos
// Deve ser chamada no final para limpeza
void destroy_semaphores(semaphores_t* sems);

#endif