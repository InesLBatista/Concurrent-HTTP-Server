#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "master.h"
#include "config.h"
#include "shared_mem.h"

server_config_t config;

void print_usage(const char *prog_name) {
    printf("Usage: %s [OPTIONS]\n", prog_name);
    printf("Options:\n");
    printf("  -c, --config PATH    Configuration file path (default: ./server.conf)\n");
    printf("  -p, --port PORT      Port to listen on (default: 8080)\n");
    printf("  -w, --workers NUM    Number of worker processes (default: 4)\n");
    printf("  -t, --threads NUM    Threads per worker (default: 10)\n");
    printf("  -d, --daemon         Run in background\n");
    printf("  -v, --verbose        Enable verbose logging\n");
    printf("  -h, --help           Show this help message\n");
    printf("  --version            Show version information\n");
}

void daemonize() {
    pid_t pid;

    pid = fork();
    if (pid < 0) exit(EXIT_FAILURE);
    if (pid > 0) exit(EXIT_SUCCESS);

    if (setsid() < 0) exit(EXIT_FAILURE);

    pid = fork();
    if (pid < 0) exit(EXIT_FAILURE);
    if (pid > 0) exit(EXIT_SUCCESS);

    umask(0);
    if (chdir("/") < 0) {
        perror("chdir");
        exit(EXIT_FAILURE);
    }

    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

    open("/dev/null", O_RDONLY);
    open("/dev/null", O_RDWR);
    open("/dev/null", O_RDWR);
}

int main(int argc, char *argv[]) {
    int opt;
    int daemon_mode = 0;
    char config_file[256] = "server.conf";

    // Default values
    config.port = 8080;
    config.num_workers = 4;
    config.threads_per_worker = 10;
    config.max_queue_size = 100;
    config.cache_size_mb = 10;
    config.timeout_seconds = 30;
    config.keep_alive_timeout = 5;
    strncpy(config.document_root, "./www", sizeof(config.document_root));
    strncpy(config.log_file, "access.log", sizeof(config.log_file));

    static struct option long_options[] = {
        {"config", required_argument, 0, 'c'},
        {"port", required_argument, 0, 'p'},
        {"workers", required_argument, 0, 'w'},
        {"threads", required_argument, 0, 't'},
        {"daemon", no_argument, 0, 'd'},
        {"verbose", no_argument, 0, 'v'},
        {"help", no_argument, 0, 'h'},
        {"version", no_argument, 0, 0},
        {0, 0, 0, 0}
    };

    // Pre-scan for config file
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-c") == 0 || strcmp(argv[i], "--config") == 0) {
            if (i + 1 < argc) {
                strncpy(config_file, argv[i+1], sizeof(config_file) - 1);
                config_file[sizeof(config_file) - 1] = '\0';
            }
        }
    }

    // Load config file
    load_config(config_file, &config);

    // Override with environment variables
    parse_env_vars(&config);

    // Override with command line arguments
    optind = 1; // Reset getopt
    while ((opt = getopt_long(argc, argv, "c:p:w:t:dvh", long_options, NULL)) != -1) {
        switch (opt) {
            case 'c':
                // Already handled
                break;
            case 'p':
                config.port = atoi(optarg);
                break;
            case 'w':
                config.num_workers = atoi(optarg);
                break;
            case 't':
                config.threads_per_worker = atoi(optarg);
                break;
            case 'd':
                daemon_mode = 1;
                break;
            case 'v':
                // Verbose logging (implementation dependent, maybe set a flag in config)
                break;
            case 'h':
                print_usage(argv[0]);
                return 0;
            case 0:
                if (strcmp(argv[optind-1], "--version") == 0) {
                    printf("Concurrent HTTP Server v1.0\n");
                    return 0;
                }
                break;
            default:
                print_usage(argv[0]);
                return 1;
        }
    }

    if (daemon_mode) {
        daemonize();
    }

    init_shared_stats();

    return start_master_server();
}