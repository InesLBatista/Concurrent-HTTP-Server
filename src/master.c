// Inês Batista, 124877
// Maria Quinteiro, 124996

#include "master.h"
#include "config.h"
#include "shared_memory.h"
#include "semaphores.h"
#include <stdio.h>          // Para printf, perror
#include <stdlib.h>         // Para exit
#include <unistd.h>         // Para close, fork, getpid
#include <sys/socket.h>     // Para socket, setsockopt, bind, listen, accept
#include <netinet/in.h>     // Para sockaddr_in
#include <signal.h>         // Para signal, SIGINT, SIGTERM, kill
#include <sys/wait.h>       // Para waitpid
#include <string.h>         // Para memset

#include <errno.h>              // Inclui a variável global errno e a constante EINTR para verificação de erros de sistema.
#include <sys/mman.h>           // Inclui munmap para desmapear a SHM no Worker (embora o Worker seja um TODO aqui).

// Variável global para controlar o loop principal do master
// volatile: garante que o compilador não optimiza o acesso a esta variável
// sig_atomic_t: tipo que garante acesso atómico em handlers de sinal
volatile sig_atomic_t keep_running = 1;

// Array para guardar os PIDs dos processos worker
// Permite ao master controlar e terminar os workers graciosamente
#define MAX_WORKERS 100                 // Define o número máximo de Workers que este array pode armazenar.
static pid_t worker_pids[MAX_WORKERS];  // Array estático para registar os PIDs dos filhos Worker.
static int num_workers_global = 0;


// Define a resposta HTTP 503 Service Unavailable (Serviço Indisponível).
static const char* HTTP_503_MESSAGE =
    "HTTP/1.1 503 Service Unavailable\r\n"   // Define o status code 503.
    "Content-Type: text/plain\r\n"          // Define o tipo de conteúdo como texto simples.
    "Connection: close\r\n"                 // Indica ao cliente para fechar a conexão após esta resposta.
    "Retry-After: 60\r\n"                   // Sugere ao cliente que tente novamente após 60 segundos.
    "Content-Length: 35\r\n"                // Tamanho do corpo da mensagem em bytes.
    "\r\n"                                  // Linha vazia que separa os cabeçalhos do corpo.
    "503: Server Queue is currently full."; // O corpo da mensagem (35 bytes).




// Handler para sinais (Ctrl+C)
// Permite parar o servidor graciosamente
void signal_handler(int signum) {
    keep_running = 0;  // Altera a flag para sair do loop principal
    printf("\nReceived signal %d. Gracefully stopping server...\n", signum);
}

// Função para criar o socket do servidor (do template do professor)
int create_server_socket(int port) {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0); // Cria um novo socket para TCP/IPv4.
    if (sockfd < 0) {
        perror("socket failed");    // Reporta erro na criação do socket.
        return -1;                  // Retorna falha.
    }
    
    int opt = 1; // Valor para a opção SO_REUSEADDR.
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)); // Permite a reutilização imediata do endereço/porta após o fecho.
    
    struct sockaddr_in addr; // Estrutura para o endereço de rede do servidor.
    memset(&addr, 0, sizeof(addr)); // Inicializa a estrutura do endereço a zero.
    addr.sin_family = AF_INET;           // Define a família de endereços para IPv4.
    addr.sin_addr.s_addr = INADDR_ANY;   // Aceita conexões em todas as interfaces de rede disponíveis.
    addr.sin_port = htons(port);         // Define a porta, convertendo para network byte order.
    
    if (bind(sockfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) { // Associa o socket ao endereço e porta definidos.
        perror("bind failed");          // Reporta erro de associação.
        close(sockfd);                  // Fecha o socket em caso de erro.
        return -1;                      // Retorna falha.
    }
    
    if (listen(sockfd, 128) < 0) { // Coloca o socket em modo de escuta, definindo o backlog (fila de espera) para 128.
        perror("listen failed");        // Reporta erro de escuta.
        close(sockfd);                  // Fecha o socket em caso de erro.
        return -1;                      // Retorna falha.
    }
    
    return sockfd; // Retorna o descritor do socket em escuta.
}



// Função para criar processos worker usando fork()
// config: configurações do servidor
// shared_data: memória partilhada com a fila
// sems: semáforos para sincronização
// Retorna: número de workers criados com sucesso
int create_worker_processes(server_config_t* config) {
    if (config->num_workers > MAX_WORKERS) { // Verifica se o número configurado excede o tamanho do array de PIDs.
        fprintf(stderr, "Aviso: Número de workers (%d) excede o máximo suportado (%d).\n", // Emite um aviso.
                config->num_workers, MAX_WORKERS);
        config->num_workers = MAX_WORKERS; // Limita o número de workers ao máximo.
    }

    printf("Creating %d worker processes...\n", config->num_workers); // Informa o número de workers a serem criados.
    
    num_workers_global = 0; // Reinicia o contador de workers.
    
    for (int i = 0; i < config->num_workers; i++) {
        pid_t pid = fork(); // Cria um novo processo (cópia do Master).
        
        if (pid == -1) {
            perror("fork failed"); // Reporta erro se o fork falhar.
            break; // Sai do loop para tentar terminar o que foi criado.
        }
        else if (pid == 0) {
            // CÓDIGO DO PROCESSO WORKER (filho)
            printf("Worker %d (PID: %d) started\n", i, getpid()); // O Worker imprime o seu ID e PID.
            
            // TODO: Chamar worker_main(config) aqui, quando worker.c estiver pronto.
            while(1) { // Loop infinito temporário para simular o worker à espera de conexões.
                sleep(60); // Simula que está à espera de trabalho ou a processar.
            } 
            
            // O worker deve desanexar (detach) da SHM antes de sair.
            if (g_shared_data != NULL) {
                munmap(g_shared_data, sizeof(shared_data_t)); // Desmapeia a SHM do espaço de endereçamento do Worker.
            }
            exit(0); // O processo Worker termina.
        }
        else {
            // CÓDIGO DO PROCESSO MASTER (pai)
            worker_pids[num_workers_global] = pid; // O Master guarda o PID do Worker para futura gestão.
            num_workers_global++; // Incrementa o contador de Workers criados.
            printf("Worker %d created with PID: %d\n", i, pid); // O Master informa o PID do novo Worker.
        }
    }
    
    return num_workers_global; // Retorna o número total de Workers criados com sucesso.
}

// Função para terminar todos os processos worker graciosamente
// Envia sinal SIGTERM a todos os workers e espera que terminem
void terminate_worker_processes() {
    printf("Terminating worker processes...\n");
    
    for (int i = 0; i < num_workers_global; i++) {
        if (worker_pids[i] > 0) {
            printf("   - Sending SIGTERM to worker PID: %d\n", worker_pids[i]);
            
            // Enviar sinal SIGTERM para o worker
            // SIGTERM é um sinal de terminação graciosa
            kill(worker_pids[i], SIGTERM);
            
            // Esperar que o worker termine
            // waitpid bloqueia até o processo filho terminar
            // NULL = não nos interessa o status de saída
            waitpid(worker_pids[i], NULL, 0);
            
            printf("   Worker PID: %d terminated\n", worker_pids[i]);
        }
    }
}


// Envia uma resposta HTTP 503 ao cliente e fecha o descritor.
void send_503_response(int client_fd) {
    // Envia a resposta HTTP 503 Service Unavailable
    send(client_fd, HTTP_503_MESSAGE, strlen(HTTP_503_MESSAGE), 0); // Usa send para garantir que todos os bytes são escritos se possível.
    
    // Incrementa a contagem de erros 500 para fins estatísticos (embora 503 seja mais específico).
    // Nota: Muitos sistemas contam 503 como 5xx, mas para precisão, deveria ter um campo 503.
    if (g_shared_data) { // Verifica se a memória partilhada está acessível.
        sem_wait(&g_shared_data->mutex); // Bloqueia o mutex para atualizar as estatísticas de forma segura.
        g_shared_data->stats.status_500++; // Incrementa o contador de erros 5xx (Usamos o 500 como fallback para 5xx genérico).
        sem_post(&g_shared_data->mutex); // Liberta o mutex.
    }
    
    close(client_fd); // Fecha a conexão do cliente.
    printf("NOTICE: Queue full. Sent HTTP 503 to client (socket %d).\n", client_fd); // Mensagem de log para o console.
}

// Função principal do processo master
// Configura o servidor, cria workers e gere ligações
int master_main(server_config_t* config) {
    printf("MASTER PROCESS (PID: %d) - Starting...\n", getpid()); // O Master imprime o seu próprio PID.
    
    // 1. CONFIGURAR HANDLERS DE SINAL
    signal(SIGINT, signal_handler);   // Associa a função signal_handler ao sinal SIGINT (Ctrl+C).
    signal(SIGTERM, signal_handler);  // Associa a função signal_handler ao sinal SIGTERM (sinal de terminação).
    
    // 2. CRIAR MEMÓRIA PARTILHADA E SEMÁFOROS
    printf("Initializing shared resources...\n"); // Inicia a configuração de recursos partilhados.
    
    // Cria a SHM e inicializa os semáforos DENTRO da SHM.
    shared_data_t* shared_data = create_shared_memory(); // Chama a função que cria, mapeia e inicializa a SHM.
    if (!shared_data) {
        printf("ERROR: Could not create shared memory\n"); // Trata o erro de criação de SHM.
        return -1; // Termina com erro.
    }
    printf("Shared memory created and semaphores initialized\n"); // Confirma a criação.
    
    // 3. CRIAR SOCKET DO SERVIDOR
    int server_fd = create_server_socket(config->port); // Cria o socket que escuta as conexões na porta configurada.
    if (server_fd < 0) {
        printf("ERROR: Could not create server socket\n"); // Trata o erro de criação do socket.
        destroy_shared_memory(shared_data); // Limpa a SHM antes de terminar.
        return -1; // Termina com erro.
    }
    printf("Server socket created on port %d\n", config->port); // Confirma o sucesso.
    
    // 4. CRIAR PROCESSOS WORKER
    int created_workers = create_worker_processes(config); // Executa o fork para criar os Workers.
    if (created_workers == 0) {
        printf("ERROR: Could not create any workers\n"); // Trata o erro se nenhum Worker for criado.
        close(server_fd); // Fecha o socket de escuta.
        destroy_shared_memory(shared_data); // Limpa a SHM.
        return -1; // Termina com erro.
    }
    printf("%d worker processes created successfully\n", created_workers); // Confirma o número de Workers.
    
    // 5. LOOP PRINCIPAL DO MASTER - ACEITAR LIGAÇÕES
    printf("\nMaster ready to accept connections...\n"); // O Master está pronto para o ciclo de aceitação.
    printf("Server running at: http://localhost:%d\n", config->port); // Informa o endereço do servidor.
    printf("Press Ctrl+C to stop the server\n\n"); // Instruções para o utilizador.
    
    int connection_count = 0; // Contador de conexões aceites desde o início.
    
    while (keep_running) { // O loop continua até que a flag seja alterada pelo signal_handler.
        struct sockaddr_in client_addr; // Estrutura para armazenar o endereço do cliente.
        socklen_t client_len = sizeof(client_addr); // Variável para o tamanho da estrutura do endereço.
        
        // Aceitar uma nova ligação de cliente
        int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len); // Bloqueia e espera por uma nova conexão.
        
        if (client_fd < 0) {
            // Se accept for interrompido por um sinal (como SIGINT), ignoramos e verificamos 'keep_running'.
            if (errno == EINTR) continue; // Continua para reavaliar 'keep_running' se for interrompido por sinal.
            
            if (keep_running) {
                perror("accept failed"); // Reporta erro se não for uma terminação.
            }
            continue; // Continua o loop.
        }
        
        connection_count++; // Incrementa o contador de conexões aceites.
        



        // LÓGICA DE ENFILEIRAMENTO NÃO-BLOQUEANTE (PRODUCER)
        // Protocolo: empty_slots (try_wait) -> mutex (wait) -> filled_slots (post)

        
        // 1. Tentar obter um slot vazio (Não bloquear se a fila estiver cheia).
        // sem_trywait retorna -1 e define errno como EAGAIN se o semáforo for 0.
        if (sem_trywait(&shared_data->empty_slots) == -1) { 
            if (errno == EAGAIN) { // Se a fila estiver cheia (semáforo empty_slots = 0).
                send_503_response(client_fd); // Envia resposta 503 e fecha o socket.
                continue; // Continua para aceitar a próxima conexão.
            }
            // Outros erros de sem_trywait (menos provável, mas tratado).
            perror("sem_trywait empty_slots failed unexpectedly"); 
            close(client_fd);
            continue;
        }

        // 2. Obter acesso exclusivo à fila (Secção Crítica).
        // Nota: O Master bloqueia aqui se um Worker ou outro Master estiver na secção crítica.
        if (sem_wait(&shared_data->mutex) == -1) { 
            perror("sem_wait mutex failed"); // Reporta erro.
            sem_post(&shared_data->empty_slots); // Reverte o decremento do empty_slots.
            close(client_fd); // Fecha a conexão em caso de erro irrecuperável.
            continue; 
        }

        // 3. SECÇÃO CRÍTICA - Adicionar ligação à fila.
        shared_data->queue.sockets[shared_data->queue.rear] = client_fd; // Insere o FD do cliente.
        shared_data->queue.rear = (shared_data->queue.rear + 1) % MAX_QUEUE_SIZE; // Atualiza o índice 'rear' circularmente.
        shared_data->queue.count++; // Incrementa o contador de elementos na fila.
        
        // 4. Libertar o mutex da fila.
        sem_post(&shared_data->mutex); // Liberta o mutex, permitindo que outro processo aceda.
        
        // 5. Sinalizar que há um novo elemento na fila.
        sem_post(&shared_data->full_slots); // Incrementa o full_slots (acorda um Worker).
        
        // Log de sucesso
        printf("Connection %d accepted (socket %d) - Queued: %d/%d\n", 
               connection_count, client_fd, shared_data->queue.count, MAX_QUEUE_SIZE);
        
    }
    
    // 6. LIMPEZA FINAL - executado quando o servidor para (graceful shutdown)
    printf("\nMASTER PROCESS - Performing cleanup...\n"); // Inicia a fase de terminação graciosa.
    
    // Fechar socket do servidor
    close(server_fd); // Fecha o socket de escuta, impedindo novas conexões.
    printf("Server socket closed\n"); // Confirma o fecho.
    
    // Terminar workers graciosamente
    terminate_worker_processes(); // Envia SIGTERM a todos os Workers e espera que terminem.
    
    // Destruir semáforos e memória partilhada
    destroy_shared_memory(shared_data); // Limpa e remove o segmento de memória partilhada.
    
    printf("Master terminated gracefully\n"); // Confirma a terminação bem-sucedida.
    return 0; // O programa termina com sucesso.
}