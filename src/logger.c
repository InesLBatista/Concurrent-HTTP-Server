// Inês Batista, 124877
// Maria Quinteiro, 124996

#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdarg.h>

/* Função auxiliar para obter timestamp formatado */
static void get_formatted_time(char* buffer, size_t size, time_t timestamp) {
    struct tm* tm_info = localtime(&timestamp);
    strftime(buffer, size, "%d/%b/%Y:%H:%M:%S %z", tm_info);
}

/* Função auxiliar para formatar entrada no padrão Apache Combined */
static void format_apache_combined(char* dest, size_t dest_size,
                                   const char* remote_addr,
                                   const char* user,
                                   const char* timestamp,
                                   const char* method,
                                   const char* uri,
                                   const char* protocol,
                                   int status,
                                   size_t bytes_sent,
                                   const char* referer,
                                   const char* user_agent) {
    /* Formato Apache Combined Log Format */
    snprintf(dest, dest_size,
             "%s - %s [%s] \"%s %s %s\" %d %zu \"%s\" \"%s\"\n",
             remote_addr ? remote_addr : "-",
             user ? user : "-",
             timestamp,
             method ? method : "-",
             uri ? uri : "-",
             protocol ? protocol : "-",
             status,
             bytes_sent,
             referer ? referer : "-",
             user_agent ? user_agent : "-");
}

/* Função para escrever uma entrada no arquivo de log */
static void write_log_entry(logger_t* logger, const log_entry_t* entry) {
    if (!logger || !logger->log_file) return;
    
    /* Usar semáforo para escrita atômica */
    sem_wait(&logger->write_semaphore);
    
    /* Escrever no arquivo */
    fputs(entry->message, logger->log_file);
    fflush(logger->log_file);
    
    /* Verificar se precisa rotacionar */
    if (logger->rotation_enabled) {
        long current_pos = ftell(logger->log_file);
        if (current_pos > 0 && (size_t)current_pos >= logger->max_file_size) {
            logger_rotate(logger);
        }
    }
    
    sem_post(&logger->write_semaphore);
}

/* Thread para flush periódico do buffer */
static void* flush_thread_func(void* arg) {
    logger_t* logger = (logger_t*)arg;
    
    while (logger->flush_running) {
        sleep(5); /* Flush a cada 5 segundos */
        
        if (logger->buffer_count > 0) {
            pthread_mutex_lock(&logger->buffer_mutex);
            
            /* Escrever todas as entradas do buffer */
            while (logger->buffer_count > 0) {
                log_entry_t* entry = &logger->buffer[logger->buffer_start];
                write_log_entry(logger, entry);
                
                logger->buffer_start = (logger->buffer_start + 1) % LOG_BUFFER_SIZE;
                logger->buffer_count--;
            }
            
            logger->buffer_end = logger->buffer_start;
            pthread_mutex_unlock(&logger->buffer_mutex);
        }
    }
    
    return NULL;
}

/* Função para adicionar entrada ao buffer */
static void buffer_add_entry(logger_t* logger, const log_entry_t* entry) {
    pthread_mutex_lock(&logger->buffer_mutex);
    
    /* Se o buffer está cheio, fazer flush imediato */
    if (logger->buffer_count >= LOG_BUFFER_SIZE) {
        pthread_mutex_unlock(&logger->buffer_mutex);
        logger_flush(logger);
        pthread_mutex_lock(&logger->buffer_mutex);
    }
    
    /* Adicionar entrada ao buffer */
    memcpy(&logger->buffer[logger->buffer_end], entry, sizeof(log_entry_t));
    logger->buffer_end = (logger->buffer_end + 1) % LOG_BUFFER_SIZE;
    logger->buffer_count++;
    
    pthread_mutex_unlock(&logger->buffer_mutex);
}

/* Inicializar logger */
logger_t* logger_init(const char* filename, int rotation_enabled) {
    logger_t* logger = malloc(sizeof(logger_t));
    if (!logger) {
        perror("Failed to allocate logger");
        return NULL;
    }
    
    memset(logger, 0, sizeof(logger_t));
    
    /* Configurar nome do arquivo */
    strncpy(logger->log_filename, filename, sizeof(logger->log_filename) - 1);
    
    /* Abrir arquivo de log */
    logger->log_file = fopen(filename, "a");
    if (!logger->log_file) {
        perror("Failed to open log file");
        free(logger);
        return NULL;
    }
    
    /* Inicializar buffer */
    logger->buffer_start = 0;
    logger->buffer_end = 0;
    logger->buffer_count = 0;
    
    /* Inicializar mutex e semáforo */
    if (pthread_mutex_init(&logger->buffer_mutex, NULL) != 0) {
        fclose(logger->log_file);
        free(logger);
        return NULL;
    }
    
    if (sem_init(&logger->write_semaphore, 0, 1) != 0) {
        pthread_mutex_destroy(&logger->buffer_mutex);
        fclose(logger->log_file);
        free(logger);
        return NULL;
    }
    
    /* Configurar rotação */
    logger->rotation_enabled = rotation_enabled;
    logger->max_file_size = LOG_MAX_FILE_SIZE;
    logger->max_backup_files = LOG_MAX_BACKUP_FILES;
    
    /* Iniciar thread de flush */
    logger->flush_running = 1;
    if (pthread_create(&logger->flush_thread, NULL, flush_thread_func, logger) != 0) {
        pthread_mutex_destroy(&logger->buffer_mutex);
        sem_destroy(&logger->write_semaphore);
        fclose(logger->log_file);
        free(logger);
        return NULL;
    }
    
    printf("Logger initialized with file: %s\n", filename);
    return logger;
}

/* Destruir logger */
void logger_destroy(logger_t* logger) {
    if (!logger) return;
    
    /* Parar thread de flush */
    logger->flush_running = 0;
    pthread_join(logger->flush_thread, NULL);
    
    /* Fazer flush final do buffer */
    logger_flush(logger);
    
    /* Fechar arquivo */
    if (logger->log_file) {
        fclose(logger->log_file);
    }
    
    /* Destruir mutex e semáforo */
    pthread_mutex_destroy(&logger->buffer_mutex);
    sem_destroy(&logger->write_semaphore);
    
    free(logger);
    
    printf("Logger destroyed\n");
}

/* Função principal de logging */
void logger_log(logger_t* logger, log_level_t level, 
                const char* remote_addr, const char* user,
                const char* method, const char* uri, 
                const char* protocol, int status, 
                size_t bytes_sent, const char* referer,
                const char* user_agent) {
    
    if (!logger) return;
    
    /* Criar entrada de log */
    log_entry_t entry;
    entry.level = level;
    entry.timestamp = time(NULL);
    
    /* Formatar timestamp */
    char timestamp_str[64];
    get_formatted_time(timestamp_str, sizeof(timestamp_str), entry.timestamp);
    
    /* Formatar mensagem no padrão Apache Combined */
    format_apache_combined(entry.message, sizeof(entry.message),
                          remote_addr, user, timestamp_str,
                          method, uri, protocol, status,
                          bytes_sent, referer, user_agent);
    
    /* Adicionar nível de log no início (para debug) */
    char level_prefix[32];
    const char* level_str;
    switch (level) {
        case LOG_LEVEL_DEBUG:   level_str = "DEBUG"; break;
        case LOG_LEVEL_INFO:    level_str = "INFO"; break;
        case LOG_LEVEL_WARNING: level_str = "WARNING"; break;
        case LOG_LEVEL_ERROR:   level_str = "ERROR"; break;
        case LOG_LEVEL_FATAL:   level_str = "FATAL"; break;
        default:                level_str = "UNKNOWN"; break;
    }
    
    /* Criar mensagem final com timestamp e nível */
    char final_message[LOG_ENTRY_SIZE];
    snprintf(final_message, sizeof(final_message), "[%s] %s", level_str, entry.message);
    strncpy(entry.message, final_message, sizeof(entry.message) - 1);
    
    /* Adicionar ao buffer */
    buffer_add_entry(logger, &entry);
    
    /* Log imediato para erros e fatal */
    if (level >= LOG_LEVEL_ERROR) {
        logger_flush(logger);
    }
}

/* Funções de conveniência */
void logger_debug(logger_t* logger, const char* remote_addr,
                  const char* method, const char* uri, 
                  const char* protocol, int status, 
                  size_t bytes_sent) {
    
    logger_log(logger, LOG_LEVEL_DEBUG, remote_addr, "-", 
               method, uri, protocol, status, bytes_sent, "-", "-");
}

void logger_info(logger_t* logger, const char* remote_addr,
                 const char* method, const char* uri, 
                 const char* protocol, int status, 
                 size_t bytes_sent) {
    
    logger_log(logger, LOG_LEVEL_INFO, remote_addr, "-", 
               method, uri, protocol, status, bytes_sent, "-", "-");
}

void logger_error(logger_t* logger, const char* remote_addr,
                  const char* method, const char* uri, 
                  const char* protocol, int status, 
                  size_t bytes_sent) {
    
    logger_log(logger, LOG_LEVEL_ERROR, remote_addr, "-", 
               method, uri, protocol, status, bytes_sent, "-", "-");
}

/* Fazer flush do buffer */
void logger_flush(logger_t* logger) {
    if (!logger) return;
    
    pthread_mutex_lock(&logger->buffer_mutex);
    
    /* Escrever todas as entradas do buffer */
    while (logger->buffer_count > 0) {
        log_entry_t* entry = &logger->buffer[logger->buffer_start];
        write_log_entry(logger, entry);
        
        logger->buffer_start = (logger->buffer_start + 1) % LOG_BUFFER_SIZE;
        logger->buffer_count--;
    }
    
    logger->buffer_end = logger->buffer_start;
    pthread_mutex_unlock(&logger->buffer_mutex);
}

/* Rotacionar arquivo de log */
int logger_rotate(logger_t* logger) {
    if (!logger || !logger->rotation_enabled) return -1;
    
    sem_wait(&logger->write_semaphore);
    
    /* Fechar arquivo atual */
    if (logger->log_file) {
        fclose(logger->log_file);
        logger->log_file = NULL;
    }
    
    /* Renomear arquivos de backup */
    for (int i = logger->max_backup_files - 1; i >= 0; i--) {
        char old_name[256];
        char new_name[256];
        
        if (i == 0) {
            snprintf(old_name, sizeof(old_name), "%s", logger->log_filename);
        } else {
            snprintf(old_name, sizeof(old_name), "%s.%d", logger->log_filename, i);
        }
        
        snprintf(new_name, sizeof(new_name), "%s.%d", logger->log_filename, i + 1);
        
        /* Verificar se o arquivo antigo existe */
        if (access(old_name, F_OK) == 0) {
            /* Remover arquivo mais antigo se exceder o máximo */
            if (i + 1 > logger->max_backup_files) {
                remove(old_name);
            } else {
                rename(old_name, new_name);
            }
        }
    }
    
    /* Abrir novo arquivo de log */
    logger->log_file = fopen(logger->log_filename, "a");
    if (!logger->log_file) {
        perror("Failed to open new log file after rotation");
        sem_post(&logger->write_semaphore);
        return -1;
    }
    
    printf("Log file rotated: %s\n", logger->log_filename);
    sem_post(&logger->write_semaphore);
    
    return 0;
}

/* Configurar tamanho máximo do arquivo */
void logger_set_max_size(logger_t* logger, size_t max_size) {
    if (logger) {
        logger->max_file_size = max_size;
    }
}

/* Configurar número máximo de backups */
void logger_set_max_backups(logger_t* logger, int max_backups) {
    if (logger) {
        logger->max_backup_files = max_backups;
    }
}

/* Obter contagem do buffer */
size_t logger_get_buffer_count(logger_t* logger) {
    if (!logger) return 0;
    
    pthread_mutex_lock(&logger->buffer_mutex);
    size_t count = logger->buffer_count;
    pthread_mutex_unlock(&logger->buffer_mutex);
    
    return count;
}

/* Obter tamanho do arquivo */
size_t logger_get_file_size(logger_t* logger) {
    if (!logger || !logger->log_file) return 0;
    
    sem_wait(&logger->write_semaphore);
    fseek(logger->log_file, 0, SEEK_END);
    long size = ftell(logger->log_file);
    sem_post(&logger->write_semaphore);
    
    return size >= 0 ? (size_t)size : 0;
    // Release log semaphore
    sem_post(sems->log_mutex);
#include <semaphore.h>
#include <unistd.h>



// Regista o pedido no ficheiro 'access.log', protegendo a escrita com 'log_mutex'.
// Formato: IP - - [Timestamp] "METHOD PATH PROTOCOL" STATUS BYTES
void log_request(const char *remote_ip, const http_request_t *req, 
                 int status_code, size_t bytes_sent, sem_t *log_mutex) {
    
    // 1. Aquisição do Semáforo (Lock): Garante que apenas um processo escreve no log.
    if (sem_wait(log_mutex) == -1) {
        perror("ERRO: sem_wait log_mutex");
        return; 
    }

    // SECÇÃO CRÍTICA: Escrita no Ficheiro de Log
    
    // Obter o timestamp no formato Apache (DD/Mon/YYYY:HH:MM:SS +0000)
    char timestamp[32];
    time_t now = time(NULL);
    struct tm *t = gmtime(&now); // Usa GMT/UTC (+0000)
    strftime(timestamp, sizeof(timestamp), "%d/%b/%Y:%H:%M:%S +0000", t);

    // Abrir o ficheiro de log em modo de adição ("a")
    FILE *log_file = fopen(LOG_FILENAME, "a");
    if (!log_file) {
        perror("ERRO: Falha ao abrir ficheiro de log");
        sem_post(log_mutex); // Libertar o semáforo antes de sair
        return;
    }

    // Escrever o registo
    fprintf(log_file, "%s - - [%s] \"%s %s %s\" %d %zu\n",
            remote_ip,                     // Host/IP
            timestamp,                     // Timestamp
            req->method,                   // Método
            req->path,                     // Caminho/URI
            req->version,         // Protocolo
            status_code,                   // Status Code
            bytes_sent);                   // Bytes Enviados

    fflush(log_file); // Força a escrita no disco
    fclose(log_file); // Fecha o ficheiro
    
    // FIM da SECÇÃO CRÍTICA

    // 2. Libertação do Semáforo (Unlock)
    if (sem_post(log_mutex) == -1) {
        perror("ERRO: sem_post log_mutex");
    }
}