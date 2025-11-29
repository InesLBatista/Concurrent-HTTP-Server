// Inês Batista, 124877
// Maria Quinteiro, 124996

#include "semaphores.h"
#include <fcntl.h>  // Para O_CREAT - opção de criação

int init_semaphores(semaphores_t* sems, int queue_size) {
    // sem_open cria semáforos "named" - visíveis entre processos diferentes
    // "/ws_empty" = nome único do semáforo no sistema
    // O_CREAT = criar se não existir, 0666 = permissões
    // queue_size = valor inicial do semáforo (lugares vazios na fila)
    sems->empty_slots = sem_open("/ws_empty", O_CREAT, 0666, queue_size);
    
    // filled_slots começa com 0 porque inicialmente não há nada na fila
    sems->filled_slots = sem_open("/ws_filled", O_CREAT, 0666, 0);
    
    // queue_mutex começa com 1 = "desbloqueado" (só um processo pode aceder)
    sems->queue_mutex = sem_open("/ws_queue_mutex", O_CREAT, 0666, 1);
    
    // stats_mutex também começa com 1 = "desbloqueado"
    sems->stats_mutex = sem_open("/ws_stats_mutex", O_CREAT, 0666, 1);
    
    // log_mutex controla acesso ao ficheiro de log (só um processo escreve de cada vez)
    sems->log_mutex = sem_open("/ws_log_mutex", O_CREAT, 0666, 1);
    
    // Verificar se TODOS os semáforos foram criados com sucesso
    // SEM_FAILED indica que houve erro na criação
    if (sems->empty_slots == SEM_FAILED || sems->filled_slots == SEM_FAILED ||
        sems->queue_mutex == SEM_FAILED || sems->stats_mutex == SEM_FAILED ||
        sems->log_mutex == SEM_FAILED) {
        return -1;  // Retorna erro se algum semáforo falhou
    }
    
    return 0;  // Sucesso
}

void destroy_semaphores(semaphores_t* sems) {
    // sem_close fecha a ligação do processo a cada semáforo
    // Isto liberta recursos mas NÃO remove o semáforo do sistema
    sem_close(sems->empty_slots);
    sem_close(sems->filled_slots);
    sem_close(sems->queue_mutex);
    sem_close(sems->stats_mutex);
    sem_close(sems->log_mutex);
    
    // sem_unlink REMOVE o semáforo do sistema
    // Isto é importante para limpeza - senão ficam semáforos "órfãos"
    sem_unlink("/ws_empty");
    sem_unlink("/ws_filled");
    sem_unlink("/ws_queue_mutex");
    sem_unlink("/ws_stats_mutex");
    sem_unlink("/ws_log_mutex");
}