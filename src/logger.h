// logger.h
// Inês Batista, Maria Quinteiro

// Define função para logging thread-safe de pedidos HTTP no formato
// Apache Combined Log Format. Inclui timestamp, IP do cliente,
// método HTTP, path, status code e bytes transferidos.

#ifndef LOGGER_H
#define LOGGER_H

#include <semaphore.h>

void log_request(sem_t* log_sem, const char* client_ip, const char* method,
                const char* path, int status, size_t bytes);

#endif