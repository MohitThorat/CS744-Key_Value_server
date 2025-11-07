# C++ Key-Value Server

This is a high-performance, multi-threaded C++ Key-Value server designed for a cloud systems course (CS744).

It features a high-concurrency, sharded LRU cache for fast read access and an asynchronous database backend (using a thread pool and MySQL) for persistent storage. The web layer is handled by civetweb.

This repository is organized into two main parts:

`/Server:` The C++ server application.

`/Client:` A C++ load generator for benchmarking.

# Prerequisites

Before building, you must install the required development libraries.

On Ubuntu/Debian, you can install them with:
```
sudo apt update
sudo apt install mysql-server -y
```

Configure an account and enter the details in `Server\src\server.cpp`
# Server Usage

## 1. Build the Server

Navigate to the Server/ directory and use make.
```
cd Server/
make clean
make
```

This will compile all source files and create the final executable at build/server.

## 2. Run the Server

From the Server/ directory, simply run the executable:
```
./build/server
```

The server will start and listen on `http://127.0.0.1:8888`.

# Client (Load Generator) Usage

## 1. Build the Client

Navigate to the Client/ directory and use make:
```
cd Client/
make clean
make
```

This will create the executable load_gen.

## 2. Run the Client

The client takes three arguments: <threads>, <duration_secs>, and <workload>.

Usage:
```
./load_gen <threads> <duration_secs> <workload>
```

## Available Workloads:

### get-popular: 100% read (high cache-hit ratio)

### get-all: 100% read (high cache-miss ratio)

### put-all: 50% POST (write), 50% DELETE

### get-put: A mixed workload of 80% read, 15% write, 5% delete

Example:

To run a 30-second test with 100 threads using the get-popular workload:
```
./load_gen 100 30 get-popular
```