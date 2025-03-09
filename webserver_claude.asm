; Minimal HTTP webserver in x86_64 assembly for Linux
; Serves index.html to any request
; No console output
; To assemble and link:
; nasm -f elf64 webserver.asm -o webserver.o
; ld webserver.o -o webserver

section .data
    ; Socket settings
    PORT        equ 8080                ; Port to listen on
    BACKLOG     equ 10                  ; Maximum connection backlog

    ; HTTP response header template
    http_header db "HTTP/1.1 200 OK", 0Dh, 0Ah
                db "Content-Type: text/html", 0Dh, 0Ah
                db "Connection: close", 0Dh, 0Ah
                db 0Dh, 0Ah
    header_len  equ $ - http_header

    ; File to serve
    filename    db "index.html", 0

section .bss
    sockfd      resq 1                  ; Socket file descriptor
    clientfd    resq 1                  ; Client connection file descriptor
    addr_in     resb 16                 ; sockaddr_in structure (16 bytes)
    file_buffer resb 8192               ; Buffer for file content (8KB)
    buffer      resb 1024               ; Buffer for HTTP request
    file_size   resq 1                  ; Size of the file

section .text
    global _start

_start:
    ; Create socket
    mov rax, 41                         ; syscall: socket
    mov rdi, 2                          ; AF_INET
    mov rsi, 1                          ; SOCK_STREAM
    mov rdx, 0                          ; protocol
    syscall

    ; Check for error
    cmp rax, 0
    jl exit_error

    ; Save socket descriptor
    mov [sockfd], rax

    ; Set up sockaddr_in structure
    mov word [addr_in], 2               ; AF_INET
    mov ax, PORT
    xchg ah, al                         ; Convert to network byte order
    mov word [addr_in + 2], ax          ; Port
    mov dword [addr_in + 4], 0          ; INADDR_ANY (0.0.0.0)

    ; Bind socket
    mov rax, 49                         ; syscall: bind
    mov rdi, [sockfd]
    mov rsi, addr_in
    mov rdx, 16                         ; sizeof(sockaddr_in)
    syscall

    ; Check for error
    cmp rax, 0
    jl exit_error

    ; Listen for connections
    mov rax, 50                         ; syscall: listen
    mov rdi, [sockfd]
    mov rsi, BACKLOG
    syscall

    ; Check for error
    cmp rax, 0
    jl exit_error

accept_loop:
    ; Accept connection
    mov rax, 43                         ; syscall: accept
    mov rdi, [sockfd]
    mov rsi, 0                          ; NULL addr
    mov rdx, 0                          ; NULL addrlen
    syscall

    ; Check for error
    cmp rax, 0
    jl accept_loop                      ; Just try again on error

    ; Save client descriptor
    mov [clientfd], rax

    ; Read HTTP request (we don't actually need to process it)
    mov rax, 0                          ; syscall: read
    mov rdi, [clientfd]
    mov rsi, buffer
    mov rdx, 1024
    syscall

    ; Open index.html
    mov rax, 2                          ; syscall: open
    mov rdi, filename
    mov rsi, 0                          ; O_RDONLY
    mov rdx, 0
    syscall

    ; Check for error
    cmp rax, 0
    jl close_client                     ; Skip to closing client if file error

    ; Save file descriptor
    mov r8, rax                         ; Use r8 for file descriptor

    ; Read file content
    mov rax, 0                          ; syscall: read
    mov rdi, r8
    mov rsi, file_buffer
    mov rdx, 8192
    syscall

    ; Save file size
    mov [file_size], rax

    ; Close file
    mov rax, 3                          ; syscall: close
    mov rdi, r8
    syscall

    ; Send HTTP header
    mov rax, 1                          ; syscall: write
    mov rdi, [clientfd]
    mov rsi, http_header
    mov rdx, header_len
    syscall

    ; Send file content
    mov rax, 1                          ; syscall: write
    mov rdi, [clientfd]
    mov rsi, file_buffer
    mov rdx, [file_size]
    syscall

close_client:
    ; Close client connection
    mov rax, 3                          ; syscall: close
    mov rdi, [clientfd]
    syscall

    ; Loop back to accept more connections
    jmp accept_loop

exit_error:
    ; Exit with error code
    mov rax, 60                         ; syscall: exit
    mov rdi, 1                          ; error code 1
    syscall