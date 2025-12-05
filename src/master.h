#ifndef MASTER_H
#define MASTER_H

#include "shared_mem.h"
#include "semaphores.h"
#include "config.h"

void master_main(shared_data_t* data, semaphores_t* sems, server_config_t* config);

#endif