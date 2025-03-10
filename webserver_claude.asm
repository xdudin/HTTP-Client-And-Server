section .data
; HTTP response header
http_header db "HTTP/1.1 200 OK",0x0d,0x0a,  \
              "Content-Type: text/html",0x0d,0x0a, \
              "Connection: close",0x0d,0x0a,0x0d,0x0a
header_len  equ $ - http_header

; File to serve
filename    db "index.html", 0

; SO_REUSEADDR option value
reuse_addr  dd 1

; sockaddr_in structure (16 bytes) for AF_INET, port 8080 (network byte order) and INADDR_ANY
sockaddr:
    dw 2, 0x1F90      ; AF_INET, port 8080
    dd 0              ; INADDR_ANY
    times 8 db 0      ; zero padding

section .bss
buffer      resb 4096  ; Buffer for file content

section .text
global _start

_start:
    ; socket(AF_INET, SOCK_STREAM, 0)
    mov rax, 41
    mov rdi, 2
    mov rsi, 1
    xor rdx, rdx
    syscall
    test rax, rax
    js exit_error
    mov r15, rax         ; Save socket fd in r15

    ; setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse_addr, 4)
    mov rax, 54
    mov rdi, r15
    mov rsi, 1           ; SOL_SOCKET
    mov rdx, 2           ; SO_REUSEADDR
    lea r10, [rel reuse_addr]
    mov r8, 4
    syscall

    ; bind(sock, &sockaddr, 16)
    mov rax, 49
    mov rdi, r15
    lea rsi, [rel sockaddr]
    mov rdx, 16
    syscall
    test rax, rax
    js exit_error

    ; listen(sock, 10)
    mov rax, 50
    mov rdi, r15
    mov rsi, 10
    syscall
    test rax, rax
    js exit_error

accept_loop:
    ; accept(sock, NULL, NULL)
    mov rax, 43
    mov rdi, r15
    xor rsi, rsi
    xor rdx, rdx
    syscall
    test rax, rax
    js accept_loop
    mov r14, rax         ; Save client fd in r14

    ; open("index.html", O_RDONLY)
    mov rax, 2
    lea rdi, [rel filename]
    xor rsi, rsi
    syscall
    test rax, rax
    js close_client
    mov r13, rax         ; Save file fd in r13

    ; write HTTP header to client
    mov rax, 1
    mov rdi, r14
    lea rsi, [rel http_header]
    mov rdx, header_len
    syscall

    ; read file content into buffer
    mov rax, 0
    mov rdi, r13
    lea rsi, [rel buffer]
    mov rdx, 4096
    syscall
    test rax, rax
    jle close_file

    ; write file content to client
    mov rdx, rax         ; bytes read
    mov rax, 1
    mov rdi, r14
    lea rsi, [rel buffer]
    syscall

close_file:
    ; Close file
    mov rax, 3
    mov rdi, r13
    syscall

close_client:
    ; Close client connection
    mov rax, 3
    mov rdi, r14
    syscall
    jmp accept_loop

exit_error:
    ; Exit with error code
    mov rax, 60
    mov rdi, 1
    syscall
