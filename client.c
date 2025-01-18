#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <stdbool.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>

// ----- DEBUG -----
// Uncomment the following for debugging
// #include "tests.c"
// #define DEBUG
// ----- DEBUG -----

// Return codes
#define SUCCESS 0
#define INVALID_FORMAT 1
#define MEMORY_ERROR 2

// Struct for representing a parsed URL
typedef struct URL {
    char *domain;  // Domain name
    int port;      // Port number
    char *path;    // Path in the URL
} URL;

// Function Prototypes
int send_http_request(URL url, char *query_string);
unsigned char *read_http_response(int sock);
int parse_console(int argc, char *argv[], URL *url, char **query_string);
char *strcasestr_custom(const char *haystack, const char *needle);
void print_usage();
bool isInteger(const char *str, int *result);
int build_query_string(char *argv[], int n, int index, char **result);
int validate_and_parse_url(char *url, URL *url_struct);
void free_pointers(unsigned char **response, char **query_string, char **location);
int check_redirection(unsigned char *response, char **result);

int main(int argc, char *argv[]) {
#ifdef DEBUG
    printf("[!] Debug Mode\n");
    FILE *fp;
    int dup_stdout = redirect_stdout(&fp, "response.txt");
#endif

    int status;
    URL url = {};
    char *query_string = "";

    // Parse console input and initialize URL and query string
    status = parse_console(argc, argv, &url, &query_string);
    if (status == MEMORY_ERROR || status == INVALID_FORMAT) {
        free_pointers(NULL, &query_string, NULL);
        exit(EXIT_FAILURE);
    }

    unsigned char *response = NULL;
    int sockfd;
    char *location = NULL;
    char *temp_location = NULL;

    while (true) {
        // Send HTTP request
        sockfd = send_http_request(url, query_string);
        if (sockfd < 0) {
            free_pointers(&response, &query_string, &location);
            exit(EXIT_FAILURE);
        }

        // Read HTTP response
        response = read_http_response(sockfd);
        if (response == NULL) {
            free_pointers(&response, &query_string, &location);
            exit(EXIT_FAILURE);
        }

        // Handle potential redirection
        if (temp_location) {
            free(temp_location);
            temp_location = NULL;
        }
        status = check_redirection(response, &location);
        if (status == MEMORY_ERROR) {
            free_pointers(&response, &query_string, &location);
            exit(EXIT_FAILURE);
        } else if (status == SUCCESS) {
            // Process redirection
            temp_location = realloc(temp_location, strlen(location) + 1);
            if (temp_location == NULL) {
                free(temp_location);
                free_pointers(&response, &query_string, &location);
                exit(EXIT_FAILURE);
            }
            strcpy(temp_location, location);

            // Update URL and query string
            if (strncmp(temp_location, "http://", 7) == 0)
                validate_and_parse_url(temp_location, &url);
            else
                url.path = temp_location[0] == '/' ? temp_location + 1 : temp_location;

            free_pointers(&response, &query_string, &location);
            query_string = "";
            continue;
        } else {
            break;  // No redirection
        }
    }

    close(sockfd);
    free_pointers(&response, &query_string, &location);

#ifdef DEBUG
    restore_stdout(dup_stdout);
#endif

    return 0;
}

/**
 * Sends an HTTP GET request to the specified URL.
 *
 * @param url The URL structure containing domain, port, and path.
 * @param query_string The query string to append to the path.
 * @return Socket file descriptor on success, -1 on failure.
 */
int send_http_request(URL url, char *query_string) {
    // Calculate the size of the HTTP request string
    int size = snprintf(NULL, 0, "GET /%s%s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n",
                        url.path, query_string, url.domain);
    char *request = (char *)malloc(size + 1);
    if (!request) {
        perror("malloc");
        return -1;
    }

    sprintf(request, "GET /%s%s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n",
            url.path, query_string, url.domain);

    printf("HTTP request =\n%s\nLEN = %d\n", request, (int)strlen(request));

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

    // Resolve hostname
    server = gethostbyname(url.domain);
    if (!server) {
        herror("gethostbyname");
        close(sock);
        free(request);
        return -1;
    }

    // Setup server socket structure
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(url.port);
    server_addr.sin_addr.s_addr = ((struct in_addr *)server->h_addr_list[0])->s_addr;

    // Connect to the server
    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("connect");
        close(sock);
        free(request);
        return -1;
    }

    // Send the request
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

/**
 * Reads the HTTP response from the socket.
 *
 * @param sock The socket file descriptor.
 * @return Pointer to the response buffer, NULL on failure.
 */
unsigned char *read_http_response(int sock) {
    unsigned char *response = NULL;
    ssize_t total = 0, received = 0;
    unsigned char buffer[1024];

    while (true) {
        memset(buffer, 0, sizeof(buffer));
        received = read(sock, buffer, sizeof(buffer));
        if (received <= 0)
            break;

        response = realloc(response, total + received + 1);
        if (response == NULL) {
            perror("realloc");
            close(sock);
            free(response);
            return NULL;
        }

        memcpy(response + total, buffer, received);
        total += received;
    }

    if (received < 0) {
        perror("read");
        close(sock);
        free(response);
        return NULL;
    }

    if (response) {
        response[total] = '\0';
    }

    for (int i = 0; i < total; i++)
        printf("%c", response[i]);
    printf("\n Total received response bytes: %d\n", (int)total);

    close(sock);
    return response;
}

/**
 * Parses command-line arguments and initializes the URL structure and query string.
 *
 * @param argc Number of arguments.
 * @param argv Argument array.
 * @param url Pointer to a URL structure to initialize.
 * @param query_string Pointer to the query string to initialize.
 * @return SUCCESS on success, MEMORY_ERROR or INVALID_FORMAT on failure.
 */
int parse_console(int argc, char *argv[], URL *url, char **query_string) {
    int status;
    int n = 0;

    // Check if the first argument is an option
    switch (argv[1][0] == '-') {
        case 1: // Query arguments come first
            if (argv[1][1] != 'r') {
                print_usage();
                return INVALID_FORMAT;
            }
            // Validate and parse number after -r
            if (!isInteger(argv[2], &n)) {
                print_usage();
                return INVALID_FORMAT;
            }

            // Build the query string
            status = build_query_string(argv, n, 3, query_string);
            if (status != SUCCESS) {
                return status;
            }

            // Validate and parse the URL
            if (validate_and_parse_url(argv[argc - 1], url) != 0) {
                print_usage();
                return INVALID_FORMAT;
            }
            break;

        case 0: // URL comes first
            if (validate_and_parse_url(argv[1], url) != 0) {
                print_usage();
                return INVALID_FORMAT;
            }

            // Handle optional query arguments
            if (argv[2] != NULL && argv[2][0] == '-') {
                if (argv[2][1] != 'r') {
                    print_usage();
                    return INVALID_FORMAT;
                }
                if (!isInteger(argv[3], &n)) {
                    print_usage();
                    return INVALID_FORMAT;
                }
                status = build_query_string(argv, n, 4, query_string);
                if (status != SUCCESS) {
                    return status;
                }
            }
            break;
    }
    return SUCCESS;
}

/**
 * Case-insensitive substring search.
 *
 * @param haystack The string to search within.
 * @param needle The substring to search for.
 * @return Pointer to the start of the substring if found, NULL otherwise.
 */
char *strcasestr_custom(const char *haystack, const char *needle) {
    if (!*needle) return (char *)haystack;

    for (; *haystack; haystack++) {
        const char *h = haystack;
        const char *n = needle;

        while (*h && *n && tolower(*h) == tolower(*n)) {
            h++;
            n++;
        }
        if (!*n) return (char *)haystack;
    }

    return NULL;
}

/**
 * Checks if the response indicates a redirection and extracts the location.
 *
 * @param response The HTTP response buffer.
 * @param result Pointer to store the redirection location, if any.
 * @return SUCCESS if redirection found, 1 if not, MEMORY_ERROR on allocation failure.
 */
int check_redirection(unsigned char *response, char **result) {
    if (strncmp((const char *)response, "HTTP/1.1 3", 10) == 0) {
        char *loc_start = strcasestr_custom((const char *)response, "location:");
        if (loc_start) {
            loc_start += 9; // Skip "location:"
            while (isspace((unsigned char)*loc_start)) loc_start++; // Skip leading whitespaces

            const char *loc_end = loc_start;
            while (*loc_end && *loc_end != '\r' && *loc_end != '\n') loc_end++; // Find end of the line

            size_t loc_length = loc_end - loc_start;
            *result = (char *)malloc(loc_length + 1);
            if (*result == NULL) {
                return MEMORY_ERROR;
            }

            strncpy(*result, loc_start, loc_length);
            (*result)[loc_length] = '\0';
            return SUCCESS;
        }
        return 1; // No "location" header found
    }
    return 1; // Not a redirection status code
}

/**
 * Frees allocated pointers if they are not NULL.
 *
 * @param response Pointer to the HTTP response buffer.
 * @param query_string Pointer to the query string.
 * @param location Pointer to the redirection location.
 */
void free_pointers(unsigned char **response, char **query_string, char **location) {
    if (query_string != NULL && *query_string != NULL && strlen(*query_string) > 0) {
        free(*query_string);
        *query_string = NULL;
    }

    if (response != NULL && *response != NULL) {
        free(*response);
        *response = NULL;
    }

    if (location != NULL && *location != NULL) {
        free(*location);
        *location = NULL;
    }
}

/**
 * Prints usage instructions for the program.
 */
void print_usage() {
    printf("Usage: client [-r n <pr1=value1 pr2=value2 …>] <URL>\n");
}

/**
 * Validates and parses a URL into its components.
 *
 * @param url The input URL string.
 * @param url_struct Pointer to the URL structure to populate.
 * @return 0 on success, -1 on failure.
 */
int validate_and_parse_url(char *url, URL *url_struct) {
    url_struct->port = 80; // Default HTTP port

    // Check if URL starts with "http://"
    char *prefix = "http://";
    if (strncmp(url, prefix, strlen(prefix)) != 0) {
        return -1;
    }

    // Skip "http://"
    char *domain_start = url + strlen(prefix);
    char *p = domain_start;

    // Extract domain
    while (*p && *p != ':' && *p != '/' && *p != '\0') {
        p++;
    }
    if (p == domain_start) {
        return -1; // Empty domain
    }

    int domain_length = p - domain_start;

    // Check for port
    if (*p == ':') {
        p++;
        const char *port_start = p;

        // Ensure port is numeric
        while (isdigit(*p)) p++;

        if (port_start == p || (*p != '/' && *p != '\0')) {
            return -1;
        }

        int port = atoi(port_start);
        if (port <= 0 || port >= 65536) {
            return -1;
        }
        url_struct->port = port;
    }

    // Check for path
    if (*p == '/') {
        url_struct->path = p + 1;
    } else {
        url_struct->path = "";
    }

    url_struct->domain = domain_start;
    url_struct->domain[domain_length] = '\0';

    return 0;
}

/**
 * Constructs a query string from command-line arguments.
 *
 * @param argv Argument array.
 * @param n Number of query parameters.
 * @param index Start index of query parameters in argv.
 * @param result Pointer to store the constructed query string.
 * @return SUCCESS on success, MEMORY_ERROR or INVALID_FORMAT on failure.
 */
int build_query_string(char *argv[], int n, int index, char **result) {
    if (n == 0)
        return SUCCESS;

    int end_of_params_index = index + n;

    // Calculate buffer size
    size_t total_len = 1; // Start with 1 for the '?'
    for (int i = index; i < end_of_params_index; i++) {
        if (argv[i] == NULL || strstr(argv[i], "http://") != NULL)
            return INVALID_FORMAT;
        total_len += strlen(argv[i]);
        if (i < end_of_params_index - 1) {
            total_len++; // Add 1 for '&'
        }
    }
    total_len++; // Null terminator

    *result = (char *)malloc(sizeof(char) * total_len);
    if (*result == NULL) {
        perror("malloc");
        return MEMORY_ERROR;
    }

    // Build query string
    (*result)[0] = '?';
    (*result)[1] = '\0';

    for (int i = index; i < end_of_params_index; i++) {
        if (strchr(argv[i], '=') == NULL)
            return INVALID_FORMAT;
        strcat(*result, argv[i]);
        if (i < end_of_params_index - 1) {
            strcat(*result, "&");
        }
    }
    return SUCCESS;
}

/**
 * Validates if a string is an integer and converts it.
 *
 * @param str Input string.
 * @param result Pointer to store the integer value.
 * @return true if valid integer, false otherwise.
 */
bool isInteger(const char *str, int *result) {
    if (str == NULL || *str == '\0')
        return false;

    const char *ptr = str;
    while (*ptr) {
        if (!isdigit(*ptr))
            return false;
        ptr++;
    }

    *result = atoi(str);
    return true;
}

