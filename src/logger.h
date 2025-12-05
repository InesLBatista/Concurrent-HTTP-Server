#ifndef LOGGER_H
#define LOGGER_H

#include "semaphores.h"

void log_request(semaphores_t* sems, const char* client_ip, 
                 const char* method, const char* path, 
                 int status, size_t bytes);

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
