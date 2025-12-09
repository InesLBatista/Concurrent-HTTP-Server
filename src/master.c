#define _XOPEN_SOURCE 700

#include "master.h"    
#include "shared_mem.h"
#include "config.h"
#include <sys/uio.h>
#include <string.h>
#include "worker.h"  
#include "stats.h"
#include "thread_pool.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <pthread.h> 
#include <signal.h>
#include <errno.h>

/* Access global configuration loaded in main.c */
extern server_config_t config;

/*
 * Global Control Flag
 * Purpose: Controls the main accept loop.
 * Type: volatile sig_atomic_t ensures atomic access during signal handling.
 */
static volatile sig_atomic_t server_running = 1;

/*
 * Signal Handler for SIGINT (Ctrl+C)
 * Purpose: Catches the interrupt signal and sets the flag to stop the 
 * main loop. This allows the server to break the accept() blocking call 
 * and proceed to the cleanup phase.
 */
void handle_sigint(int sig) {
    (void)sig; /* Mark unused parameter */
    server_running = 0; 
}

/*
 * Send a File Descriptor via UNIX Domain Socket
 * Purpose: Transmits an open file descriptor from the current process to another
 * process. This is required because file descriptors are process-local integers;
 * simply passing the integer value (e.g., '5') to another process is invalid.
 * We must use SCM_RIGHTS (Socket Control Message) to instruct the kernel to
 * duplicate the FD into the receiving process's file table.
 *
 * Parameters:
 * - socket: The UNIX domain socket (IPC channel) to send through.
 * - fd_to_send: The file descriptor to transfer.
 *
 * Return:
 * - Number of bytes sent on success (usually 1).
 * - -1 on failure.
 */
static int send_fd(int socket, int fd_to_send)
{
    struct msghdr msg = {0};

    /* Dummy data buffer. sendmsg requires at least one byte of real data
     * to be sent along with the ancillary (control) data.
     */
    char buf[1] = {0}; 
    struct iovec io = {.iov_base = buf, .iov_len = 1};

    /* Union ensures proper memory alignment for the control message buffer */
    union
    {
        char buf[CMSG_SPACE(sizeof(int))]; /* Buffer big enough for one int FD */
        struct cmsghdr align;              /* Enforce alignment */
    } u;
    
    /* Zero out the control buffer to prevent garbage data */
    memset(&u, 0, sizeof(u)); 

    /* Setup message header */
    msg.msg_iov = &io;          /* Point to dummy data */
    msg.msg_iovlen = 1;         /* Number of I/O vectors */
    msg.msg_control = u.buf;    /* Point to control buffer */
    msg.msg_controllen = sizeof(u.buf); /* Size of control buffer */

    /* Configure the Ancillary Data (Control Message) Header */
    struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
    cmsg->cmsg_level = SOL_SOCKET;  /* Socket-level protocol */
    cmsg->cmsg_type = SCM_RIGHTS;   /* We are sending access rights (FDs) */
    cmsg->cmsg_len = CMSG_LEN(sizeof(int)); /* Length of data + header */

    /* Copy the file descriptor into the data portion of the control message */
    *((int *)CMSG_DATA(cmsg)) = fd_to_send;

    return sendmsg(socket, &msg, 0);
}

/*
 * Start Master Server Logic
 * Purpose: Initializes the server socket, spawns worker processes, and 
 * enters the main loop to accept and distribute connections.
 *
 * Return:
 * - 0 on clean shutdown.
 * - Non-zero on fatal errors (e.g., socket failure).
 */
int start_master_server()
{
    /* 1. Setup Signal Handling */
    struct sigaction sa;
    sa.sa_handler = handle_sigint;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0; /* No SA_RESTART: we want accept() to be interrupted */
    sigaction(SIGINT, &sa, NULL);

    /* 2. Create Server Socket */
    int server_socket = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    /* Allow immediate reuse of the port after server restart */
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY; /* Listen on all interfaces */
    address.sin_port = htons(config.port); 

    if (bind(server_socket, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed");
        return 1;
    }
    
    /* Listen with a backlog of 128 pending connections */
    listen(server_socket, 128);

    printf("Master (PID: %d) listening on port %d.\n", getpid(), config.port);

    /* 3. Start Statistics Monitor Thread
     * This runs in the background to print server metrics periodically.
     */
    pthread_t stats_tid;
    pthread_create(&stats_tid, NULL, stats_monitor_thread, NULL);

    /* 4. Fork Worker Processes */
    int *worker_pipes = malloc(sizeof(int) * config.num_workers);
    for (int i = 0; i < config.num_workers; i++)
    {
        /* Create a UNIX domain socket pair for passing File Descriptors */
        int sv[2]; 
        if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) < 0) {
            perror("socketpair");
            exit(1);
        }

        pid_t pid = fork();
        if (pid == 0) {
            /* === CHILD PROCESS (WORKER) === */
            close(server_socket); /* Child does not accept connections */
            close(sv[0]);         /* Close Master's end of the pipe */
            
            /* Ignore SIGINT: Workers wait for pipe EOF to shutdown gracefully.
             * This prevents workers from dying mid-request when Ctrl+C is pressed.
             */
            signal(SIGINT, SIG_IGN); 
            
            start_worker_process(sv[1]); /* Enter Worker Logic */
            exit(0);
        }
        
        /* === PARENT PROCESS (MASTER) === */
        close(sv[1]); /* Close Worker's end */
        worker_pipes[i] = sv[0]; /* Store Master's end */
    }

    /* 5. Main Loop: Accept and Distribute */
    int current_worker = 0;
    
    while (server_running) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        /* Blocking call - waits for a client */
        int client_fd = accept(server_socket, (struct sockaddr *)&client_addr, &client_len);
        
        /* Check if accept failed due to signal interruption (Ctrl+C) */
        if (client_fd < 0) {
            if (errno == EINTR) continue; /* Loop back to check server_running */
            perror("accept");
            continue;
        }

        /* * Distribute connection to a worker via IPC (Round-Robin).
         * We send the File Descriptor itself using SCM_RIGHTS.
         */
        send_fd(worker_pipes[current_worker], client_fd);
        
        /* * CRITICAL: Master must close the FD.
         * The worker now has a copy. If Master doesn't close it, the socket
         * will remain open until the Master process exits.
         */
        close(client_fd);
        current_worker = (current_worker + 1) % config.num_workers;
    }

    /* 6. Shutdown Sequence */
    printf("\nShutting down server...\n");

    /* Close pipes to signal EOF to workers */
    for (int i = 0; i < config.num_workers; i++) {
        close(worker_pipes[i]);
    }

    /* Wait for all workers to finish their cleanup */
    while (wait(NULL) > 0);

    /* * Cancel and join the stats thread to ensure no memory is lost.
     * Use pthread_cancel because the thread is sleeping (sleep(30)).
     */
    pthread_cancel(stats_tid);
    pthread_join(stats_tid, NULL);

    /* Final cleanup */
    free(worker_pipes);
    close(server_socket);

    printf("Server stopped cleanly.\n");
    return 0;
}