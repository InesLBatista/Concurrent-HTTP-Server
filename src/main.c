
#include <stdio.h>
#include "master.h"
#include "config.h"
#include "shared_mem.h"

server_config_t config; 

int main(void)
{

    if (load_config("server.conf", &config) != 0) {
        fprintf(stderr, "Failed to load configuration.\n");
        return 1;
    }

    init_shared_stats();

    return start_master_server();
}