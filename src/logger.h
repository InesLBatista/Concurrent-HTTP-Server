#ifndef LOGGER_H
#define LOGGER_H

#include <stdio.h>
#include <time.h>
#include <pthread.h>
#include <semaphore.h>

#define LOG_BUFFER_SIZE 1000      /* Número máximo de entradas no buffer */
#define LOG_ENTRY_SIZE 512        /* Tamanho máximo de cada entrada de log */
#define LOG_MAX_FILE_SIZE (10 * 1024 * 1024)  /* 10MB para rotação */
#define LOG_MAX_BACKUP_FILES 5    /* Número máximo de arquivos de backup */

/* Níveis de log */
typedef enum {
    LOG_LEVEL_DEBUG = 0,
    LOG_LEVEL_INFO,
    LOG_LEVEL_WARNING,
    LOG_LEVEL_ERROR,
    LOG_LEVEL_FATAL
} log_level_t;

/* Estrutura para uma entrada de log em buffer */
typedef struct {
    char message[LOG_ENTRY_SIZE];
    time_t timestamp;
    log_level_t level;
} log_entry_t;

/* Estrutura principal do logger */
typedef struct {
    FILE* log_file;               /* Arquivo de log atual */
    char log_filename[256];       /* Nome do arquivo de log */
    
    log_entry_t buffer[LOG_BUFFER_SIZE];  /* Buffer circular */
    int buffer_start;             /* Índice do início do buffer */
    int buffer_end;               /* Índice do fim do buffer */
    int buffer_count;             /* Número de entradas no buffer */
    
    pthread_mutex_t buffer_mutex; /* Mutex para acesso ao buffer */
    sem_t write_semaphore;        /* Semáforo para escrita atômica no arquivo */
    
    pthread_t flush_thread;       /* Thread para flush periódico */
    volatile int flush_running;   /* Flag para controle da thread de flush */
    
    int rotation_enabled;         /* Habilitar rotação de logs */
    size_t max_file_size;         /* Tamanho máximo do arquivo */
    int max_backup_files;         /* Número máximo de backups */
} logger_t;

/* Inicialização e destruição */
logger_t* logger_init(const char* filename, int rotation_enabled);
void logger_destroy(logger_t* logger);

/* Funções de logging */
void logger_log(logger_t* logger, log_level_t level, 
                const char* remote_addr, const char* user,
                const char* method, const char* uri, 
                const char* protocol, int status, 
                size_t bytes_sent, const char* referer,
                const char* user_agent);

/* Funções de conveniência */
void logger_debug(logger_t* logger, const char* remote_addr,
                  const char* method, const char* uri, 
                  const char* protocol, int status, 
                  size_t bytes_sent);
                  
void logger_info(logger_t* logger, const char* remote_addr,
                 const char* method, const char* uri, 
                 const char* protocol, int status, 
                 size_t bytes_sent);
                 
void logger_error(logger_t* logger, const char* remote_addr,
                  const char* method, const char* uri, 
                  const char* protocol, int status, 
                  size_t bytes_sent);

/* Gerenciamento de buffer e flush */
void logger_flush(logger_t* logger);
void logger_set_immediate_flush(logger_t* logger, int enable);

/* Rotação de logs */
int logger_rotate(logger_t* logger);
void logger_set_max_size(logger_t* logger, size_t max_size);
void logger_set_max_backups(logger_t* logger, int max_backups);

/* Estatísticas */
size_t logger_get_buffer_count(logger_t* logger);
size_t logger_get_file_size(logger_t* logger);

#endif /* LOGGER_H */
#endif
// Inês Batista, 124877
// Maria Quinteiro, 124996

#ifndef LOGGER_H
#define LOGGER_H

#include <semaphore.h>
#include <stddef.h> // Para size_t
#include "http.h"   // Para http_request_t (detalhes do pedido)

// O nome do ficheiro de log de acesso
#define LOG_FILENAME "access.log"

// Função para registar o pedido no ficheiro de log (thread-safe).
void log_request(const char *remote_ip, const http_request_t *req, 
                 int status_code, size_t bytes_sent, sem_t *log_mutex);

#endif 
