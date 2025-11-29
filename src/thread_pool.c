// thread_pool.c
// Inês Batista, Maria Quinteiro

// Implementa o sistema de thread pool com queue interna de tarefas.
// Usa condition variables para threads bloquearem quando não há trabalho
// e acordarem quando novas conexões chegam.

#include "thread_pool.h"
#include "http.h"    // Para process_http_request
#include "logger.h"  // Para log_request
#include "stats.h"   // Para funções de estatísticas
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

// Função para processar pedidos HTTP (declaração se não estiver em http.h)
void process_http_request(int client_fd, server_config_t* config, 
                         semaphores_t* semaphores, shared_data_t* shared_data);

// Função executada por cada thread na pool
// Arg: ponteiro para o thread pool
static void* worker_thread(void* arg) {
    thread_pool_t* pool = (thread_pool_t*)arg;
    thread_pool_context_t* ctx = pool->context;
    
    printf("Thread %lu started in worker %d\n", 
           (unsigned long)pthread_self(), ctx->worker_id);
    
    // Loop principal da thread
    while (1) {
        pthread_mutex_lock(&pool->queue.mutex);
        
        // Espera por trabalho ou shutdown
        while (pool->queue.count == 0 && !pool->queue.shutdown) {
            pthread_cond_wait(&pool->queue.cond, &pool->queue.mutex);
        }
        
        // Verifica se deve terminar
        if (pool->queue.shutdown) {
            pthread_mutex_unlock(&pool->queue.mutex);
            break;
        }
        
        // Obtém tarefa da queue
        int client_fd = task_queue_pop(&pool->queue);
        pthread_mutex_unlock(&pool->queue.mutex);
        
        // Processa a conexão
        if (client_fd >= 0) {
            printf("Thread %lu processing connection (FD: %d)\n", 
                   (unsigned long)pthread_self(), client_fd);
            
            // Chama a função de processamento HTTP
            process_http_request(client_fd, ctx->config, 
                               ctx->semaphores, ctx->shared_data);
            
            printf("Thread %lu finished processing (FD: %d)\n", 
                   (unsigned long)pthread_self(), client_fd);
        }
    }
    
    printf("Thread %lu shutting down\n", (unsigned long)pthread_self());
    return NULL;
}

// Inicializa uma queue de tarefas
// Queue: queue a inicializar
void task_queue_init(task_queue_t* queue) {
    queue->front = NULL;
    queue->rear = NULL;
    queue->count = 0;
    queue->shutdown = 0;
    pthread_mutex_init(&queue->mutex, NULL);
    pthread_cond_init(&queue->cond, NULL);
}

// Destrói uma queue de tarefas e liberta recursos
// Queue: queue a destruir
void task_queue_destroy(task_queue_t* queue) {
    // Remove todas as tarefas pendentes
    pthread_mutex_lock(&queue->mutex);
    task_t* current = queue->front;
    while (current != NULL) {
        task_t* next = current->next;
        close(current->client_fd);  // Fecha sockets pendentes
        free(current);
        current = next;
    }
    pthread_mutex_unlock(&queue->mutex);
    
    // Destrói mutex e condition variable
    pthread_mutex_destroy(&queue->mutex);
    pthread_cond_destroy(&queue->cond);
}

// Adiciona uma tarefa à queue
// Queue: queue onde adicionar
// Client_fd: descritor de socket do cliente
void task_queue_push(task_queue_t* queue, int client_fd) {
    // Aloca nova tarefa
    task_t* new_task = malloc(sizeof(task_t));
    if (!new_task) {
        perror("Failed to allocate task");
        close(client_fd);
        return;
    }
    
    new_task->client_fd = client_fd;
    new_task->next = NULL;
    
    pthread_mutex_lock(&queue->mutex);
    
    // Adiciona à queue
    if (queue->rear == NULL) {
        // Queue vazia
        queue->front = new_task;
        queue->rear = new_task;
    } else {
        // Adiciona ao final
        queue->rear->next = new_task;
        queue->rear = new_task;
    }
    
    queue->count++;
    
    // Sinaliza que há trabalho disponível
    pthread_cond_signal(&queue->cond);
    pthread_mutex_unlock(&queue->mutex);
}

// Remove e retorna uma tarefa da queue
// Queue: queue de onde remover
// Retorna: descritor de socket ou -1 se queue vazia
int task_queue_pop(task_queue_t* queue) {
    if (queue->front == NULL) {
        return -1;
    }
    
    task_t* task = queue->front;
    int client_fd = task->client_fd;
    
    // Atualiza front da queue
    queue->front = task->next;
    if (queue->front == NULL) {
        queue->rear = NULL;  // Queue ficou vazia
    }
    
    queue->count--;
    free(task);
    return client_fd;
}

// Cria e inicializa um thread pool
// Num_threads: número de threads na pool
// Worker_id: ID do worker
// Shared_data: memória partilhada
// Semaphores: semáforos para sincronização
// Config: configurações do servidor
// Retorna: ponteiro para thread pool ou NULL em erro
thread_pool_t* create_thread_pool(int num_threads, int worker_id, 
                                 shared_data_t* shared_data, 
                                 semaphores_t* semaphores, 
                                 server_config_t* config) {
    thread_pool_t* pool = malloc(sizeof(thread_pool_t));
    if (!pool) {
        perror("Failed to allocate thread pool");
        return NULL;
    }
    
    // Inicializa queue de tarefas
    task_queue_init(&pool->queue);
    
    // Aloca array de threads
    pool->threads = malloc(sizeof(pthread_t) * num_threads);
    if (!pool->threads) {
        perror("Failed to allocate threads array");
        free(pool);
        return NULL;
    }
    
    // Aloca e inicializa contexto
    pool->context = malloc(sizeof(thread_pool_context_t));
    if (!pool->context) {
        perror("Failed to allocate thread pool context");
        free(pool->threads);
        free(pool);
        return NULL;
    }
    
    pool->context->worker_id = worker_id;
    pool->context->shared_data = shared_data;
    pool->context->semaphores = semaphores;
    pool->context->config = config;
    
    pool->num_threads = num_threads;
    
    // Cria threads
    for (int i = 0; i < num_threads; i++) {
        if (pthread_create(&pool->threads[i], NULL, worker_thread, pool) != 0) {
            perror("Failed to create thread");
            // Destrói threads já criadas
            for (int j = 0; j < i; j++) {
                pthread_cancel(pool->threads[j]);
            }
            destroy_thread_pool(pool);
            return NULL;
        }
    }
    
    printf("Created thread pool with %d threads in worker %d\n", 
           num_threads, worker_id);
    return pool;
}

// Destrói um thread pool e liberta todos os recursos
// Pool: thread pool a destruir
void destroy_thread_pool(thread_pool_t* pool) {
    if (!pool) return;
    
    // Sinaliza shutdown para as threads
    pthread_mutex_lock(&pool->queue.mutex);
    pool->queue.shutdown = 1;
    pthread_cond_broadcast(&pool->queue.cond);  // Acorda todas as threads
    pthread_mutex_unlock(&pool->queue.mutex);
    
    // Espera que todas as threads terminem
    for (int i = 0; i < pool->num_threads; i++) {
        pthread_join(pool->threads[i], NULL);
    }
    
    // Liberta recursos
    free(pool->context);
    free(pool->threads);
    task_queue_destroy(&pool->queue);
    free(pool);
    
    printf("Thread pool destroyed\n");
}

// Adiciona uma tarefa ao thread pool
// Pool: thread pool onde adicionar a tarefa
// Client_fd: descritor de socket do cliente
void add_task_to_pool(thread_pool_t* pool, int client_fd) {
    task_queue_push(&pool->queue, client_fd);
}