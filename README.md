# Mini Redis

A multithreaded Redis-like in-memory key-value store built from scratch in C++.

This project implements a TCP-based client-server architecture with support for key-value operations, concurrent clients, key expiration, and disk persistence. It was built to understand the core systems concepts behind an in-memory database and networked server.

## Features

* TCP client-server communication using POSIX sockets
* Multithreaded client handling using C++ threads
* Thread-safe shared key-value storage using mutexes
* `SET`, `GET`, `DEL`, and `EXISTS` commands
* `PING` health-check command
* Support for multi-word values
* Key expiration using `EXPIRE`
* Lazy expiration of expired keys
* Persistent storage using `SAVE`
* Automatic loading of persisted data on startup
* TTL preservation across server restarts
* Newline-delimited command protocol
* Error handling for invalid and incomplete commands
* Performance benchmarking for sequential and concurrent clients

## Architecture

The server follows a TCP client-server architecture.

```text
                    ┌─────────────────────┐
                    │       Client        │
                    │   nc / Benchmark    │
                    └──────────┬──────────┘
                               │
                               │ TCP
                               ▼
                    ┌─────────────────────┐
                    │       Server        │
                    │      Port 6379      │
                    └──────────┬──────────┘
                               │
                 ┌─────────────┴─────────────┐
                 │                           │
                 ▼                           ▼
        ┌─────────────────┐        ┌─────────────────┐
        │  Client Thread  │  ...   │  Client Thread  │
        │        1        │        │        N        │
        └────────┬────────┘        └────────┬────────┘
                 │                           │
                 └─────────────┬─────────────┘
                               │
                               ▼
                    ┌─────────────────────┐
                    │   Shared Database   │
                    │ unordered_map<K,V>  │
                    └──────────┬──────────┘
                               │
                         mutex protection
                               │
                               ▼
                    ┌─────────────────────┐
                    │   Expiry Tracking   │
                    │   TTL information   │
                    └─────────────────────┘
```

### Request Flow

1. The server creates a TCP socket and listens on port `6379`.
2. When a client connects, the server accepts the connection.
3. A separate thread is created to handle that client.
4. The client sends newline-delimited commands such as `SET`, `GET`, or `PING`.
5. The server parses each command and performs the requested operation.
6. Shared database access is protected using a mutex.
7. Expiring keys are checked lazily during operations such as `GET` and `EXISTS`.
8. The `SAVE` command writes the database and remaining TTL information to disk.
9. The server loads persisted data from disk when it starts.

## Supported Commands

| Command  | Syntax               | Description                               |
| -------- | -------------------- | ----------------------------------------- |
| `PING`   | `PING`               | Checks whether the server is responsive   |
| `SET`    | `SET key value`      | Stores a value for a key                  |
| `GET`    | `GET key`            | Retrieves the value associated with a key |
| `DEL`    | `DEL key`            | Deletes a key                             |
| `EXISTS` | `EXISTS key`         | Checks whether a key exists               |
| `EXPIRE` | `EXPIRE key seconds` | Sets a TTL for a key                      |
| `SAVE`   | `SAVE`               | Persists the current database to disk     |

### Multi-word Values

Values can contain spaces.

```text
SET name Avisha Sharma
GET name
```

Response:

```text
Avisha Sharma
```

The first token is interpreted as the command, the second as the key, and everything after the key is treated as the value.

## Building & Running

### Requirements

* C++17 or later
* macOS/Linux
* POSIX socket support

### Build

From the project root:

```bash
clang++ -std=c++17 src/main.cpp -o server
```

### Start the Server

```bash
./server
```

The server listens on:

```text
127.0.0.1:6379
```

### Connect Using Netcat

Open another terminal:

```bash
nc localhost 6379
```

You can then send commands:

```text
PING
SET name Avisha Sharma
GET name
EXISTS name
EXPIRE name 30
GET name
DEL name
SAVE
```

Stop the server with:

```text
Ctrl+C
```

## TTL and Expiration

Keys can be assigned a time-to-live using the `EXPIRE` command.

Example:

```text
SET name Avisha Sharma
EXPIRE name 30
```

The key will become unavailable after approximately 30 seconds.

Expiration is implemented using `std::chrono::steady_clock`.

The server uses **lazy expiration**: expired keys are removed when operations such as `GET` or `EXISTS` access them, rather than using a background expiration thread.

For example:

```text
GET name
```

returns:

```text
(nil)
```

after the key has expired.

## Persistence

The server supports persistence through the `SAVE` command.

Example:

```text
SET name Avisha Sharma
EXPIRE name 60
SAVE
```

The database is written to `database.txt`.

The persistence format stores:

```text
key|value|remaining_seconds
```

For example:

```text
name|Avisha Sharma|53
city|Mumbai|0
```

A value of `0` indicates that the key has no expiration.

When the server starts, it automatically loads the persisted database from `database.txt`.

Remaining TTL values are reconstructed relative to the new server start time, allowing expiration to continue across server restarts.

## Concurrency

Each connected client is handled by a separate C++ thread.

```text
Client 1 ──→ Thread 1 ──┐
Client 2 ──→ Thread 2 ──┤
Client 3 ──→ Thread 3 ──┼──→ Shared Database
   ...                   │
Client N ──→ Thread N ──┘
```

Since multiple threads access the same database, a `std::mutex` is used to protect shared state.

Database operations acquire the mutex before reading or modifying the shared `unordered_map`.

This allows multiple clients to interact with the same key-value store safely.

## Protocol Framing

TCP provides a continuous byte stream rather than separate messages.

To define command boundaries, the server uses a newline-delimited protocol.

For example:

```text
SET name Avisha Sharma\n
GET name\n
PING\n
```

The server maintains a per-client input buffer and processes complete commands whenever a newline character is received.

This allows the server to correctly handle cases where:

* A command arrives in multiple TCP packets
* Multiple commands arrive in a single TCP packet

## Error Handling

The server returns errors for invalid or incomplete commands.

Examples:

```text
SET
```

```text
ERR missing key
```

```text
SET name
```

```text
ERR missing value
```

```text
GET
```

```text
ERR missing key
```

Unknown commands return:

```text
ERR unknown command
```

## Performance Benchmark

A custom C++ benchmark client was created to measure TCP request/response throughput.

### Single Client

10,000 sequential `PING` requests were benchmarked over a persistent TCP connection.

Average result across three runs:

```text
~62.3K requests/sec
```

### 10 Concurrent Clients

10 clients were run concurrently, with each client sending 1,000 requests.

Total:

```text
10 clients × 1,000 requests = 10,000 requests
```

Average result across three runs:

```text
~107.8K requests/sec
```

### Benchmark Summary

| Configuration         | Total Requests | Average Throughput |
| --------------------- | -------------: | -----------------: |
| 1 client              |         10,000 |       ~62.3K req/s |
| 10 concurrent clients |         10,000 |      ~107.8K req/s |

The concurrent benchmark demonstrates the server's ability to handle multiple TCP clients simultaneously using its thread-per-client architecture.

> Benchmarks were performed locally over `127.0.0.1` and represent `PING` request/response throughput rather than general database operation performance.

## Project Structure

```text
mini-redis/
│
├── src/
│   └── main.cpp
│
├── benchmark/
│   └── benchmark.cpp
│
├── database.txt
│
├── server
│
└── README.md
```

## Technologies & Concepts

* C++
* C++17
* POSIX sockets
* TCP/IP
* Multithreading
* `std::thread`
* `std::mutex`
* `std::unordered_map`
* TCP stream framing
* Command parsing
* `std::chrono`
* File I/O
* Persistence
* Performance benchmarking

## Future Improvements

Potential future improvements include:

* Additional Redis-style commands
* Support for more data types
* Improved protocol design
* More comprehensive automated tests
* Background expiration cleanup
* More robust persistence mechanisms
* Configurable server settings
* More detailed performance benchmarks
* Improved error handling and logging
* Graceful server shutdown

## Learning Goals

This project was developed to gain hands-on experience with:

* Network programming
* TCP client-server communication
* Concurrent server design
* Thread synchronization
* Shared-state management
* In-memory data structures
* Key expiration and TTL management
* Disk persistence
* Protocol design
* Performance measurement

```