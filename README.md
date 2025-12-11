# Concurrent HTTP Server

**Authors:**
*   Inês Batista, 124877
*   Maria Quinteiro, 124996

## Introduction
The **Concurrent HTTP Server** is a high-performance, multi-process, and multi-threaded web server developed for the Operating Systems course. It is designed to handle thousands of concurrent connections efficiently by leveraging advanced systems programming concepts.

The server implements a **Hybrid Architecture**, combining the stability of multi-processing with the efficiency of multi-threading. It uses **UNIX Domain Sockets** for fast Inter-Process Communication (IPC) and a custom **Producer-Consumer** model to distribute load.

Full documentation:
*   [Design Document](./docs/design.pdf)
*   [Technical Report](./docs/report.pdf)
*   [User Manual](./docs/user_manual.pdf)

---

## System Architecture

### Master-Worker Model
The system follows a strict **Master-Worker** pattern:
1.  **Master Process:** Responsible for initialization, parsing configuration, and accepting incoming connections. It acts as the **Producer**.
2.  **Worker Processes:** A pool of pre-forked processes that handle the actual HTTP request processing. They act as **Consumers**.

### Inter-Process Communication (IPC)
Instead of a traditional shared memory queue for connections, the server uses **UNIX Domain Sockets** with the `SCM_RIGHTS` mechanism.
*   The Master accepts a connection and sends the file descriptor (FD) to a Worker via a dedicated socket pair.
*   This allows for **Zero-Copy** handoff and utilizes the kernel's internal buffering for synchronization.

### Thread Pool
Each Worker process maintains its own **Thread Pool**.
*   A main thread receives FDs from the Master and pushes them to a local queue.
*   Worker threads pop FDs from the queue and process the HTTP requests.
*   This design ensures that a single blocking operation (like disk I/O) does not stall the entire worker.

---

## Features

### Core Features
*   **Concurrent Handling:** Supports thousands of simultaneous clients.
*   **Static File Serving:** Serves HTML, CSS, JS, Images, etc.
*   **Thread-Safe Logging:** Asynchronous logging to `access.log` using a ring buffer and flush thread.
*   **LRU File Cache:** In-memory cache with **Reader-Writer Locks** to speed up access to frequently requested files.
*   **Global Statistics:** Real-time metrics stored in Shared Memory.

### Bonus Features
1.  **HTTP Keep-Alive:** Supports persistent connections, allowing multiple requests over a single TCP connection.
2.  **Range Requests:** Supports the `Range` header for partial content delivery (e.g., video streaming, resumable downloads).
3.  **Virtual Host Support:** Serves different content based on the `Host` header (e.g., `site1.com` vs `site2.com`).
4.  **Real-Time Dashboard:** A `/stats` endpoint provides JSON metrics for a live web dashboard.

---

## Synchronization Mechanisms

The server employs a tiered synchronization strategy to ensure data integrity without sacrificing performance:

| Resource | Mechanism | Description |
| :--- | :--- | :--- |
| **Global Statistics** | **POSIX Named Semaphores** | Ensures atomic updates to shared memory counters across processes. |
| **File Cache** | **Reader-Writer Locks** (`pthread_rwlock_t`) | Allows multiple concurrent readers but exclusive access for writers (eviction/insertion). |
| **Thread Pool** | **Mutexes & Condition Variables** | Protects the local request queue. Uses `pthread_cond_signal` to avoid "thundering herd". |
| **Logging** | **Mutexes** | Protects the log ring buffer during writes. |

---

## Compilation

### Prerequisites
*   Linux Environment
*   GCC Compiler
*   Make

### Build Commands
```bash
make          # Build the server
make clean    # Remove artifacts
make run      # Build and run
make debug    # Build with debug symbols
make release  # Build with optimizations (-O3)
```

---

## Configuration

The server is configured via `server.conf` or Environment Variables.

| Parameter | Env Variable | Default | Description |
| :--- | :--- | :--- | :--- |
| `PORT` | `HTTP_PORT` | `8080` | Listening Port |
| `NUM_WORKERS` | `HTTP_WORKERS` | `4` | Number of Worker Processes |
| `THREADS_PER_WORKER` | `HTTP_THREADS` | `10` | Threads per Worker |
| `DOCUMENT_ROOT` | `HTTP_ROOT` | `./www` | Root directory for files |
| `CACHE_SIZE_MB` | `HTTP_CACHE_SIZE` | `10` | Cache size limit (MB) |
| `LOG_FILE` | `HTTP_LOG_FILE` | `access.log` | Log file path |

**Example `server.conf`:**
```ini
PORT=8080
NUM_WORKERS=4
THREADS_PER_WORKER=10
DOCUMENT_ROOT=./www
CACHE_SIZE_MB=10
```

---

## Usage

### Starting the Server
```bash
./server                    # Default config
./server -c my.conf         # Custom config file
./server -p 9090            # Override port
./server -d                 # Run as Daemon (background)
./server -v                 # Verbose logging
```

### Monitoring
*   **Dashboard:** Open `http://localhost:8080/dashboard.html` to see real-time stats.
*   **Logs:** Watch traffic with `tail -f access.log`.

---

## Testing

The project includes a comprehensive test suite.

```bash
make test       # Run functional tests
```

### Performance & Stress Testing
```bash
# Basic Load Test (Apache Benchmark)
ab -n 1000 -c 100 http://localhost:8080/index.html

# Check for Memory Leaks
make valgrind

# Check for Race Conditions
make helgrind
```
