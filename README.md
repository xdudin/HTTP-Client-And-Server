# HTTP Client

A lightweight command-line HTTP client that allows users to construct HTTP requests, communicate with a web server, and display the server's response. The client supports IPv4 connections and provides essential functionality for HTTP request handling.

---

## Features

- **Command-Line Request Construction**: Create and customize HTTP requests directly from the terminal.
- **Server Communication**: Send HTTP requests to a web server over IPv4 and manage the connection.
- **Response Handling**: Receive and display the complete HTTP response, including headers and body.
- **Redirection Support**: Detect and handle HTTP redirection responses (3xx status codes).

---

## Getting Started

### Prerequisites

- GCC or another C compiler.
- A Linux environment for compilation and testing.

### Compilation

To compile the program, run the following command in the project directory:

```bash
gcc client.c -o client
```

### Usage

```bash
./client [–r n <pr1=value1 pr2=value2 …>] <URL>
```

### Example

```bash
./client -r 3 addr=jecrusalem tel=02-6655443 age=23 http://httpbin.org/anything
```
