// semaphores.h
// Inês Batista, Maria Quinteiro

// Define semáforos POSIX nomeados para coordenar acesso a recursos partilhados.
// Inclui semáforos para slots vazios/cheios na fila, mutex para estatísticas
// e logging. Garante exclusão mútua e sincronização correta no modelo produtor-consumidor.

#ifndef SEMAPHORES_H
#define SEMAPHORES_H

#include <semaphore.h>  // Para sem_t e funções de semáforo

// Estrutura que agrupa todos os semáforos do sistema - conforme template
typedef struct {
    sem_t* empty_slots;    // Conta slots vazios na fila (inicia com queue_size)
    sem_t* filled_slots;   // Conta slots preenchidos na fila (inicia com 0)
    sem_t* queue_mutex;    // Mutex para acesso exclusivo à fila (inicia com 1)
    sem_t* stats_mutex;    // Mutex para atualizar estatísticas (inicia com 1)
    sem_t* log_mutex;      // Mutex para escrever logs (inicia com 1)
} semaphores_t;

// Funções para inicializar e destruir semáforos - conforme template
int init_semaphores(semaphores_t* sems, int queue_size);
void destroy_semaphores(semaphores_t* sems);

#endif