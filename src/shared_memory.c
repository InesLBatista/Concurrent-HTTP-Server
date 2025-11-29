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

// Função para criar e inicializar a memória partilhada
// Retorna um ponteiro para a estrutura partilhada ou NULL se houve erro
shared_data_t* create_shared_memory() {
    // shm_open cria um segmento de memória partilhada
    // O_CREAT = criar se não existir, O_RDWR = leitura e escrita
    // 0666 = dono/grupo/outros podem ler e escrever
    int shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    if (shm_fd == -1) {
        perror("shm_open failed");  
        return NULL;               
    }
    
    // ftruncate define o tamanho do segmento de memória
    // Preciso de espaço suficiente para a estrutura shared_data_t
    if (ftruncate(shm_fd, sizeof(shared_data_t)) == -1) {
        perror("ftruncate failed");  
        close(shm_fd);              
        return NULL;                 
    }
    

    // mmap mapeia a memória partilhada no espaço de endereçamento do processo
    // NULL = o sistema escolhe o endereço, PROT_READ|PROT_WRITE = permissões
    // MAP_SHARED = mudanças são visíveis noutros processos
    shared_data_t* data = mmap(NULL, sizeof(shared_data_t),
                              PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    
    // Fecho o descritor porque mmap já criou o mapeamento
    // Não preciso mais do descritor após o mapeamento
    close(shm_fd);
    
    // Verificar se o mapeamento foi bem sucedido
    // MAP_FAILED indica que o mapeamento falhou
    if (data == MAP_FAILED) {
        perror("mmap failed");  
        return NULL;       
    }
    


    // Inicializar toda a memória a zero usando memset, garante que começamos com valores zeros conhecidos e evita lixo nas variáveis da estrutura
    memset(data, 0, sizeof(shared_data_t));
    
    // INICIALIZAR OS SEMÁFOROS PARA SINCRONIZAÇÃO
    // sem_init inicializa um semáforo para uso entre processos
    // O segundo argumento 1 indica que o semáforo é partilhado entre processos
    // O terceiro argumento é o valor inicial do semáforo
    
    // mutex: valor inicial 1 - permite acesso exclusivo
    if (sem_init(&data->mutex, 1, 1) == -1) {
        perror("sem_init mutex failed");     
        munmap(data, sizeof(shared_data_t));  
        return NULL;                          
    }
    
    // empty_slots: valor inicial MAX_QUEUE_SIZE - todos os slots começam vazios
    // Controla quantos slots vazios existem na fila //// (produtor espera aqui)
    if (sem_init(&data->empty_slots, 1, MAX_QUEUE_SIZE) == -1) {
        perror("sem_init empty_slots failed");  
        sem_destroy(&data->mutex);              // Destroi o semáforo mutex já criado
        munmap(data, sizeof(shared_data_t));    // Liberta o mapeamento de memória
        return NULL;                        
    }
    
    // full_slots: valor inicial 0 - nenhum slot preenchido no início
    // Controla quantos slots preenchidos existem //// (consumidor espera aqui)
    if (sem_init(&data->full_slots, 1, 0) == -1) {
        perror("sem_init full_slots failed");   
        sem_destroy(&data->mutex);   // Destroi o semáforo mutex
        sem_destroy(&data->empty_slots);  // Destroi o semáforo empty_slots
        munmap(data, sizeof(shared_data_t));   
        return NULL;                        
    }
    
    // Se chegámos aqui, tudo foi inicializado com sucesso
    // Retorna o ponteiro para a memória partilhada inicializada
    return data;
}

// Função para limpar e libertar a memória partilhada
// Deve ser chamada quando o servidor termina para evitar leaks !!
void destroy_shared_memory(shared_data_t* data) {
    // Verifica se o ponteiro não é NULL antes de operar
    if (data) {
        // DESTRUIR OS SEMÁFOROS - libertar recursos do sistema
        // sem_destroy liberta os recursos associados ao semáforo
        // Importante que só chamar quando nenhum processo está a usar os semáforos
        
        sem_destroy(&data->mutex);         // Destroi o semáforo de exclusão mútua
        sem_destroy(&data->empty_slots);     // Destroi o semáforo de slots vazios
        sem_destroy(&data->full_slots);     // Destroi o semáforo de slots preenchidos
        


        // LIBERTAR A MEMÓRIA MAPEADA
        // munmap remove o mapeamento de memória do processo
        // Liberta a memória do espaço de endereçamento do processo atual
        munmap(data, sizeof(shared_data_t));
        
        // REMOVER O SEGMENTO DE MEMÓRIA PARTILHADA DO SISTEMA
        // shm_unlink remove o segmento de memória partilhada do sistema
        // Isto limpa o ficheiro em /dev/shm/webserver_shm
        // !!! o segmento só é realmente removido quando todos os processos que o estão a usar fecham o mapeamento
        shm_unlink(SHM_NAME);
    }
}