#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <ctype.h>
#include "threadpool.h"

#define DEBUG 1

#if DEBUG
#define DEBUG_PRINT(fmt, ...) \
        fprintf(stderr, "DEBUG: " fmt, ##__VA_ARGS__)
#else
#define DEBUG_PRINT(fmt, ...) \
        do { } while (0)
#endif

#define FIRST_LINE_SIZE 4000

int handle_request(void *arg) {
    int client_fd = *(int *)arg;
    DEBUG_PRINT("%d enters function and handles client with socket fd %d\n", (int)pthread_self(), client_fd);

    size_t total_bytes_read = 0;
    int bytes_read;
    int first_line_found = 0;
    char* end_of_line;
    char first_line[FIRST_LINE_SIZE];
    while((bytes_read = read(client_fd, &first_line[total_bytes_read], FIRST_LINE_SIZE)) > 0){
        total_bytes_read += bytes_read;
        first_line[total_bytes_read] = '\0'; // Null-terminate the buffer for safety

        // Check if the first line is complete
        end_of_line = strstr(first_line, "\r\n");
        if(end_of_line){
            first_line_found = 1;
            break;
        }
    }

    if(bytes_read < 0){
        perror("read");
        return 1;
    }
    else if(first_line_found){
        *end_of_line = '\0';
    }

    DEBUG_PRINT("First line of the request:\n%s\n", first_line);
    close(client_fd);
    free(arg);
    return 0;
}

int main(int argc, char* argv[]){
    // TODO: ask about the last argument in usage print
    if(argc != 5){
        printf("Usage: server <port> <pool-size> <queue-size> <max-requests>\n");
        exit(EXIT_FAILURE);
    }

    int port = atoi(argv[1]);
    int numThreads = atoi(argv[2]);
    int qMaxSize = atoi(argv[3]);
    int maxRequests = atoi(argv[4]);

    threadpool *tp = create_threadpool(numThreads, qMaxSize);
    if(tp == NULL)
        exit(EXIT_FAILURE);

    int server_fd, client_fd;
    struct sockaddr_in address;
    int addrlen = sizeof(address);

    // Create socket
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("Socket failed");
        exit(EXIT_FAILURE);
    }

    // Configure server address
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(port);

    // Bind the socket to the specified port
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("Bind failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    // Listen for incoming connections
    if (listen(server_fd, 3) < 0) {
        perror("Listen failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    DEBUG_PRINT("Server listening on port %d\n", port);

    int requestsCounter = 0;
    while(requestsCounter < maxRequests) {

        int* client_socket = (int*) malloc(sizeof(int));
        if (client_socket == NULL) {
            perror("malloc");
            close(server_fd);
            destroy_threadpool(tp);
            exit(EXIT_FAILURE);
        }

        // Accept incoming connections
        if ((client_fd = accept(server_fd, (struct sockaddr *) &address, (socklen_t *) &addrlen)) < 0) {
            perror("Accept failed");
            close(server_fd);
            destroy_threadpool(tp);
            exit(EXIT_FAILURE);
        }

        *client_socket = client_fd;
        dispatch(tp, handle_request, client_socket);
        requestsCounter++;
        DEBUG_PRINT("Accepted incoming connection with client IP: %s\n", inet_ntoa(address.sin_addr));
    }

    // Echo back messages from the client
//    while (1) {
//        memset(buffer, 0, BUFFER_SIZE);
//        int read_size = read(client_fd, buffer, BUFFER_SIZE);
//        if (read_size <= 0) {
//            printf("Client disconnected\n");
//            break;
//        }
//        printf("Received: %s", buffer);
//        int i = 0;
//        while(*(buffer + i)){
//            char ch = toupper(buffer[i]);
//            buffer[i++] = ch;
//        }
//        send(client_fd, buffer, strlen(buffer), 0);
//    }

    // Close the connections
    close(client_fd);
    close(server_fd);
    destroy_threadpool(tp);
    return 0;
}


