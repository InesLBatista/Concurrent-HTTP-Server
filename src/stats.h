// Interface estatisticas 

// define a estrutura server_stats_t para armazenar métricas do servidor em memória partilhada
// inclui contadores de pedidos HTTP
// tem funções de atualizar/consultar estatísticas de forma thread-safe entre processos

#ifndef STATS_H
#define STATS_H

#include <time.h>
#include <sys/types.h>

// Estrutura para estatísticas do servidor partilhada entre processos
typedef struct {
    // Contadores de requests por código de status
    unsigned long total_requests;    // Total de pedidos servidos
    unsigned long total_bytes;       // Total de bytes transferidos
    unsigned long status_200;        // Requests com status 200 OK
    unsigned long status_404;        // Requests com status 404 Not Found
    unsigned long status_403;        // Requests com status 403 Forbidden
    unsigned long status_500;        // Requests com status 500 Internal Error
    unsigned long status_503;        // Requests com status 503 Service Unavailable
    unsigned long status_400;        // Requests com status 400 Bad Request
    unsigned long status_501;        // Requests com status 501 Not Implemented
    
    // Métricas de performance e carga
    unsigned long active_connections;    // Conexões ativas no momento
    unsigned long max_concurrent;        // Máximo de conexões simultâneas
    double average_response_time;        // Tempo médio de resposta em ms
    time_t server_start_time;            // Timestamp de início do servidor
    
    // Para cálculo do tempo médio de resposta
    unsigned long total_response_time_ms; // Soma acumulada dos tempos de resposta
    
    // Contadores de erro
    unsigned long connection_errors;     // Erros de conexão
    unsigned long timeout_errors;        // Timeouts
} server_stats_t;




// Inicializa o sistema de estatísticas em memória partilhada
// Retorna 0 em sucesso, -1 em erro
int stats_init(void);

// Limpa recursos do sistema de estatísticas
void stats_cleanup(void);

// Incrementa contador para um código de status HTTP específico
void stats_increment_request(int status_code);

// Adiciona bytes transferidos às estatísticas
void stats_add_bytes(size_t bytes);

// Atualiza métricas de tempo de resposta
void stats_update_response_time(long response_time_ms);

// Define o número de conexões ativas
void stats_set_active_connections(unsigned long count);

// Obtém ponteiro para as estatísticas atuais (para leitura)
const server_stats_t* stats_get(void);

// Imprime estatísticas em formato legível (para debug)
void stats_print(void);

// Exibe estatísticas formatadas (para output periódico do master process)
void stats_display(void);

// Retorna o tempo de atividade do servidor em segundos
long stats_get_uptime(void);

// Retorna requests por segundo (média desde o início)
double stats_get_requests_per_second(void);

// Incrementa contador de erros de conexão
void stats_increment_connection_error(void);

// Incrementa contador de timeouts
void stats_increment_timeout_error(void);


#endif