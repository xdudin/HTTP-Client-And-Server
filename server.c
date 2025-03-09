#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <dirent.h>
#include <fcntl.h>

#define INITIAL_BUFFER_SIZE 8192
#define FIRST_LINE_SIZE 4000
#define PORT 8080
#define MAX_REQUESTS 15

// Function prototypes
int handle_client(void *arg);
char *build_http_response(int status_code, char *path);
void write_to_client(int client_fd, const char *buffer);

int read_and_write(int client_fd, const char *path);

// Main function
int main(){
    int server_fd, client_fd;
    struct sockaddr_in address;
    int addrlen = sizeof(address);

    // Create socket
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        return EXIT_FAILURE;
    }

    // Configure server address
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(PORT);

    // Bind the socket to the specified port
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        return EXIT_FAILURE;
    }

    // Listen for incoming connections
    if (listen(server_fd, 3) < 0) {
        return EXIT_FAILURE;
    }

    while(true) {
        int* client_socket = malloc(sizeof(int));
        if (client_socket == NULL) {
            return EXIT_FAILURE;
        }

        // Accept incoming connections
        if ((client_fd = accept(server_fd, (struct sockaddr *) &address, (socklen_t *) &addrlen)) < 0) {
            return EXIT_FAILURE;
        }

        *client_socket = client_fd;
        handle_client(client_socket);
    }
}

// Client handler
int handle_client(void *arg) {
    const int client_fd = *(int *)arg;
    free(arg);
    size_t total_bytes_read = 0;
    ssize_t bytes_read;
    char first_line[FIRST_LINE_SIZE] = {0};
    char path[FIRST_LINE_SIZE] = {0};

    while((bytes_read = read(client_fd, &first_line[total_bytes_read], FIRST_LINE_SIZE)) > 0){
        total_bytes_read += bytes_read;
        first_line[total_bytes_read] = '\0'; // Null-terminate the buffer for safety

        // Check if the first line is complete
        if(strstr(first_line, "\r\n")){
            break;
        }
    }

    strcpy(path, "index.html");
    constexpr int status_code = 200;

    // Construct response
    char *response = build_http_response(status_code, path);

    write:
    // Write the response to client
    write_to_client(client_fd, response);

    read_and_write(client_fd, path);

    close(client_fd);
    free(response);

    return 0;
}

// Build HTTP response
char * build_http_response(const int status_code, char* path) {
    char date_header[128];

    // Determine status text
    const char *status_text = "OK";
    const char *mime_type = "text/html";
    char *OK_body = "";

    // Calculate the response size
    constexpr size_t body_length = 0;
    constexpr int response_size = 256 + body_length; // Headers + body

    // Allocate memory for the response
    char *response = malloc(response_size);

    int written = 0;
    // Build the response headers
    written += snprintf(response, response_size,
             "HTTP/1.0 %d %s\r\n"
             "Server: webserver/1.0\r\n"
             "Date: %s\r\n",
             status_code, status_text, date_header);

    // Add Content-Type header
    written += snprintf(response + written, response_size - written, "Content-Type: %s\r\n", mime_type);

    // Add Content-Length header
    constexpr size_t cont_length = 27; // 27 bytes for fixed index.html
    written += snprintf(response + written, response_size - written, "Content-Length: %lu\r\n", cont_length);

    // Add Connection header
    written += snprintf(response + written, response_size - written, "Connection: close\r\n\r\n");

    // Add the body to the response
    char *src = OK_body;
    snprintf(response + written, response_size - written, "%s", src);

    if (strlen(OK_body) > 0)
        free(OK_body);

    return response;
}

// Write response to client
void write_to_client(const int client_fd, const char *buffer){
    // Send the request
    size_t total_sent = 0;
    const size_t buffer_len = strlen(buffer);
    while (total_sent < buffer_len) {
        const ssize_t bytes_sent = write(client_fd, buffer + total_sent, buffer_len - total_sent);
        total_sent += bytes_sent;
    }
}

int read_and_write(const int client_fd, const char *path){
    // Open the file
    const int file_fd = open(path, O_RDONLY);
    if(file_fd < 0)
        return -1;

    char buffer[4096];
    size_t bytesRead;
    // Read the file into the buffer and write to client
    while ((bytesRead = read(file_fd, buffer, sizeof(buffer))) > 0) {
        write(client_fd, buffer, bytesRead); // Write the data
    }

    close(file_fd);
    return 0;
}