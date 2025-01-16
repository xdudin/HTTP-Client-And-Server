#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <ctype.h>
#include "threadpool.h"
#include <sys/stat.h>
#include <libgen.h>
#include <dirent.h>

#define DEBUG 1
#define RFC1123FMT "%a, %d %b %Y %H:%M:%S GMT"
#define INITIAL_BUFFER_SIZE 8192
#define FIRST_LINE_SIZE 4000

#if DEBUG
#define DEBUG_PRINT(fmt, ...) \
        fprintf(stderr, "DEBUG: " fmt, ##__VA_ARGS__)
#else
#define DEBUG_PRINT(fmt, ...) \
        do { } while (0)
#endif

typedef struct{
    char method[FIRST_LINE_SIZE];
    char path[FIRST_LINE_SIZE];
    char version[FIRST_LINE_SIZE];
}request_st;

// TODO: ask about the wrong content legnth of example files

int parse_request(char* first_line, request_st* client_request);
int find_index_html(const char* dir_path);
int has_execute_permissions(const char* path);
void get_current_date(char* timebuf, size_t buff_size);
char *get_mime_type(char *name);
void write_to_client(int client_fd, unsigned char* buffer, int buffer_len);
int generate_directory_listing(const char *dir_path, char **html_body, int* is_allocated, int *body_size);
int is_directory(const char *path);
char *get_last_modified_date(const char *path);
int open_read_file(unsigned char **body, char* path, int *is_allocated);
int construct_OK_body(char* path, unsigned char** body, int *file_size);
unsigned char* build_http_response(int status_code, char* path, const unsigned char* body, long body_size, int *response_len);

int handle_request(void *arg) {
    int client_fd = *(int *)arg;
    free(arg);
    DEBUG_PRINT("%d enters function and handles client with socket fd %d\n", (int)pthread_self(), client_fd);

    size_t total_bytes_read = 0;
    int bytes_read;
    int first_line_found = 0;
    char* end_of_line;
    char first_line[FIRST_LINE_SIZE];
    int status_code;
    unsigned char* body = NULL;
    int body_size = 0;
    unsigned char* response = NULL;
    int response_len = 0;

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

    request_st client_request = {0};

    status_code = parse_request(first_line, &client_request);

    DEBUG_PRINT("Status Code: %d\n", status_code);

    if (status_code == 200) {
        // Construct body for 200 responses
        if (construct_OK_body(client_request.path, &body, &body_size) != 0) {
            response = build_http_response(500, NULL, NULL, 0, &response_len);
            goto write;
        }
    }

    // Construct response
    response = build_http_response(status_code, client_request.path, body, body_size, &response_len);

    write:
    // Send response to client
    write_to_client(client_fd, response, response_len);

    close(client_fd);
    if (body)
        free(body);
    free(response);
    DEBUG_PRINT("%d finished handling client with socket fd %d\n", (int)pthread_self(), client_fd);
    return 0;
}

unsigned char* build_http_response(int status_code, char* path, const unsigned char* body, long body_size, int *response_len) {
    const char* status_text;
    const char* custom_message;
    const char* error_template =
            "<HTML><HEAD><TITLE>%d %s</TITLE></HEAD>\r\n"
            "<BODY><H4>%d %s</H4>\r\n%s\r\n</BODY></HTML>";
    char error_body[256] = {0};
    const char* mime_type = NULL;
    const char* last_modified = NULL;
    char date_header[128];
    unsigned char* response;
    size_t response_size;

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
    size_t body_length = (status_code == 200 ? body_size : strlen(error_body));
    response_size = 256 + body_length; // Headers + body

    // Allocate memory for the response
    response = (unsigned char*)malloc(response_size);
    if (!response) {
        return build_http_response(500, NULL, NULL, 0, response_len);
    }

    int written = 0;
    // Build the response headers
    written += snprintf((char *)response, response_size,
             "HTTP/1.0 %d %s\r\n"
             "Server: webserver/1.0\r\n"
             "Date: %s\r\n",
             status_code, status_text, date_header);

    // Add Location header for 302 redirects
    if (status_code == 302) {
        written += snprintf((char *) response + written, response_size - written,
                 "Location: %s/\r\n", path);
    }

    // Add Content-Type header
    if (mime_type) {
        written += snprintf((char *)response + written, response_size - written,
                 "Content-Type: %s\r\n", mime_type);
    }

    // Add Content-Length and Connection headers
    written += snprintf((char *)response + written, response_size - written,
             "Content-Length: %lu\r\n",
             body_length);

    // Add Last-Modified header for 200 responses
    if (status_code == 200 && last_modified) {
        written += snprintf((char *)response + written, response_size - written,
                 "Last-Modified: %s\r\n", last_modified);
    }

    // Add Connection header
    written += snprintf((char *)response + written, response_size - written,
             "Connection: close\r\n\r\n");

    // Add the body to the response
    // strcat((char *)response, (status_code == 200 ? body : error_body));
    const unsigned char *src = (status_code == 200 ? body : (unsigned char *)error_body);
    size_t src_len = (status_code == 200 ? body_size : strlen(error_body));
    memcpy(response + written, src, src_len);

    *response_len = written + src_len;
    return response;
}

int main(int argc, char* argv[]){
    // TODO: ask about the last argument in usage print
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
        dispatch(tp, handle_request, client_socket);
        requestsCounter++;
        DEBUG_PRINT("Accepted incoming connection with client IP: %s\n", inet_ntoa(address.sin_addr));
    }

    DEBUG_PRINT("Starting to destroy threadpool...\n");
    // Close the connections
    destroy_threadpool(tp);
    close(server_fd);
    return 0;
}

int parse_request(char* first_line, request_st* client_request){
    // Check if the request line contains 3 parts
    if (sscanf(first_line, "%s %s %s", client_request->method, client_request->path, client_request->version) != 3)
        return 400;
    DEBUG_PRINT("Path: %s\n", client_request->path);
    // Check if the version is of HTTP
    if (strstr(client_request->version, "HTTP/") == NULL)
        return 400;

    // Check if the method is GET
    if (strstr(client_request->method, "GET") == NULL)
        return 501;

    if (client_request->path[0] == '/')
        memmove(client_request->path, client_request->path + 1, sizeof(client_request->path));

    struct stat path_stat;
    // Check if the requested path exists
    if (stat(client_request->path, &path_stat) != 0){
        return 404;
    }

    // Directory Case
    if (S_ISDIR(path_stat.st_mode)){
        size_t len = strlen(client_request->path);
        if (client_request->path[len - 1] != '/'){
            return 302; // Redirect needed
        }

        if (find_index_html(client_request->path)) {
            // case of index.html
            strcat(client_request->path, "index.html");
        }
        // At this point, we have a directory with either index.html or directory listing case
        return 200;
    }

    // File case
    if (!S_ISREG(path_stat.st_mode) || !(path_stat.st_mode & S_IROTH))
        return 403; // Not a regular file or has no read permissions for everyone

    if (strchr(client_request->path, '/') != NULL){
        // The file is in some directory
        return has_execute_permissions(client_request->path);
    }

    // The path is the file itself
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

int construct_OK_body(char* path, unsigned char** body, int* file_size) {
    int is_allocated = 0;

        if (is_directory(path)) {
            // case of directory listing
            if (generate_directory_listing(path,(char **) body, &is_allocated, file_size) != 0) {
                return -1;
            }
        }
        else {
            DEBUG_PRINT("Attempting to read file %s\n", path);
            if (open_read_file(body, path, file_size) != 0) {
                return -1;
            }
            DEBUG_PRINT("Content of the file %s: %s\n", path, *body);
        }

        return 0;
}

int open_read_file(unsigned char **body, char* path, int *file_size){
    // Open the file
    FILE *file = fopen(path, "rb");
    if (!file) {
        return -1;
    }
    // Get the file size
    fseek(file, 0, SEEK_END);
    *file_size = ftell(file);
    fseek(file, 0, SEEK_SET);  // Rewind to the beginning of the file

    DEBUG_PRINT("File Size: %d\n", *file_size);
    // Dynamically allocate memory for the body buffer
    *body = (unsigned char *)malloc(*file_size + 1);
    if (!*body) {
        fclose(file);
        return -1;
    }

    // Read the file into the body buffer
    fread(*body, 1, *file_size, file);
    (*body)[*file_size] = '\0';
    fclose(file);
    return 0;
}

void get_current_date(char* timebuf, size_t buff_size){
    time_t now;

    now = time(NULL);
    strftime(timebuf, buff_size, RFC1123FMT, gmtime(&now));
    // timebuf holds the correct format of the current time
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

void write_to_client(int client_fd, unsigned char* buffer, int buffer_len){
    // Send the request
    size_t total_sent = 0;
    //size_t buffer_len = strlen(buffer);
    while (total_sent < buffer_len) {
        ssize_t bytes_sent = write(client_fd, buffer + total_sent, buffer_len - total_sent);
        if (bytes_sent < 0) {
            free(buffer);
            unsigned char* response = build_http_response(500, NULL, NULL, 0, &buffer_len);
            write_to_client(client_fd, response, buffer_len);
            return;
        }
        total_sent += bytes_sent;
    }
}

int generate_directory_listing(const char *dir_path, char **html_body, int* is_allocated, int *body_size) {
    size_t buffer_size = INITIAL_BUFFER_SIZE;
    size_t used_size = 0;
    *html_body = (char*)malloc(buffer_size);
    if (!*html_body){
        return -1;
    }

    *is_allocated = 1;  // signal that body pointer has been allocated
    DIR *dir;
    struct dirent *entry;
    struct stat file_stat;
    char full_path[1024];
    char time_buf[128];

    // Start building the HTML
    used_size += snprintf(*html_body + used_size, buffer_size - used_size,
                          "<HTML>\r\n"
                          "<HEAD><TITLE>Index of %s</TITLE></HEAD>\r\n\r\n"
                          "<BODY>\r\n"
                          "<H4>Index of %s</H4>\r\n\r\n"
                          "<table CELLSPACING=8>\r\n"
                          "<tr><th>Name</th><th>Last Modified</th><th>Size</th></tr>\r\n\r\n\r\n",
                          dir_path, dir_path);

    // Open the directory
    dir = opendir(dir_path);
    if (!dir) {
        free(*html_body);
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

        // Estimate required size for the new row
        size_t row_size = snprintf(NULL, 0,
                                   "<tr>\r\n<td><A HREF=\"%s\">%s</A></td><td>%s</td>\r\n<td>%s</td>\r\n</tr>\r\n\r\n",
                                   entry->d_name, entry->d_name, time_buf, size_str);

        // Handle buffer overflow
        if (used_size + row_size + 1 > buffer_size) {
            buffer_size *= 2;
            char *new_html = realloc(*html_body, buffer_size);
            if (!new_html) {
                free(*html_body);
                return -1;
            }
            *html_body = new_html;
        }

        // Add the row to the buffer
        used_size += snprintf(*html_body + used_size, buffer_size - used_size,
                              "<tr>\r\n<td><A HREF=\"%s\">%s</A></td><td>%s</td>\r\n<td>%s</td>\r\n</tr>\r\n\r\n",
                              entry->d_name, entry->d_name, time_buf, size_str);

    }

    closedir(dir);

    // Add footer
    if (used_size + 64 > buffer_size) {
        buffer_size += 64;
        char *new_html = realloc(*html_body, buffer_size);
        if (!new_html) {
            free(*html_body);
            return -1;
        }
        *html_body = new_html;
    }

    used_size += snprintf(*html_body + used_size, buffer_size - used_size,
                          "</table>\r\n\r\n<HR>\r\n\r\n<ADDRESS>webserver/1.0</ADDRESS>\r\n\r\n</BODY></HTML>\r\n\r\n");

    *body_size = used_size;
    return 0;
}

int get_absolute_path(char *relative_path){
    char cwd[PATH_MAX]; // Buffer for the current working directory

    // Get the current working directory
    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        return -1;
    }
    DEBUG_PRINT("Absolute path of server directory: \n%s\n", cwd);
    // Create a new path with cwd + relative_path
    char new_path[PATH_MAX];
    snprintf(new_path, sizeof(new_path), "%s%s", cwd, relative_path);
    strcpy(relative_path, new_path);
    DEBUG_PRINT("Absolute path with requested file/directory \n%s\n", relative_path);
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
