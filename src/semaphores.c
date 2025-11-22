// semaphores.c
// Inês Batista, Maria Quinteiro

// Inicializa e gere semáforos nomeados para sincronização inter-processos.
// empty_slots controla espaço disponível na fila, filled_slots indica trabalho
// disponível, e mutexes protegem acesso concorrente a recursos críticos.

#include "semaphores.h"
#include <fcntl.h>     // Para constantes O_CREAT
#include <stdio.h>     // Para perror

// Função para inicializar todos os semáforos do sistema
// sems: ponteiro para estrutura onde guardar os semáforos
// queue_size: tamanho da fila para inicializar empty_slots
// Retorna 0 em sucesso, -1 em erro
int init_semaphores(semaphores_t* sems, int queue_size) {
    // Cria e inicializa semáforo para slots vazios
    // "/ws_empty" - nome único do semáforo no sistema
    // O_CREAT - cria se não existir, 0666 - permissões
    // queue_size - valor inicial (número de slots disponíveis)
    sems->empty_slots = sem_open("/ws_empty", O_CREAT, 0666, queue_size);
    
    // Cria semáforo para slots preenchidos (inicia com 0 - sem trabalho)
    sems->filled_slots = sem_open("/ws_filled", O_CREAT, 0666, 0);
    
    // Cria mutex para acesso à fila (inicia com 1 - desbloqueado)
    sems->queue_mutex = sem_open("/ws_queue_mutex", O_CREAT, 0666, 1);
    
    // Cria mutex para estatísticas
    sems->stats_mutex = sem_open("/ws_stats_mutex", O_CREAT, 0666, 1);
    
    // Cria mutex para logging
    sems->log_mutex = sem_open("/ws_log_mutex", O_CREAT, 0666, 1);
    
    // Verifica se todos os semáforos foram criados com sucesso
    // SEM_FAILED é retornado em caso de erro
    if (sems->empty_slots == SEM_FAILED || 
        sems->filled_slots == SEM_FAILED ||
        sems->queue_mutex == SEM_FAILED || 
        sems->stats_mutex == SEM_FAILED ||
        sems->log_mutex == SEM_FAILED) {
        
        perror("sem_open failed");
        return -1;  // Retorna erro se algum semáforo falhar
    }
    
    return 0;  // Sucesso
}

// Função para destruir e libertar todos os semáforos
void destroy_semaphores(semaphores_t* sems) {
    // Fecha cada semáforo - liberta recursos do processo atual
    sem_close(sems->empty_slots);
    sem_close(sems->filled_slots);
    sem_close(sems->queue_mutex);
    sem_close(sems->stats_mutex);
    sem_close(sems->log_mutex);
    
    // Remove os semáforos do sistema - serão destruídos quando
    // todos os processos fecharem as suas referências
    sem_unlink("/ws_empty");
    sem_unlink("/ws_filled");
    sem_unlink("/ws_queue_mutex");
    sem_unlink("/ws_stats_mutex");
    sem_unlink("/ws_log_mutex");
}