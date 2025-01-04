# HTTP Client and Server

This repository contains two primary components: a lightweight command-line HTTP client and a multi-threaded HTTP server. The HTTP client enables users to construct and send HTTP requests directly from the terminal, while the server listens for client requests and dispatches work to a thread pool for efficient handling of multiple simultaneous connections. Together, these components provide a robust foundation for HTTP communication and request handling.

---

## Features

### HTTP Client
- **Command-Line Request Construction**: Create and customize HTTP requests directly from the terminal.
- **Server Communication**: Send HTTP requests to a web server over IPv4 and manage the connection.
- **Response Handling**: Receive and display the complete HTTP response, including headers and body.
- **Redirection Support**: Detect and handle HTTP redirection responses (3xx status codes).

### HTTP Server
- **Request Listening**: Accept incoming client requests over IPv4 connections.
- **Thread Pool Management**: Dispatch incoming requests to a pool of pre-initialized threads for concurrent handling.
- **Efficient Request Handling**: Ensure scalability and responsiveness by utilizing a multi-threaded architecture.
- **Customizable Worker Logic**: Define and implement the logic for handling requests within the thread pool.
---

## Getting Started

### Prerequisites

- GCC or another C compiler.
- A Linux environment for compilation and testing.

### Compilation

To compile the program, run the following command in the project directory:

### HTTP Client
```bash
gcc client.c -o client
```

#### Usage

```bash
./client [–r n <pr1=value1 pr2=value2 …>] <URL>
```

#### Example

```bash
./client -r 3 addr=jecrusalem tel=02-6655443 age=23 http://httpbin.org/anything
```
### HTTP Server
```bash
gcc server.c threadpool.c -o server
```

#### Usage

```bash
./server <port> <pool-size> <max-queue-size> <max-number-of
request>
```

#### Example

```bash
./server 8080 10 5 15
```

