#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <libgen.h>
#include <dirent.h>
#include <fcntl.h>

#define INITIAL_BUFFER_SIZE 8192
#define FIRST_LINE_SIZE 4000
#define PORT 8080
#define MAX_REQUESTS 15

// Function prototypes
int handle_client(void *arg);
char *build_http_response(int status_code, char *path, int *error);
void write_to_client(int client_fd, char *buffer);

int parse_request(char *first_line, char *path);
int read_and_write(int client_fd, char *path);
int generate_directory_listing(char *dir_path, char **body);
int find_index_html(const char *dir_path);
int has_execute_permissions(const char *path);
int is_directory(const char *path);

int get_file_size(char *path);
char *get_mime_type(char *name);
char *get_last_modified_date(const char *path);

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

    int requestsCounter = 0;
    while(requestsCounter < MAX_REQUESTS) {

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

        requestsCounter++;
    }

    // Close the connections
    close(server_fd);
    return 0;
}

// Client handler
int handle_client(void *arg) {
    const int client_fd = *(int *)arg;
    free(arg);
    size_t total_bytes_read = 0;
    ssize_t bytes_read;
    int first_line_found = 0;
    char* end_of_line;
    char first_line[FIRST_LINE_SIZE] = {0};
    char path[FIRST_LINE_SIZE] = {0};
    int status_code;
    int error = 0;
    char *response;

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
        status_code = 500;
        response = build_http_response(status_code, nullptr, &error);
        goto write;
    }
    else if(first_line_found){
        *end_of_line = '\0';
    }

    status_code = parse_request(first_line, path);

    // Construct response
    response = build_http_response(status_code, path, &error);

    write:
    // Write the response to client
    write_to_client(client_fd, response);

    const int is_file = (!error && status_code == 200 && !is_directory(path)) ? 1 : 0;

    if (is_file)
        read_and_write(client_fd, path);

    close(client_fd);
    free(response);

    return 0;
}

// Build HTTP response
char * build_http_response(int status_code, char* path, int *error) {
    const char* status_text;
    const char* custom_message;
    const char* error_template =
            "<HTML><HEAD><TITLE>%d %s</TITLE></HEAD>\r\n"
            "<BODY><H4>%d %s</H4>\r\n%s\r\n</BODY></HTML>";
    char error_body[256] = {0};
    const char* mime_type = nullptr;
    const char* last_modified = nullptr;
    char date_header[128];
    char *OK_body = nullptr;
    const int is_file = (status_code == 200 && !is_directory(path)) ? 1 : 0;
    const int is_dir = (status_code == 200 && is_directory(path)) ? 1 : 0;

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

    if (is_file)
        OK_body = "";
    else if (is_dir) {
        if (generate_directory_listing(path, &OK_body) < 0) {
            *error = 1;
            return build_http_response(500, nullptr, error);
        }
    }

    // Calculate the response size
    const size_t body_length = (status_code == 200 ? strlen(OK_body) : strlen(error_body));
    const int response_size = 256 + body_length; // Headers + body

    // Allocate memory for the response
    char *response = (char*) malloc(response_size);
    if (!response) {
        *error = 1;
        return build_http_response(500, nullptr, error);
    }

    int written = 0;
    // Build the response headers
    written += snprintf(response, response_size,
             "HTTP/1.0 %d %s\r\n"
             "Server: webserver/1.0\r\n"
             "Date: %s\r\n",
             status_code, status_text, date_header);

    // Add Location header for 302 redirects
    if (status_code == 302) {
        written += snprintf(response + written, response_size - written,
                 "Location: /%s/\r\n", path);
    }

    // Add Content-Type header
    if (mime_type) {
        written += snprintf(response + written, response_size - written,
                 "Content-Type: %s\r\n", mime_type);
    }

    // Add Content-Length header
    const size_t cont_length = is_file ? get_file_size(path) : body_length;
    written += snprintf(response + written, response_size - written,
             "Content-Length: %lu\r\n",
             cont_length);

    // Add Last-Modified header for 200 responses
    if (status_code == 200 && last_modified) {
        written += snprintf(response + written, response_size - written, "Last-Modified: %s\r\n", last_modified);
    }

    // Add Connection header
    written += snprintf(response + written, response_size - written, "Connection: close\r\n\r\n");

    // Add the body to the response
    char *src = (status_code == 200) ? OK_body : error_body;
    written += snprintf(response + written, response_size - written, "%s", src);

    if (OK_body && strlen(OK_body) > 0)
        free(OK_body);
    return response;
}

// Write response to client
void write_to_client(int client_fd, char *buffer){
    // Send the request
    size_t total_sent = 0;
    size_t buffer_len = strlen(buffer);
    while (total_sent < buffer_len) {
        ssize_t bytes_sent = write(client_fd, buffer + total_sent, buffer_len - total_sent);
        if (bytes_sent < 0) {
            int error = 1;
            char *response = build_http_response(500, nullptr, &error);
            write_to_client(client_fd, response);
            free(response);
            return;
        }
        total_sent += bytes_sent;
    }
}

// Helper functions
int parse_request(char* first_line, char* path){
    char method[FIRST_LINE_SIZE] = {0};
    char version[FIRST_LINE_SIZE] = {0};
    // Check if the request line contains 3 parts
    if (sscanf(first_line, "%s %s %s", method, path, version) != 3)
        return 400;
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
        if (stat(dir, &dir_stat) != 0 || !(dir_stat.st_mode & S_IXOTH)) {
            return 403;  // Directory not executable
        }
        dir = dirname(dir);
    }

    return 200;  // OK - will serve file
}

int read_and_write(int client_fd, char *path){
    // Open the file
    int file_fd = open(path, O_RDONLY);
    if(file_fd < 0)
        return -1;

    char buffer[4096];
    size_t bytesRead;
    // Read the file into the buffer and write to client
    while ((bytesRead = read(file_fd, buffer, sizeof(buffer))) > 0) {
        write(client_fd, buffer, bytesRead); // Write the data
    }

    if (bytesRead < 0) {
        close(file_fd);
        return -1;
    }

    close(file_fd);
    return 0;
}

int get_file_size(char *path) {
    FILE *file = fopen(path, "r");
    if (!file) {
        return -1;
    }
    // Get the file size
    fseek(file, 0, SEEK_END);
    int size = ftell(file);
    fseek(file, 0, SEEK_SET);  // Rewind to the beginning of the file

    fclose(file);
    return size;
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

int generate_directory_listing(char *dir_path, char **body) {
    size_t buffer_size = INITIAL_BUFFER_SIZE;
    size_t used_size = 0;
    *body = (char*)malloc(buffer_size);
    if (!*body){
        return -1;
    }

    DIR *dir;
    struct dirent *entry;
    struct stat file_stat;
    char full_path[1024];
    char time_buf[128];

    // Start building the HTML
    used_size += snprintf(*body + used_size, buffer_size - used_size,
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
        free(*body);
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

        // Extract size of file
        const char *size_str = S_ISREG(file_stat.st_mode) ?
                               (sprintf(full_path, "%ld", file_stat.st_size), full_path) : "";

        // Handle directory entries with concatenating trailing /
        if (S_ISDIR(file_stat.st_mode))
            strcat(entry->d_name, "/");

        // Estimate required size for the new row
        size_t row_size = snprintf(nullptr, 0,
                                   "<tr>\r\n<td><A HREF=\"%s\">%s</A></td><td>%s</td>\r\n<td>%s</td>\r\n</tr>\r\n\r\n",
                                   entry->d_name, entry->d_name, time_buf, size_str);

        // Handle buffer overflow
        if (used_size + row_size + 1 > buffer_size) {
            buffer_size *= 2;
            char *new_html = realloc(*body, buffer_size);
            if (!new_html) {
                free(*body);
                return -1;
            }
            *body = new_html;
        }

        // Add the row to the buffer
        used_size += snprintf(*body + used_size, buffer_size - used_size,
                              "<tr>\r\n<td><A HREF=\"%s\">%s</A></td><td>%s</td>\r\n<td>%s</td>\r\n</tr>\r\n\r\n",
                              entry->d_name, entry->d_name, time_buf, size_str);

    }

    closedir(dir);

    // Add footer
    if (used_size + 64 > buffer_size) {
        buffer_size += 64;
        char *new_html = realloc(*body, buffer_size);
        if (!new_html) {
            free(*body);
            return -1;
        }
        *body = new_html;
    }

    used_size += snprintf(*body + used_size, buffer_size - used_size,
                          "</table>\r\n\r\n<HR>\r\n\r\n<ADDRESS>webserver/1.0</ADDRESS>\r\n\r\n</BODY></HTML>\r\n\r\n");

    strcpy(dir_path, dir_path[0] == '\0' ? "/" : dir_path);
    return 0;
}

char *get_last_modified_date(const char *path) {
    static char timebuf[128];
    struct stat file_stat;

    if (stat(path, &file_stat) == -1)
        return nullptr;

    return timebuf;
}

int is_directory(const char *path) {
    if (path)
        return path[strlen(path) - 1] == '/';
    return -1;
}
