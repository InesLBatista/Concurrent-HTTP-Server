

// Inês Batista, 124877
// Maria Quinteiro, 124996

#include "shared_memory.h"       
#include <sys/mman.h>    // Para mmap, munmap - gerir memória partilhada
#include <fcntl.h>       // Para O_CREAT, O_RDWR - opções de criação de ficheiros
#include <unistd.h>      // Para close, ftruncate - operações de sistema
#include <string.h>      // Para memset - preencher memória com zeros
#include <stdio.h>       // Para perror - mostrar mensagens de erro
#include <errno.h>       // Para errno e EINTR

// Definição da variável global (declarada em shared_memory.h).
// Usa o SHM_NAME definido em shared_memory.h.
shared_data_t *g_shared_data = NULL; // Ponteiro global para a estrutura de dados na memória partilhada.


// Função para criar, mapear e inicializar a memória partilhada.
shared_data_t* create_shared_memory() {
    // 1. Abrir/Criar o segmento de memória partilhada.
    int shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666); 
    if (shm_fd == -1) {
        perror("shm_open failed");
        return NULL;
    }
    
    // 2. Definir o tamanho.
    if (ftruncate(shm_fd, sizeof(shared_data_t)) == -1) { 
        perror("ftruncate failed");
        close(shm_fd);
        return NULL;
    }
    
    // 3. Mapear o segmento para o espaço de endereçamento do processo.
    g_shared_data = mmap(NULL, sizeof(shared_data_t), 
                              PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0); 
    
    close(shm_fd); // O descritor pode ser fechado após o mapeamento.
    
    if (g_shared_data == MAP_FAILED) { 
        perror("mmap failed");
        shm_unlink(SHM_NAME); // Limpa o segmento em caso de falha.
        g_shared_data = NULL;
        return NULL;
    }
    
    // 4. Inicializar a memória a zero (boa prática).
    memset(g_shared_data, 0, sizeof(shared_data_t));
    
    // 5. INICIALIZAR OS SEMÁFOROS (o '1' no 2º argumento indica partilhado entre processos).
    
    // mutex: acesso exclusivo (valor inicial 1).
    if (sem_init(&g_shared_data->mutex, 1, 1) == -1) { 
        perror("sem_init mutex failed");
        munmap(g_shared_data, sizeof(shared_data_t));
        shm_unlink(SHM_NAME);
        g_shared_data = NULL;
        return NULL;
    }
    
    // empty_slots: slots vazios para o Produtor (Master) (valor inicial MAX_QUEUE_SIZE).
    if (sem_init(&g_shared_data->empty_slots, 1, MAX_QUEUE_SIZE) == -1) { 
        perror("sem_init empty_slots failed");
        sem_destroy(&g_shared_data->mutex);
        munmap(g_shared_data, sizeof(shared_data_t));
        shm_unlink(SHM_NAME);
        g_shared_data = NULL;
        return NULL;
    }
    
    // full_slots: slots preenchidos para o Consumidor (Worker) (valor inicial 0).
    if (sem_init(&g_shared_data->full_slots, 1, 0) == -1) {
        perror("sem_init full_slots failed");
        sem_destroy(&g_shared_data->mutex);
        sem_destroy(&g_shared_data->empty_slots);
        munmap(g_shared_data, sizeof(shared_data_t));
        shm_unlink(SHM_NAME);
        g_shared_data = NULL;
        return NULL;
    }
    
    printf("Memória Partilhada (POSIX SHM %s) e Semáforos inicializados.\n", SHM_NAME);
    return g_shared_data; 
}


// Função para limpar e libertar a memória partilhada (chamada pelo Master).
void destroy_shared_memory(shared_data_t* data) {
    if (data) { 
        // 1. DESTRUIR OS SEMÁFOROS.
        sem_destroy(&data->mutex);
        sem_destroy(&data->empty_slots);
        sem_destroy(&data->full_slots);

        // 2. LIBERTAR A MEMÓRIA MAPEADA.
        munmap(data, sizeof(shared_data_t));
        
        // 3. REMOVER O SEGMENTO DE MEMÓRIA PARTILHADA DO SISTEMA.
        shm_unlink(SHM_NAME);
    }
    g_shared_data = NULL;
    printf("Memória Partilhada destruída.\n");
}



// O Master usará sem_trywait no master.c para lidar com o 503, mas o worker.c precisa de uma versão bloqueante.
int enqueue_connection(int client_fd) {
    if (g_shared_data == NULL) return -1;
    
    // Não precisamos de sem_wait aqui, pois o Master implementará a lógica de 503 com o uso de sem_trywait diretamente no master.c. 
    // Esta função é apenas um placeholder para um enqueue simples, mas não será chamada diretamente pelo Master.

    return -1; // Retorna -1 para indicar que esta função não deve ser usada diretamente pelo Master.
}



// Retira um descritor de ficheiro da fila de forma segura (Consumidor: Worker).
int dequeue_connection(void) {
    int client_fd = -1;
    if (g_shared_data == NULL) return -1;

    // 1. Esperar por um slot preenchido (Bloqueia Worker se fila vazia).
    // O loop trata de interrupções (como SIGTERM) para permitir o shutdown gracioso.
    while (sem_wait(&g_shared_data->full_slots) == -1) {
        if (errno != EINTR) {
            perror("sem_wait full_slots failed");
            return -1;
        }
        // Se EINTR (interrupção de sinal), tenta novamente ou o worker_main trata.
        return -1; 
    }
    
    // 2. Obter acesso exclusivo à fila.
    if (sem_wait(&g_shared_data->mutex) == -1) {
        perror("sem_wait mutex failed");
        sem_post(&g_shared_data->full_slots); // Reverte a operação.
        return -1;
    }
    
    // 3. SECÇÃO CRÍTICA - Retirar ligação da fila.
    if (g_shared_data->queue.count > 0) { 
        client_fd = g_shared_data->queue.sockets[g_shared_data->queue.head]; 
        g_shared_data->queue.head = (g_shared_data->queue.head + 1) % MAX_QUEUE_SIZE; 
        g_shared_data->queue.count--;
    } 
    
    // 4. Libertar o mutex.
    sem_post(&g_shared_data->mutex);
    
    // 5. Sinalizar que há um slot vazio (informa o Master).
    sem_post(&g_shared_data->empty_slots);

    return client_fd;
}