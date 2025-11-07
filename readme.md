C++ Key-Value Server

This repository contains a high-performance, multi-threaded C++ Key-Value server. The server uses an efficient sharded LRU cache for fast reads and a MySQL database for persistence.

The project is split into two main parts:

    /Server: The C++ server code.

    /Client: A C++ load generator for benchmarking.


Server

Instructions for building and running the K-V server.

1. Build the Server

Navigate to the Server/ directory and use make:
Bash

cd Server/
make clean
make

This will create the executable at build/server.

2. Run the Server

From the Server/ directory, run the executable:
Bash

./build/server

The server will start and listen on port 8888.

Client (Load Generator)

Instructions for building and running the load generator.

1. Build the Client

Navigate to the Client/ directory and use make:
Bash

cd ../Client/
make

This will create the executable at load_gen.

2. Run the Client

The client takes three arguments: threads, duration, and workload.

Usage:
Bash

./load_gen <threads> <duration_secs> <workload>

Available Workloads:

    get-popular: 90% read (cache hit), 10% write

    get-all: 100% read (cache miss)

    put-all: 50% write, 50% delete

Example (Run for 30 seconds with 100 threads):
Bash

./load_gen 100 30 get-popular