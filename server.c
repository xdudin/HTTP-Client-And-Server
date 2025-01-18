#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include "threadpool.h"
#include <sys/stat.h>
#include <libgen.h>
#include <dirent.h>

#define DEBUG 1
#define RFC1123FMT "%a, %d %b %Y %H:%M:%S GMT"
#define INITIAL_BUFFER_SIZE 8192
#define FIRST_LINE_SIZE 4000

// Debugging macros
#if DEBUG
#define DEBUG_PRINT(fmt, ...) \
        fprintf(stderr, "DEBUG: " fmt, ##__VA_ARGS__)
#else
#define DEBUG_PRINT(fmt, ...) \
        do { } while (0)
#endif

// Struct definitions
typedef struct{
    unsigned char *body;
    int size;
}body_st;

typedef struct{
    unsigned char *response;
    int size;
}response_st;

// TODO: ask about the wrong content legnth of example files
// TODO: ask about the permissions (others or oth+grp+usr)

// Function prototypes
int handle_client(void *arg);
void build_http_response(int status_code, char *path, body_st *bodySt, response_st *responseSt);
void write_to_client(int client_fd, response_st *responseSt);
void free_structs(body_st *bodySt, response_st *responseSt);

int parse_request(char *first_line, char *path);
int construct_OK_body(char *path, body_st *bodySt);
int open_read_file(char *path, body_st *bodySt);
int generate_directory_listing(char *dir_path, body_st *bodySt);
int find_index_html(const char *dir_path);
int has_execute_permissions(const char *path);
int is_directory(const char *path);

void get_current_date(char* timebuf, size_t buff_size);
char *get_mime_type(char *name);
char *get_last_modified_date(const char *path);

// Main function
int main(int argc, char* argv[]){
    if(argc != 5){
        printf("Usage: server <port> <pool-size> <max-queue-size> <max-number-of-request>\n");
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
        destroy_threadpool(tp);
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
        destroy_threadpool(tp);
        exit(EXIT_FAILURE);
    }

    // Listen for incoming connections
    if (listen(server_fd, 3) < 0) {
        perror("Listen failed");
        close(server_fd);
        destroy_threadpool(tp);
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
        dispatch(tp, handle_client, client_socket);
        requestsCounter++;
        DEBUG_PRINT("Accepted incoming connection with client IP: %s\n", inet_ntoa(address.sin_addr));
    }

    DEBUG_PRINT("Starting to destroy threadpool...\n");
    // Close the connections
    destroy_threadpool(tp);
    close(server_fd);
    return 0;
}

// Client handler
int handle_client(void *arg) {
    int client_fd = *(int *)arg;
    free(arg);
    DEBUG_PRINT("%d enters function and handles client with socket fd %d\n", (int)pthread_self(), client_fd);
    size_t total_bytes_read = 0;
    int bytes_read;
    int first_line_found = 0;
    char* end_of_line;
    char first_line[FIRST_LINE_SIZE];
    char path[FIRST_LINE_SIZE];
    int status_code;
    body_st *bodySt = (body_st*) malloc(sizeof(body_st));
    response_st *responseSt = (response_st*) malloc(sizeof(response_st));
    if (bodySt == NULL || responseSt == NULL) {
        response_st temp_response = {0};
        build_http_response(500, NULL, NULL, &temp_response);
        goto write;
    }

    bodySt->body = responseSt->response = NULL;
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
        build_http_response(500, NULL, NULL, responseSt);
        goto write;
    }
    else if(first_line_found){
        *end_of_line = '\0';
    }

    DEBUG_PRINT("First line of the request:\n%s\n", first_line);

    status_code = parse_request(first_line, path);

    DEBUG_PRINT("Status Code: %d\n", status_code);

    if (status_code == 200) {
        // Construct body for 200 responses
        if (construct_OK_body(path, bodySt) != 0) {
            build_http_response(500, NULL, NULL, responseSt);
            goto write;
        }
    }

    // Construct response
    build_http_response(status_code, path, bodySt, responseSt);

    write:
    // Send response to client
    write_to_client(client_fd, responseSt);

    close(client_fd);

    free_structs(bodySt, responseSt);

    DEBUG_PRINT("%d finished handling client with socket fd %d\n", (int)pthread_self(), client_fd);
    return 0;
}

// Build HTTP response
void build_http_response(int status_code, char* path, body_st *bodySt, response_st *responseSt) {
    const char* status_text;
    const char* custom_message;
    const char* error_template =
            "<HTML><HEAD><TITLE>%d %s</TITLE></HEAD>\r\n"
            "<BODY><H4>%d %s</H4>\r\n%s\r\n</BODY></HTML>";
    char error_body[256] = {0};
    const char* mime_type = NULL;
    const char* last_modified = NULL;
    char date_header[128];

    // Get current date
    get_current_date(date_header, sizeof(date_header));

    // Determine status text
    switch (status_code) {
        case 200: status_text = "OK"; break;
        case 302: status_text = "Found"; custom_message = "Directories must end with a slash."; break;
        case 400: status_text = "Bad Request"; custom_message = "Bad Request.";break;
        case 403: status_text = "Forbidden"; custom_message = "Access denied."; break;
        case 404: status_text = "Not Found"; custom_message = "File not found."; break;
        case 500: status_text = "Internal Server Error"; custom_message = "Some server side error."; break;
        case 501: status_text = "Not supported"; custom_message = "Method is not supported."; break;
    }

    // Handle error/redirect bodies
    if (status_code != 200) {
        snprintf(error_body, sizeof(error_body), error_template, status_code, status_text, status_code, status_text, custom_message);
        mime_type = "text/html"; // Explicitly set Content-Type for error/redirect bodies
    }

    // For 200 OK, determine MIME type and last modified date from the path
    if (status_code == 200 && path) {
        mime_type = is_directory(path) ? "text/html" : get_mime_type(path);
        last_modified = get_last_modified_date(path);
    }

    // Calculate the response size
    size_t body_length = (status_code == 200 ? bodySt->size : strlen(error_body));
    responseSt->size = 256 + body_length; // Headers + body

    // Allocate memory for the response
    responseSt->response = (unsigned char*) malloc(responseSt->size);
    if (!responseSt->response) {
        build_http_response(500, NULL, NULL, responseSt);
        return;
    }

    int written = 0;
    // Build the response headers
    written += snprintf((char *)responseSt->response, responseSt->size,
             "HTTP/1.0 %d %s\r\n"
             "Server: webserver/1.0\r\n"
             "Date: %s\r\n",
             status_code, status_text, date_header);

    // Add Location header for 302 redirects
    if (status_code == 302) {
        written += snprintf((char *) responseSt->response + written, responseSt->size - written,
                 "Location: /%s/\r\n", path);
    }

    // Add Content-Type header
    if (mime_type) {
        written += snprintf((char *)responseSt->response + written, responseSt->size - written,
                 "Content-Type: %s\r\n", mime_type);
    }

    // Add Content-Length and Connection headers
    written += snprintf((char *)responseSt->response + written, responseSt->size - written,
             "Content-Length: %lu\r\n",
             body_length);

    // Add Last-Modified header for 200 responses
    if (status_code == 200 && last_modified) {
        written += snprintf((char *)responseSt->response + written, responseSt->size - written,
                 "Last-Modified: %s\r\n", last_modified);
    }

    // Add Connection header
    written += snprintf((char *)responseSt->response + written, responseSt->size - written,
             "Connection: close\r\n\r\n");

    // Add the body to the response
    const unsigned char *src = (status_code == 200 ? bodySt->body : (unsigned char *)error_body);
    size_t src_len = (status_code == 200 ? bodySt->size : strlen(error_body));
    memcpy(responseSt->response + written, src, src_len);

    responseSt->size = written + src_len;
    return;
}

// Write response to client
void write_to_client(int client_fd, response_st *responseSt){
    // Send the request
    size_t total_sent = 0;
    //size_t buffer_len = strlen(buffer);
    while (total_sent < responseSt->size) {
        ssize_t bytes_sent = write(client_fd, responseSt->response + total_sent, responseSt->size - total_sent);
        if (bytes_sent < 0) {
            free(responseSt->response);
            build_http_response(500, NULL, NULL, responseSt);
            write_to_client(client_fd, responseSt);
            return;
        }
        total_sent += bytes_sent;
    }
}

// Helper functions
int parse_request(char* first_line, char* path){
    char method[FIRST_LINE_SIZE];
    char version[FIRST_LINE_SIZE];
    // Check if the request line contains 3 parts
    if (sscanf(first_line, "%s %s %s", method, path, version) != 3)
        return 400;
    DEBUG_PRINT("Path: %s\n", path);
    // Check if the version is of HTTP
    if (strstr(version, "HTTP/") == NULL)
        return 400;

    // Check if the method is GET
    if (strstr(method, "GET") == NULL)
        return 501;

    if (path[0] == '/') {
        if (strlen(path) > 1)
            memmove(path, path + 1, strlen(path));
        else  {
            // Case of current directory
            if (!find_index_html("")) {
                return 200; // Directory listing response
            }
            strcpy(path, "index.html");
        }
    }

    struct stat path_stat;
    // Check if the requested path exists
    if (stat(path, &path_stat) != 0){
        return 404;
    }

    // Directory Case
    if (S_ISDIR(path_stat.st_mode)){
        size_t len = strlen(path);
        if (path[len - 1] != '/'){
            return 302; // Redirect needed
        }

        if (find_index_html(path)) {
            // case of index.html
            strcat(path, "index.html");
            stat(path, &path_stat);
            goto file;
        }
        // At this point, we have a directory listing case
        return 200;
    }

    file:
    // File case
    if (!S_ISREG(path_stat.st_mode) || !(path_stat.st_mode & S_IROTH))
        return 403; // Not a regular file or has no read permissions for everyone

    if (strchr(path, '/') != NULL){
        // The file is in some directory
        return has_execute_permissions(path);
    }

    // The path is to a file
    return 200;
}

int find_index_html(const char* dir_path){
    char full_path[FIRST_LINE_SIZE];
    struct stat file_Stat;

    snprintf(full_path, sizeof(full_path), "%sindex.html", dir_path);
    if (stat(full_path, &file_Stat) == 0)
        return 1;
    return 0;
}

int has_execute_permissions(const char* path){
    // Check execute permissions for all directories in path
    char path_copy[FIRST_LINE_SIZE];
    strncpy(path_copy, path, sizeof(path_copy));
    char *dir = dirname(path_copy);
    struct stat dir_stat;

    while (strcmp(dir, ".") != 0) {
        DEBUG_PRINT("dir: %s\n", dir);
        if (stat(dir, &dir_stat) != 0 || !(dir_stat.st_mode & S_IXOTH) ||
                                                  !(dir_stat.st_mode & S_IXGRP) ||
                                                  !(dir_stat.st_mode & S_IXUSR)) {
            return 403;  // Directory not executable
        }
        dir = dirname(dir);
    }

    return 200;  // OK - will serve file
}

int construct_OK_body(char* path, body_st *bodySt) {
        if (is_directory(path)) {
            // case of directory listing
            if (generate_directory_listing(path, bodySt) != 0) {
                return -1;
            }
        }
        else {
            DEBUG_PRINT("Attempting to read file %s\n", path);
            if (open_read_file(path, bodySt) != 0) {
                return -1;
            }
            DEBUG_PRINT("Content of the file %s: %s\n", path, bodySt->body);
        }

        return 0;
}

int open_read_file(char *path, body_st *bodySt){
    // Open the file
    FILE *file = fopen(path, "rb");
    if (!file) {
        return -1;
    }
    // Get the file size
    fseek(file, 0, SEEK_END);
    bodySt->size = ftell(file);
    fseek(file, 0, SEEK_SET);  // Rewind to the beginning of the file

    DEBUG_PRINT("File Size: %d\n", bodySt->size);
    // Dynamically allocate memory for the body buffer
    bodySt->body = (unsigned char *)malloc(bodySt->size + 1);
    if (!bodySt->body) {
        fclose(file);
        return -1;
    }

    // Read the file into the body buffer
    fread(bodySt->body, 1, bodySt->size, file);
    (bodySt->body)[bodySt->size] = '\0';
    fclose(file);
    return 0;
}

void get_current_date(char *timebuf, size_t buff_size) {
    time_t now = time(NULL);
    strftime(timebuf, buff_size, RFC1123FMT, gmtime(&now));
}

char *get_mime_type(char *name)
{
    char *ext = strrchr(name, '.');
    if (!ext) return NULL;
    if (strcmp(ext, ".html") == 0 || strcmp(ext, ".htm") == 0) return "text/html";
    if (strcmp(ext, ".jpg") == 0 || strcmp(ext, ".jpeg") == 0) return "image/jpeg";
    if (strcmp(ext, ".gif") == 0) return "image/gif";
    if (strcmp(ext, ".png") == 0) return "image/png";
    if (strcmp(ext, ".css") == 0) return "text/css";
    if (strcmp(ext, ".au") == 0) return "audio/basic";
    if (strcmp(ext, ".wav") == 0) return "audio/wav";
    if (strcmp(ext, ".avi") == 0) return "video/x-msvideo";
    if (strcmp(ext, ".mpeg") == 0 || strcmp(ext, ".mpg") == 0) return "video/mpeg";
    if (strcmp(ext, ".mp3") == 0) return "audio/mpeg";
    return NULL;
}

int generate_directory_listing(char *dir_path, body_st *bodySt) {
    size_t buffer_size = INITIAL_BUFFER_SIZE;
    size_t used_size = 0;
    bodySt->body = (unsigned char*)malloc(buffer_size);
    if (!bodySt->body){
        return -1;
    }

    DIR *dir;
    struct dirent *entry;
    struct stat file_stat;
    char full_path[1024];
    char time_buf[128];

    // Start building the HTML
    used_size += snprintf((char *)bodySt->body + used_size, buffer_size - used_size,
                          "<HTML>\r\n"
                          "<HEAD><TITLE>Index of %s</TITLE></HEAD>\r\n\r\n"
                          "<BODY>\r\n"
                          "<H4>Index of %s</H4>\r\n\r\n"
                          "<table CELLSPACING=8>\r\n"
                          "<tr><th>Name</th><th>Last Modified</th><th>Size</th></tr>\r\n\r\n\r\n",
                          dir_path, dir_path);

    // Check if path is current directory
    if (strcmp(dir_path, "/") == 0) {
        dir = opendir(".");
        strcpy(dir_path, "");
    }
    else
        dir = opendir(dir_path);

    if (!dir) {
        free(bodySt->body);
        return -1;
    }

    // Iterate through directory entries
    while ((entry = readdir(dir)) != NULL) {
        // Skip through . (current directory) and .. (parent directory)
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        snprintf(full_path, sizeof(full_path), "%s%s", dir_path, entry->d_name);
        if (stat(full_path, &file_stat) == -1) {
            continue; // Skip entries we cannot stat
        }

        // Format modification time
        strftime(time_buf, sizeof(time_buf), RFC1123FMT, gmtime(&file_stat.st_mtime));

        // Extract size of file
        const char *size_str = S_ISREG(file_stat.st_mode) ?
                               (sprintf(full_path, "%ld", file_stat.st_size), full_path) : "";

        // Handle directory entries with concatenating trailing /
        if (S_ISDIR(file_stat.st_mode))
            strcat(entry->d_name, "/");

        // Estimate required size for the new row
        size_t row_size = snprintf(NULL, 0,
                                   "<tr>\r\n<td><A HREF=\"%s\">%s</A></td><td>%s</td>\r\n<td>%s</td>\r\n</tr>\r\n\r\n",
                                   entry->d_name, entry->d_name, time_buf, size_str);

        // Handle buffer overflow
        if (used_size + row_size + 1 > buffer_size) {
            buffer_size *= 2;
            unsigned char *new_html = realloc(bodySt->body, buffer_size);
            if (!new_html) {
                free(bodySt->body);
                return -1;
            }
            bodySt->body = new_html;
        }

        // Add the row to the buffer
        used_size += snprintf((char *)bodySt->body + used_size, buffer_size - used_size,
                              "<tr>\r\n<td><A HREF=\"%s\">%s</A></td><td>%s</td>\r\n<td>%s</td>\r\n</tr>\r\n\r\n",
                              entry->d_name, entry->d_name, time_buf, size_str);

    }

    closedir(dir);

    // Add footer
    if (used_size + 64 > buffer_size) {
        buffer_size += 64;
        unsigned char *new_html = realloc(bodySt->body, buffer_size);
        if (!new_html) {
            free(bodySt->body);
            return -1;
        }
        bodySt->body = new_html;
    }

    used_size += snprintf((char *)bodySt->body + used_size, buffer_size - used_size,
                          "</table>\r\n\r\n<HR>\r\n\r\n<ADDRESS>webserver/1.0</ADDRESS>\r\n\r\n</BODY></HTML>\r\n\r\n");

    bodySt->size = used_size;
    strcpy(dir_path, dir_path[0] == '\0' ? "/" : dir_path);
    return 0;
}

char *get_last_modified_date(const char *path) {
    static char timebuf[128];
    struct tm tm_info;
    struct stat file_stat;

    if (stat(path, &file_stat) == -1)
        return NULL;

    gmtime_r(&file_stat.st_mtime, &tm_info);
    strftime(timebuf, sizeof(timebuf), RFC1123FMT, &tm_info);

    return timebuf;
}

int is_directory(const char *path) {
    return path[strlen(path) - 1] == '/';
}

void free_structs(body_st *bodySt, response_st *responseSt) {
    if (bodySt) {
        if (bodySt->body)
            free(bodySt->body);
        free(bodySt);
    }
    if (responseSt) {
        if (responseSt->response)
            free(responseSt->response);
        free(responseSt);
    }
}
