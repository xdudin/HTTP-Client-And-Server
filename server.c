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
#define HTTP_400_BODY "<HTML><HEAD><TITLE>400 Bad Request</TITLE></HEAD>\r\n" \
                      "<BODY><H4>400 Bad request</H4>\r\n"                  \
                      "Bad Request.\r\n"                                    \
                      "</BODY></HTML>"
#define HTTP_302_BODY "<HTML><HEAD><TITLE>302 Found</TITLE></HEAD>\r\n" \
                        "<BODY><H4>302 Found</H4>\r\n"  \
                        "Directories must end with a slash.\r\n"    \
                        "</BODY></HTML>"
#define HTTP_403_BODY "<HTML><HEAD><TITLE>403 Forbidden</TITLE></HEAD>\r\n" \
                        "<BODY><H4>403 Forbidden</H4>\r\n" \
                        "Access denied.\r\n" \
                        "</BODY></HTML>"
#define HTTP_404_BODY "<HTML><HEAD><TITLE>404 Not Found</TITLE></HEAD>\r\n" \
                        "<BODY><H4>404 Not Found</H4>\r\n" \
                        "File not found.\r\n" \
                        "</BODY></HTML>"
#define HTTP_500_BODY "<HTML><HEAD><TITLE>500 Internal Server Error</TITLE></HEAD>\r\n" \
                        "<BODY><H4>500 Internal Server Error</H4>\r\n" \
                        "Some server side error.\r\n" \
                        "</BODY></HTML>"
#define HTTP_501_BODY "<HTML><HEAD><TITLE>501 Not supported</TITLE></HEAD>\r\n" \
                        "<BODY><H4>501 Not supported</H4>\r\n" \
                        "Method is not supported.\r\n" \
                        "</BODY></HTML>"

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

int parse_request(char* first_line, request_st* client_request);
int find_index_html(const char* dir_path);
int has_execute_permissions(const char* path);
void construct_response(int client_fd, int status_code, char* path);
void get_current_date(char* timebuf, size_t buff_size);
char *get_mime_type(char *name);
void write_to_client(int client_fd, char* buffer);
int generate_directory_listing(const char *dir_path, char **html_body, int* is_allocated);
void send_internal_error_response(int client_fd);
int get_absolute_path(char *relative_path);
int is_directory(const char *path);
char *get_last_modified_date(const char *path);
int open_read_file(char **body, char* path, int *is_allocated);

int handle_request(void *arg) {
    int client_fd = *(int *)arg;
    DEBUG_PRINT("%d enters function and handles client with socket fd %d\n", (int)pthread_self(), client_fd);

    size_t total_bytes_read = 0;
    int bytes_read;
    int first_line_found = 0;
    char* end_of_line;
    char first_line[FIRST_LINE_SIZE];
    int status_code;

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

    // Construct absolute path
    construct_response(client_fd, status_code, client_request.path);

    close(client_fd);
    free(arg);
    DEBUG_PRINT("%d finished handling client with socket fd %d\n", (int)pthread_self(), client_fd);
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

    // Close the connections
    destroy_threadpool(tp);
    close(server_fd);
    return 0;
}

int parse_request(char* first_line, request_st* client_request){
    // Check if the request line contains 3 parts
    if (sscanf(first_line, "%s %s %s", client_request->method, client_request->path, client_request->version) != 3)
        return 400;

    // Check if the version is of HTTP
    if (strstr(client_request->version, "HTTP/") == NULL)
        return 400;

    // Check if the method is GET
    if (strstr(client_request->method, "GET") == NULL)
        return 501;

    // Construct absolute path
    if (get_absolute_path(client_request->path) != 0)
        return 500;

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

        // At this point, we have a directory with trailing '/'
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

    while (strcmp(dir, "/") != 0) {
        if (stat(dir, &dir_stat) != 0 || !(dir_stat.st_mode & S_IXOTH) ||
                                                  !(dir_stat.st_mode & S_IXGRP) ||
                                                  !(dir_stat.st_mode & S_IXUSR)) {
            return 403;  // Directory not executable
        }
        dir = dirname(dir);
    }

    return 200;  // OK - will serve file
}

void construct_response(int client_fd, int status_code, char* path){
    const char *status_phrase;
    char timebuf[128];
    char *last_modified = NULL;
    char status_line[64];
    char headers[1024];
    char* body = NULL;
    int is_allocated = 0;

    switch (status_code){
        case 400: status_phrase = "400 Bad Request"; body = HTTP_400_BODY; break;
        case 501: status_phrase = "501 Not supported"; body = HTTP_501_BODY; break;
        case 404: status_phrase = "404 Not Found"; body = HTTP_404_BODY; break;
        case 403: status_phrase = "403 Forbidden"; body = HTTP_403_BODY; break;
        case 302: status_phrase = "302 Found"; strcat(path, "/"); body = HTTP_302_BODY; break;
        case 200: status_phrase = "200 OK"; break;
    }

    // Get the current date time
    get_current_date(timebuf, sizeof(timebuf));
    DEBUG_PRINT("Current time: \n%s\n", timebuf);
    strcat(timebuf, "\r\n");

    // Construct the location header if dealing with 302 response
    char location_header[strlen(path) + 12 + 1];    // To handle the size of "Location: <path>\r\n"
    if (status_code == 302)
        snprintf(location_header, sizeof(location_header), "Location: %s\r\n", path);

    // Get content type
    char content_type[20] = "text/html";

    // Construct 200 OK body response (reads the requested file / returns directory listing)
    if (status_code == 200) {
        char full_path[1024];
        // Check if path is a directory
        if (is_directory(path)) {
            if (find_index_html(path))
                snprintf(full_path, sizeof(full_path), "%s/index.html", path);
            else {
                // case of directory listing
                strcpy(full_path, path);
                if (generate_directory_listing(path, &body, &is_allocated) != 0) {
                    send_internal_error_response(client_fd);
                    goto end;
                }
            }
        }
        else {
            // If it's a file, check MIME type
            strcpy(full_path, path);
            char* res;
            if ((res = get_mime_type(full_path)) == NULL)
                content_type[0] = '\0';
            else
                strcpy(content_type, res);

            if (open_read_file(&body, full_path, &is_allocated) != 0) {
                send_internal_error_response(client_fd);
                goto end;
            }
            DEBUG_PRINT("Content of the file %s: %s\n", full_path, body);
        }
        last_modified = get_last_modified_date(full_path);
    }

    char contentType_header[256];  // New buffer for the full header
    if (content_type[0] != '\0')
        snprintf(contentType_header, sizeof(contentType_header), "Content-Type: %s\r\n", content_type);

    char modify_header[256];  // New buffer for the full header
    if (last_modified){
        sprintf(modify_header, "Last-Modified: %s\r\n", last_modified);
    }

    // Construct the status line
    snprintf(status_line, sizeof(status_line), "HTTP/1.0 %s\r\n", status_phrase);

    // Construct headers
    snprintf(headers, sizeof(headers),
             "Server: webserver/1.0\r\n"
             "Date: %s"
             "%s"   // Location header if exists (302 response)
             "%s"   // Content-type if exists
             "Content-Length: %ld\r\n"
             "%s"   // Last modification date if exists (the body is a file or content of a directory)
             "Connection: close\r\n\r\n",
             timebuf,
             status_code == 302 ? location_header : "",
             content_type[0] != '\0' ? contentType_header : "",
             strlen(body),
             last_modified ? modify_header : "");

    size_t total_size = strlen(status_line) + strlen(headers) + strlen(body) + 1;
    char *response = (char*) malloc(total_size);
    if (response == NULL) {
        send_internal_error_response(client_fd);
        goto end;
    }

    // Construct the full response and write to client
    snprintf(response, total_size, "%s%s%s", status_line, headers, body);
    write_to_client(client_fd, response);

    end:
    if(is_allocated)
        free(body);

    return;
}

int open_read_file(char **body, char* path, int *is_allocated){
    // Open the file
    FILE *file = fopen(path, "rb");
    if (!file) {
        return -1;
    }
    // Get the file size
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);  // Rewind to the beginning of the file

    DEBUG_PRINT("File Size: %ld\n", file_size);
    // Dynamically allocate memory for the body buffer
    *body = (char *)malloc(file_size + 1);
    if (!*body) {
        fclose(file);
        return -1;
    }

    *is_allocated = 1;
    // Read the file into the body buffer
    fread(*body, 1, file_size, file);
    (*body)[file_size] = '\0';
    fclose(file);
    return 0;
}

void send_internal_error_response(int client_fd){
    char body[] = HTTP_500_BODY;
    char status_line[] = "HTTP/1.0 500 Internal Server Error";
    char headers[1024];
    char timebuf[128];
    get_current_date(timebuf, sizeof (timebuf));
    snprintf(headers, sizeof(headers),
             "Server: webserver/1.0\r\n"
             "Date: %s"
             "Content-Type: text/html\r\n"   // Content-type
             "Content-Length: %ld\r\n"
             "Connection: close\r\n\r\n",
             timebuf,
             strlen(body));

    size_t total_size = strlen(status_line) + strlen(headers) + strlen(body);
    char *response = (char*) malloc(total_size);
    if (response == NULL) {
        send_internal_error_response(client_fd);
        return;
    }
    snprintf(response, total_size, "%s%s%s", status_line, headers, body);
    write_to_client(client_fd, response);
    free(response);
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

void write_to_client(int client_fd, char* buffer){
    // Send the request
    size_t total_sent = 0;
    size_t buffer_len = strlen(buffer);
    while (total_sent < buffer_len) {
        ssize_t bytes_sent = write(client_fd, buffer + total_sent, buffer_len - total_sent);
        if (bytes_sent < 0) {
            free(buffer);
            buffer = HTTP_500_BODY;
            write_to_client(client_fd, buffer);
            return;
        }
        total_sent += bytes_sent;
    }
}

int generate_directory_listing(const char *dir_path, char **html_body, int* is_allocated) {
    size_t buffer_size = INITIAL_BUFFER_SIZE;
    size_t used_size = 0;
    *html_body = (char*)malloc(buffer_size);
    if (!*html_body){
        *html_body = HTTP_500_BODY;
        return -1;
    }

    *is_allocated = 1;  // signal that body pointer has been allocated
    DIR *dir;
    struct dirent *entry;
    struct stat file_stat;
    char full_path[1024];
    char time_buf[64];

    // Start building the HTML
    used_size += snprintf(*html_body + used_size, buffer_size - used_size,
                          "<HTML>\r\n"
                          "<HEAD><TITLE>Index of %s</TITLE></HEAD>\r\n"
                          "<BODY>\r\n"
                          "<H4>Index of %s</H4>\r\n"
                          "<table CELLSPACING=8>\r\n"
                          "<tr><th>Name</th><th>Last Modified</th><th>Size</th></tr>\r\n",
                          dir_path, dir_path);

    // Open the directory
    dir = opendir(dir_path);
    if (!dir) {
        strcpy(*html_body, HTTP_500_BODY);
        return -1;
    }

    // TODO: Check why readdir gets some empty files for some reason
    // Iterate through directory entries
    while ((entry = readdir(dir)) != NULL) {
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
                                   "<tr><td><A HREF=\"%s\">%s</A></td><td>%s</td><td>%s</td></tr>\r\n",
                                   entry->d_name, entry->d_name, time_buf, size_str);

        // Handle buffer overflow
        if (used_size + row_size + 1 > buffer_size) {
            buffer_size *= 2;
            char *new_html = realloc(*html_body, buffer_size);
            if (!new_html) {
                strcpy(*html_body, HTTP_500_BODY);
                return -1;
            }
            *html_body = new_html;
        }

        // Add the row to the buffer
        used_size += snprintf(*html_body + used_size, buffer_size - used_size,
                              "<tr><td><A HREF=\"%s\">%s</A></td><td>%s</td><td>%s</td></tr>\r\n",
                              entry->d_name, entry->d_name, time_buf, size_str);

    }

    closedir(dir);

    // Add footer
    if (used_size + 64 > buffer_size) {
        buffer_size += 64;
        char *new_html = realloc(*html_body, buffer_size);
        if (!new_html) {
            strcpy(*html_body, HTTP_500_BODY);
            return -1;
        }
        *html_body = new_html;
    }

    used_size += snprintf(*html_body + used_size, buffer_size - used_size,
                          "</table>\r\n<HR>\r\n<ADDRESS>webserver/1.0</ADDRESS>\r\n</BODY></HTML>\r\n");

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
    struct stat path_stat;
    stat(path, &path_stat);
    return S_ISDIR(path_stat.st_mode);
}