#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <stdbool.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>

// ----- DEBUG -----
// curl command to save the response to a file
// curl -D headers.txt http://example.com -o body.txt
//#include "tests.c"
//#define DEBUG
// ----- DEBUG -----

#define SUCCESS 0
#define INVALID_FORMAT 1
#define MEMORY_ERROR 2

typedef struct URL{
    char* domain;
    int port;
    char* path;
}URL;

int send_http_request(URL url, char* query_string);
unsigned char* read_http_response(int sock);
int parse_console(int argc, char* argv[], URL* url, char** query_string);
char* strcasestr_custom(const char* haystack, const char* needle);
void print_usage();
bool isInteger (const char *str, int *result);
int build_query_string(char* argv[], int n, int index, char** result);
int validate_and_parse_url(char *url, URL* url_struct);
void free_pointers(unsigned char** response, char** query_string, char** location);
int check_redirection(unsigned char* response, char** result);

int main(int argc, char* argv[]) {

#ifdef DEBUG
    printf("[!] Debug Mode\n");

    FILE* fp;
    int dup_stdout = redirect_stdout(&fp, "response.txt");
#endif

    int status;
    URL url = {};
    char *query_string = "";

    status = parse_console(argc, argv, &url, &query_string);
    if(status == MEMORY_ERROR || status == INVALID_FORMAT) {
        free_pointers(NULL, &query_string, NULL);
        exit(EXIT_FAILURE);
    }

    unsigned char* response = NULL;
    int sockfd;
    char *location = NULL;
    char* temp_location = NULL;
    while(true) {
        sockfd = send_http_request(url, query_string);
        if(sockfd < 0){
            free_pointers(&response, &query_string, &location);
            exit(EXIT_FAILURE);
        }
        response = read_http_response(sockfd);
        if(response == NULL){
            free_pointers(&response, &query_string, &location);
            exit(EXIT_FAILURE);
        }

        if(temp_location){
            free(temp_location);
            temp_location = NULL;
        }
        status = check_redirection(response, &location);
        if (status == MEMORY_ERROR) {
            // Memory allocation failure
            free_pointers(&response, &query_string, &location);
            exit(EXIT_FAILURE);
        }
        else if(status == SUCCESS){
            // Found redirection status code with the location header
            temp_location = realloc(temp_location, strlen(location) + 1);
            if(temp_location == NULL){
                free(temp_location);
                free_pointers(&response, &query_string, &location);
                exit(EXIT_FAILURE);
            }
            strcpy(temp_location, location);
            //temp_location[strlen(location)] = '\0';

            if (strncmp(temp_location, "http://", 7) == 0)
                validate_and_parse_url(temp_location, &url);
            else
                url.path = temp_location[0] == '/' ? temp_location + 1 : temp_location;

            free_pointers(&response, &query_string, &location);
            query_string = "";
            continue;
        }
        else {
            // Not a redirection status code
            break;
        }
    }

    close(sockfd);
    free_pointers(&response, &query_string, &location);

#ifdef DEBUG
    restore_stdout(dup_stdout);
#endif

    return 0;
}

int send_http_request(URL url, char* query_string){
    // Calculate the size required for the request string
    int size = snprintf(NULL, 0, "GET /%s%s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n", url.path, query_string,
                        url.domain);

    char* request = (char*) malloc(size + 1); // +1 for null terminator
    if(!request){
        perror("malloc");
        return -1;
    }

    // null-terminate the request buffer
    memset(request, 0, strlen(request));

    sprintf(request, "GET /%s%s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n", url.path, query_string,
            url.domain);

    printf("HTTP request =\n%s\nLEN = %d\n", request, (int) strlen(request));

    int sock;
    struct sockaddr_in server_addr;
    struct hostent *server;

    // Create socket
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("socket");
        free(request);
        return -1;
    }

    // Get server information - apply dns
    server = gethostbyname(url.domain);
    if (!server) {
        herror("gethostbyname");
        close(sock);
        free(request);
        return -1;
    }

    // Setup server socket structure
    memset(&server_addr, 0, sizeof(server_addr));
    // assign the format of the ip address we use with this socket
    server_addr.sin_family = AF_INET;
    // assign the port number in network byte order
    server_addr.sin_port = htons(url.port);
    // assign the ip address of the server
    server_addr.sin_addr.s_addr = ((struct in_addr *) server->h_addr_list[0])->s_addr;

    if (connect(sock, (struct sockaddr *) &server_addr, sizeof(server_addr)) < 0) {
        perror("connect");
        close(sock);
        free(request);
        return -1;
    }

    size_t total_sent = 0;
    size_t request_len = strlen(request);

    while (total_sent < request_len) {
        ssize_t bytes_sent = write(sock, request + total_sent, request_len - total_sent);
        if (bytes_sent < 0) {
            perror("write");
            close(sock);
            free(request);
            return -1;
        }
        total_sent += bytes_sent;
    }

    free(request);
    return sock;
}

unsigned char* read_http_response(int sock){
    unsigned char* response = NULL;
    ssize_t total = 0;
    ssize_t received = 0;
    unsigned char buffer[1024];

    while(true) {
        // null terminate the chunk buffer
        memset(buffer, 0, sizeof(buffer));
        // read the response_file in chunks
        received = read(sock, buffer, sizeof(buffer));
        // allocate memory for the response_file
        if(received <= 0)
            break;
        response = realloc(response, total + received + 1);
        if (response == NULL) {
            perror("realloc");
            close(sock);
            free(response);
            response = NULL;
            return response;
        }
        // copy the chunk to the response_file buffer
        memcpy(response + total, buffer, received);
        // update the total number of bytes read
        total += received;
    }

    // if received is -1, then there was an error
    if (received < 0) {
        perror("read");
        close(sock);
        free(response);
        response = NULL;
        return response;
    }

    // Null-terminate the response buffer
    if (response != NULL) {
        response[total] = '\0';
    }

    printf("%s", response);
    printf("\n Total received response_file bytes: %d\n", (int) total);

    close(sock);

    return response;
}

int parse_console(int argc, char* argv[], URL* url, char** query_string){
    int status;
    int n = 0;

    switch (argv[1][0] == '-') {
        case 1:
            // query arguments come first
            if (argv[1][1] != 'r') {
                print_usage();
                return INVALID_FORMAT;
            }
            // must have argument n after -r:
            if (!isInteger(argv[2], &n)) {
                print_usage();
                return INVALID_FORMAT;
            }

            status = build_query_string(argv, n, 3, query_string);
            switch (status) {
                case MEMORY_ERROR:
                    perror("malloc");
                    return MEMORY_ERROR;
                case INVALID_FORMAT:
                    print_usage();
                    return INVALID_FORMAT;
            }

            if (validate_and_parse_url(argv[argc - 1], url) != 0) {
                print_usage();
                return INVALID_FORMAT;
            }
            break;

        case 0:
            // url come first
            if (validate_and_parse_url(argv[1], url) != 0) {
                print_usage();
                return INVALID_FORMAT;
            }

            if (argv[2] != NULL && argv[2][0] == '-') {
                // must have r after -
                if (argv[2][1] != 'r') {
                    print_usage();
                    return INVALID_FORMAT;
                }

                // must have argument n after -r:
                if (!isInteger(argv[3], &n)) {
                    print_usage();
                    return INVALID_FORMAT;
                }

                status = build_query_string(argv, n, 4, query_string);
                switch (status) {
                    case MEMORY_ERROR:
                        perror("malloc");
                        return MEMORY_ERROR;
                    case INVALID_FORMAT:
                        print_usage();
                        return INVALID_FORMAT;
                }
            }
            break;
    }
    return SUCCESS;
}

char* strcasestr_custom(const char* haystack, const char* needle){
    if (!*needle) return (char*)haystack;

    for(; *haystack; haystack++){
        const char* h = haystack;
        const char* n = needle;

        while (*h && *n && tolower(*h) == tolower(*n)){
            h++;
            n++;
        }
        if (!*n) return (char*)haystack;
    }

    return NULL;
}

int check_redirection(unsigned char* response, char** result){
    if (strncmp((const char*) response, "HTTP/1.1 3", 10) == 0){
        char* loc_start = strcasestr_custom((const char*) response, "location:");
        if(loc_start){
            loc_start += 9; // Skip "location:"
            while(isspace((unsigned char)*loc_start)) loc_start++; // Skip leading whitespaces

            const char* loc_end = loc_start;
            while(*loc_end && *loc_end != '\r' && *loc_end != '\n') loc_end++; // Find end of the header line

            size_t loc_length = loc_end - loc_start;
            *result = (char*) malloc(loc_length + 1);
            if(*result == NULL){
                return MEMORY_ERROR;
            }

            strncpy(*result, loc_start, loc_length);
            (*result)[loc_length] = '\0';
            return SUCCESS;
        }
        else
            return 1; // No header found
    }

    return 1; // Not a redirect status code
}

void free_pointers(unsigned char** response, char** query_string, char** location){
    if(query_string != NULL && (*query_string) != NULL && strlen(*query_string) > 0) {
        free(*query_string);
        *query_string = NULL;
    }

    if(response != NULL && (*response) != NULL) {
        free(*response);
        *response = NULL;
    }

    if(location != NULL && (*location) != NULL){
        free(*location);
        *location = NULL;
    }
}

void print_usage(){
    printf("Usage: client [-r n < pr1=value1 pr2=value2 …>] <URL>\n");
}

int validate_and_parse_url(char *url, URL* url_struct) {
    url_struct->port = 80; // Default HTTP port

    // Check if URL starts with "http://"
    char *prefix = "http://";
    if (strncmp(url, prefix, strlen(prefix)) != 0) {
        return -1;
    }

    // Skip "http://"
    char *domain_start = url + strlen(prefix);
    char *p = domain_start;

    // Read domain name
    while (*p && *p != ':' && *p != '/' && *p != '\0') {
        p++;
    }

    if (p == domain_start) {
        return -1;
    }

    int domain_length = p - domain_start;

    // Check for port
    if (*p == ':'){
        int port;
        p++;
        const char* port_start = p;

        // Ensure port is numeric
        while(isdigit(*p))
            p++;

        if (port_start == p || (*p != '/' && *p != '\0')) {
            return -1;
        }

        port = atoi(port_start);
        if (port <= 0 || port >= 65536) {
            return -1;
        }
        url_struct->port = port;
    }

    // Check for path
    if(*p == '/'){
        url_struct->path = p + 1;
    }
    else
        url_struct->path = "";

    url_struct->domain = domain_start;
    url_struct->domain[domain_length] = '\0';

    return 0;
}

int build_query_string(char* argv[], int n, int index, char** result){
    if(n == 0)
        return SUCCESS;

    int end_of_params_index = index + n;

    // Calculate required buffer size
    size_t total_len = 1;  // Start with 1 for the '?'
    for (int i = index; i < end_of_params_index; i++) {  // Start from the given index
        if (argv[i] == NULL || strstr(argv[i], "http://") != NULL)
            // few too many arguments than specified
            return INVALID_FORMAT;
        total_len += strlen(argv[i]);
        if (i < end_of_params_index - 1) {
            total_len++;  // Add 1 for '&' separator
        }
    }
    total_len++;  // Add 1 for null terminator

    *result = (char*)malloc(sizeof(char) * total_len);
    if (*result == NULL) {
        perror("malloc");
        return MEMORY_ERROR;
    }

    // Initialize first character and null terminate
    (*result)[0] = '?';
    (*result)[1] = '\0';

    // Build the query string
    for (int i = index; i < end_of_params_index; i++) {
        if(strchr(argv[i], '=') == NULL)
            return INVALID_FORMAT;
        strcat(*result, argv[i]);
        if (i < end_of_params_index - 1) {
            strcat(*result, "&");
        }
    }
    return SUCCESS;
}

bool isInteger (const char *str, int *result){
    if (str == NULL || *str == '\0')
        return false;

    // Check all characters are digits
    const char *ptr = str;
    while (*ptr) {
        if (!isdigit(*ptr))
            return false;
        ptr++;
    }

    // Convert to integer
    *result = atoi(str);
    return true;
}
