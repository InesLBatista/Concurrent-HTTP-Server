#ifndef LOGGER_H
#define LOGGER_H

#include "semaphores.h"

void log_request(semaphores_t* sems, const char* client_ip, 
                 const char* method, const char* path, 
                 int status, size_t bytes);

#endif