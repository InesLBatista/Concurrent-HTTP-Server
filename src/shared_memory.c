// Inês Batista, 124877
// Maria Quinteiro, 124996

#include "shared_memory.h"       
#include <sys/mman.h>    // Para mmap, munmap - gerir memória partilhada
#include <fcntl.h>       // Para O_CREAT, O_RDWR - opções de criação de ficheiros
#include <unistd.h>      // Para close, ftruncate - operações de sistema
#include <string.h>      // Para memset - preencher memória com zeros
#include <stdio.h>     // Para perror - mostrar mensagens de erro

// Nome único para identificar a nossa memória partilhada
// Aparece em /dev/shm/webserver_shm no sistema
#define SHM_NAME "/webserver_shm"

// Definição da variável global (declarada em shared_memory.h).
shared_data_t *g_shared_data = NULL; // Ponteiro global para a estrutura de dados na memória partilhada.

// Função para criar, mapear e inicializar a memória partilhada.
shared_data_t* create_shared_memory() {
    int shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666); // Tenta criar ou abrir o segmento de memória partilhada.
    if (shm_fd == -1) {
        perror("shm_open failed");  // Reporta erro se a criação/abertura falhar.
        return NULL;               // Retorna NULL para indicar falha.
    }
    
    if (ftruncate(shm_fd, sizeof(shared_data_t)) == -1) { // Define o tamanho do segmento de memória partilhada.
        perror("ftruncate failed");  // Reporta erro se não conseguir definir o tamanho.
        close(shm_fd);              // Fecha o descritor de ficheiro.
        return NULL;                 // Retorna NULL.
    }
    
    g_shared_data = mmap(NULL, sizeof(shared_data_t), // Mapeia o segmento para o espaço de endereçamento do processo atual.
                              PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0); // Permissões de leitura/escrita, partilhado entre processos.
    
    close(shm_fd); // O descritor de ficheiro pode ser fechado após o mapeamento.
    
    if (g_shared_data == MAP_FAILED) { // Verifica se o mapeamento falhou.
        perror("mmap failed");  // Reporta erro de mapeamento.
        g_shared_data = NULL; // Reseta o ponteiro global.
        return NULL;       // Retorna NULL para indicar falha.
    }
    
    // Inicializar toda a memória a zero (boa prática para garantir estado inicial limpo).
    memset(g_shared_data, 0, sizeof(shared_data_t)); // Limpa a SHM, incluindo a fila e estatísticas.
    
    // INICIALIZAR OS SEMÁFOROS POSIX (o '1' no 2º argumento indica partilhado entre processos).
    
    // mutex: valor inicial 1 - permite acesso exclusivo à fila.
    if (sem_init(&g_shared_data->mutex, 1, 1) == -1) { // Inicializa o semáforo de exclusão mútua com valor 1.
        perror("sem_init mutex failed");     // Reporta erro na inicialização do mutex.
        munmap(g_shared_data, sizeof(shared_data_t));  // Limpa o mapeamento se a inicialização falhar.
        g_shared_data = NULL; // Reseta o ponteiro.
        return NULL;                          // Retorna NULL.
    }
    
    // empty_slots: valor inicial MAX_QUEUE_SIZE - produtor espera aqui.
    if (sem_init(&g_shared_data->empty_slots, 1, MAX_QUEUE_SIZE) == -1) { // Inicializa com o tamanho máximo da fila (todos os slots vazios).
        perror("sem_init empty_slots failed");  // Reporta erro.
        sem_destroy(&g_shared_data->mutex);              // Destrói o mutex já criado.
        munmap(g_shared_data, sizeof(shared_data_t));    // Limpa o mapeamento.
        g_shared_data = NULL;
        return NULL;                        // Retorna NULL.
    }
    
    // full_slots: valor inicial 0 - consumidor espera aqui.
    if (sem_init(&g_shared_data->full_slots, 1, 0) == -1) { // Inicializa com 0 (a fila está vazia no início).
        perror("sem_init full_slots failed");   // Reporta erro.
        sem_destroy(&g_shared_data->mutex);   // Destrói o mutex.
        sem_destroy(&g_shared_data->empty_slots); // Destrói o semáforo empty_slots.
        munmap(g_shared_data, sizeof(shared_data_t));   // Limpa o mapeamento.
        g_shared_data = NULL;
        return NULL;                        // Retorna NULL.
    }
    
    printf("Memória Partilhada (POSIX SHM %s) e Semáforos inicializados.\n", SHM_NAME); // Mensagem de sucesso.
    return g_shared_data; // Retorna o ponteiro para a estrutura partilhada.
}

// Função para limpar e libertar a memória partilhada (chamada pelo Master).
void destroy_shared_memory(shared_data_t* data) {
    if (data) { // Verifica se o ponteiro é válido.
        // DESTRUIR OS SEMÁFOROS (libertar recursos do kernel).
        sem_destroy(&data->mutex);         // Destrói o semáforo de exclusão mútua.
        sem_destroy(&data->empty_slots);     // Destrói o semáforo de slots vazios.
        sem_destroy(&data->full_slots);     // Destrói o semáforo de slots preenchidos.

        // LIBERTAR A MEMÓRIA MAPEADA (desanexar do espaço de endereçamento do processo).
        munmap(data, sizeof(shared_data_t)); // Remove o mapeamento do segmento.
        
        // REMOVER O SEGMENTO DE MEMÓRIA PARTILHADA DO SISTEMA.
        shm_unlink(SHM_NAME); // Elimina o nome do segmento do sistema de ficheiros (o segmento só desaparece quando não houver referências).
    }
    g_shared_data = NULL; // Garante que o ponteiro global é nulo após a destruição.
    printf("Memória Partilhada destruída.\n"); // Mensagem de confirmação.
}



// Adiciona um descritor de ficheiro à fila de forma segura (Produtor: Master).
int enqueue_connection(int client_fd) {
    if (g_shared_data == NULL) return -1; // Verifica se a SHM está inicializada.
    
    // 1. Esperar por um slot vazio na fila (pode bloquear se a fila estiver cheia).
    if (sem_wait(&g_shared_data->empty_slots) == -1) { // Decrementa a contagem de slots vazios.
        perror("sem_wait empty_slots failed"); // Reporta erro (ex: interrupção por sinal).
        return -1; // Retorna código de erro.
    }
    
    // 2. Obter acesso exclusivo à fila (Secção Crítica).
    if (sem_wait(&g_shared_data->mutex) == -1) { // Bloqueia o mutex para entrar na secção crítica.
        perror("sem_wait mutex failed"); // Reporta erro.
        sem_post(&g_shared_data->empty_slots); // Reverte a operação anterior (empty_slots).
        return -1; // Retorna código de erro.
    }
    
    // 3. SECÇÃO CRÍTICA - Adicionar ligação à fila.
    g_shared_data->queue.sockets[g_shared_data->queue.rear] = client_fd; // Insere o FD do cliente no índice 'rear'.
    g_shared_data->queue.rear = (g_shared_data->queue.rear + 1) % MAX_QUEUE_SIZE; // Move o índice 'rear' de forma circular.
    g_shared_data->queue.count++; // Incrementa o contador de elementos na fila.
    
    // 4. Libertar o mutex da fila.
    sem_post(&g_shared_data->mutex); // Liberta o mutex, permitindo que outro processo aceda.
    
    // 5. Sinalizar que há um novo elemento na fila.
    sem_post(&g_shared_data->full_slots); // Incrementa a contagem de slots preenchidos (acorda um Worker).

    return 0; // Retorna sucesso.
}

// Retira um descritor de ficheiro da fila de forma segura (Consumidor: Worker).
int dequeue_connection(void) {
    int client_fd = -1; // Variável para armazenar o FD a ser retirado.
    if (g_shared_data == NULL) return -1; // Verifica se a SHM está inicializada.

    // 1. Esperar por um slot preenchido na fila (bloqueia se vazia).
    if (sem_wait(&g_shared_data->full_slots) == -1) { // Decrementa a contagem de slots preenchidos (espera por trabalho).
        perror("sem_wait full_slots failed"); // Reporta erro.
        return -1; // Retorna código de erro.
    }
    
    // 2. Obter acesso exclusivo à fila (Secção Crítica).
    if (sem_wait(&g_shared_data->mutex) == -1) { // Bloqueia o mutex para garantir acesso exclusivo.
        perror("sem_wait mutex failed"); // Reporta erro.
        sem_post(&g_shared_data->full_slots); // Reverte a operação anterior (full_slots).
        return -1; // Retorna código de erro.
    }
    
    // 3. SECÇÃO CRÍTICA - Retirar ligação da fila.
    client_fd = g_shared_data->queue.sockets[g_shared_data->queue.front]; // Retira o FD do índice 'front'.
    g_shared_data->queue.front = (g_shared_data->queue.front + 1) % MAX_QUEUE_SIZE; // Move o índice 'front' de forma circular.
    g_shared_data->queue.count--; // Decrementa o contador de elementos na fila.
    
    // 4. Libertar o mutex da fila.
    sem_post(&g_shared_data->mutex); // Liberta o mutex.
    
    // 5. Sinalizar que há um slot vazio.
    sem_post(&g_shared_data->empty_slots); // Incrementa a contagem de slots vazios (informa o Master).

    return client_fd; // Retorna o descritor de ficheiro do cliente.
}