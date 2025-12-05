// Inês Batista, 124877
// Maria Quinteiro, 124996

#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>

void log_request(semaphores_t* sems, const char* client_ip, 
                 const char* method, const char* path, 
                 int status, size_t bytes) {
    time_t now = time(NULL);
    struct tm* tm_info = localtime(&now);
    
    char timestamp[64];
    strftime(timestamp, sizeof(timestamp), "%d/%b/%Y:%H:%M:%S %z", tm_info);
    
    // Acquire log semaphore
    sem_wait(sems->log_mutex);
    
    // Open log file
    FILE* log = fopen("access.log", "a");
    if (log) {
        fprintf(log, "%s - - [%s] \"%s %s HTTP/1.1\" %d %zu\n",
                client_ip, timestamp, method, path, status, bytes);
        fclose(log);
        
        // Check log rotation (simplified)
        struct stat st;
        if (stat("access.log", &st) == 0 && st.st_size > 10 * 1024 * 1024) {
            rename("access.log", "access.log.old");
        }
    }
    
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