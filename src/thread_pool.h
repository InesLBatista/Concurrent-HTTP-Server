// thread_pool.h
// Inês Batista, Maria Quinteiro

// Define o sistema de thread pool para cada worker process.
// Cada worker cria uma pool de threads que processam conexões concorrentemente
// usando condition variables e mutexes para sincronização.

#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <pthread.h>
#include "config.h"
#include "shared_memory.h"
#include "semaphores.h"

// Estrutura forward declaration - evita dependência circular
typedef struct thread_pool_context thread_pool_context_t;

// Estrutura para representar uma tarefa na queue interna do worker
typedef struct task {
    int client_fd;              // Descritor de socket do cliente
    struct task* next;          // Ponteiro para próxima tarefa
} task_t;

// Estrutura para a queue interna de tarefas do worker
typedef struct {
    task_t* front;              // Primeira tarefa na queue
    task_t* rear;               // Última tarefa na queue
    int count;                  // Número de tarefas na queue
    int shutdown;               // Flag para shutdown gracioso
    pthread_mutex_t mutex;      // Mutex para acesso à queue
    pthread_cond_t cond;        // Condition variable para sinalizar trabalho
} task_queue_t;

// Estrutura principal do thread pool
typedef struct {
    pthread_t* threads;         // Array de threads
    int num_threads;            // Número de threads na pool
    task_queue_t queue;         // Queue interna de tarefas
    thread_pool_context_t* context;  // Contexto do thread pool
} thread_pool_t;

// Estrutura do contexto para o thread pool (substitui worker_context_t)
struct thread_pool_context {
    int worker_id;                  // ID único do worker
    shared_data_t* shared_data;     // Memória partilhada com master
    semaphores_t* semaphores;       // Semáforos para sincronização
    server_config_t* config;        // Configurações do servidor
};

// Funções do thread pool
thread_pool_t* create_thread_pool(int num_threads, int worker_id, 
                                 shared_data_t* shared_data, 
                                 semaphores_t* semaphores, 
                                 server_config_t* config);
void destroy_thread_pool(thread_pool_t* pool);
void add_task_to_pool(thread_pool_t* pool, int client_fd);

// Funções da queue interna de tarefas
void task_queue_init(task_queue_t* queue);
void task_queue_destroy(task_queue_t* queue);
void task_queue_push(task_queue_t* queue, int client_fd);
int task_queue_pop(task_queue_t* queue);

#endif