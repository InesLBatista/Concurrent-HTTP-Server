# Multi-Threaded Web Server with IPC and Semaphores

## Authors
Inês Batista, 124877<br>
Maria Quinteiro, 124996

## Description
Concurrent HTTP Server is a high-performance, multi-process, multi-threaded HTTP/1.1 web server developed for the Operating Systems course. This project demonstrates advanced systems programming concepts by implementing a production-grade web server capable of handling thousands of concurrent connections efficiently.

The server follows a master-worker architecture with shared memory inter-process communication and comprehensive synchronization mechanisms to ensure thread safety and optimal resource utilization under heavy load.

The full design document is available here: [design.pdf](./docs/design.pdf)

## Compilation
### Quick Start
```bash
# Build the server
make
# Clean build artifacts
make clean
# Rebuild from scratch
make clean && make
# Build and run
make run
```
### Build Targets
```bash
make all # Build server executable (default)
make clean # Remove object files and executable
make run # Build and start server
make test # Build and run test suite
make debug # Build with debug symbols (-g)
make release # Build with optimizations (-O3)
make valgrind # Build and run under Valgrind
make helgrind # Build and run under Helgrind
```
### Manual Compilation
```bash
# Compile all source files
gcc -Wall -Wextra -pthread -lrt -o server \
src/main.c \
src/master.c \
src/worker.c \
src/http.c \
src/thread_pool.c \
src/cache.c \
src/logger.c \
src/stats.c \
src/config.c
# Run the server
./server
```
### Compiler Flags Explained
- `-Wall -Wextra`: Enable all warnings
- `-pthread`: Link pthread library
- `-lrt`: Link POSIX real-time extensions (for semaphores)
- `-O3`: Optimization level 3 (for release builds)
- `-g`: Debug symbols (for debugging)
- `-fsanitize=thread`: Thread sanitizer (for race detection)

## Configuration
### Configuration File (server.conf)
The server reads configuration from `server.conf` in the current
directory:
```ini
# Server Configuration File
# Network settings
PORT=8080 # Port to listen on
TIMEOUT_SECONDS=30 # Connection timeout
# File system
DOCUMENT_ROOT=/var/www/html # Root directory for serving files
# Process architecture
NUM_WORKERS=4 # Number of worker processes
THREADS_PER_WORKER=10 # Threads per worker
# Queue management
MAX_QUEUE_SIZE=100 # Connection queue size
# Caching
CACHE_SIZE_MB=10 # Cache size per worker (MB)
# Logging
LOG_FILE=access.log # Access log file path
LOG_LEVEL=INFO # Log level: DEBUG, INFO, WARN, ERROR
```
### Configuration Parameters
| Parameter | Default | Description |
|--------------------|------------|----------------------------------|
| PORT | 8080 | TCP port for HTTP server |
| DOCUMENT_ROOT | ./www | Root directory for serving files |
| NUM_WORKERS | 4 | Number of worker processes |
| THREADS_PER_WORKER | 10 | Thread pool size per worker |
| MAX_QUEUE_SIZE | 100 | Connection queue capacity |
| CACHE_SIZE_MB | 10 | Maximum cache size per worker |
| LOG_FILE | access.log | Path to access log |
| TIMEOUT_SECONDS | 30 | Connection timeout |
### Environment Variables
```bash
# Override configuration via environment
export HTTP_PORT=8080
export HTTP_WORKERS=4
export HTTP_THREADS=10
./server
```

## Usage
### Starting the Server
```bash
# Start with default configuration
./server
# Start with custom configuration file
./server -c /path/to/server.conf
# Start on specific port
./server -p 9090
# Start with verbose logging
./server -v
# Start in background (daemon mode)
./server -d
```
### Command-Line Options
```
Usage: ./server [OPTIONS]
Options:
-c, --config PATH Configuration file path (default: ./server.conf)
-p, --port PORT Port to listen on (default: 8080)
-w, --workers NUM Number of worker processes (default: 4)
-t, --threads NUM Threads per worker (default: 10)
-d, --daemon Run in background
-v, --verbose Enable verbose logging
-h, --help Show this help message
--version Show version information
```
### Accessing the Server
```bash
# Open in browser
firefox http://localhost:8080
# Using curl
curl http://localhost:8080/index.html
# View headers only (HEAD request)
curl -I http://localhost:8080/index.html
# Download file
wget http://localhost:8080/document.pdf
```
### Stopping the Server
```bash
# Graceful shutdown (from server terminal)
Ctrl+C
# Send SIGTERM
kill $(pgrep -f "./server")
# Force kill (not recommended)
kill -9 $(pgrep -f "./server")
```
### Viewing Logs
```bash
# Follow access log in real-time
tail -f access.log
# View last 100 entries
tail -n 100 access.log
# Search for errors
grep "500\|404" access.log
# Count requests by status code
awk '{print $9}' access.log | sort | uniq -c
```
### Monitoring Statistics
Statistics are displayed every 30 seconds on stdout:
```
========================================
SERVER STATISTICS
========================================
Uptime: 120 seconds
Total Requests: 1,542
Successful (2xx): 1,425
Client Errors (4xx): 112
Server Errors (5xx): 5
Bytes Transferred: 15,728,640
Average Response Time: 8.3 ms
Active Connections: 12
Cache Hit Rate: 82.4%
========================================
```

## Testing
### Functional Tests
```bash
# Run all tests
make test
# Basic functionality test
curl http://localhost:8080/index.html
# Test HEAD method
curl -I http://localhost:8080/index.html
# Test 404 error
curl http://localhost:8080/nonexistent.html
# Test different file types
curl http://localhost:8080/style.css # CSS
curl http://localhost:8080/script.js # JavaScript
curl http://localhost:8080/image.png # Image
```
### Load Testing
```bash
# Basic load test (1000 requests, 10 concurrent)
ab -n 1000 -c 10 http://localhost:8080/index.html
# High concurrency test (10000 requests, 100 concurrent)
ab -n 10000 -c 100 http://localhost:8080/index.html
# Sustained load test (5 minutes)
ab -t 300 -c 50 http://localhost:8080/
# Test multiple files
for file in index.html style.css script.js; do
ab -n 1000 -c 50 http://localhost:8080/$file
done
```
### Concurrency Testing
```bash
# Parallel requests with curl
for i in {1..100}; do
curl -s http://localhost:8080/index.html &
done
wait
# Parallel requests with different files
for i in {1..50}; do
curl -s http://localhost:8080/page$((i % 10)).html &
done
wait
```
### Memory Leak Detection
```bash
# Run server under Valgrind
make valgrind
# Or manually:
valgrind --leak-check=full \
--show-leak-kinds=all \
--track-origins=yes \
--log-file=valgrind.log \
./server
# In another terminal, generate traffic
ab -n 1000 -c 50 http://localhost:8080/
# Stop server and check valgrind.log
```
### Race Condition Detection
```bash
# Run server under Helgrind
make helgrind
# Or manually:
valgrind --tool=helgrind \
--log-file=helgrind.log \
./server
# Generate concurrent traffic
ab -n 5000 -c 100 http://localhost:8080/
# Check helgrind.log for data races
```
### Performance Testing
```bash
# Measure cache effectiveness
# First request (cache miss)
time curl -s http://localhost:8080/large.html > /dev/null
# Subsequent requests (cache hit)
for i in {1..10}; do
time curl -s http://localhost:8080/large.html > /dev/null
done
# Monitor server resource usage
top -p $(pgrep -f "./server")
# Monitor worker processes
watch -n 1 'pgrep -P $(pgrep -f "./server") | xargs ps -o
pid,ppid,nlwp,cmd'
```
