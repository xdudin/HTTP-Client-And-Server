section .data
    header db "HTTP/1.1 200 OK", 13, 10, "Content-Type: text/html", 13, 10, 13, 10
    header_len equ $ - header
    filename db "index.html", 0

section .bss
    buffer resb 4096

section .text
    global _start

_start:
    ; Create socket: socket(AF_INET, SOCK_STREAM, 0)
    mov rax, 41         ; syscall: socket
    mov rdi, 2          ; AF_INET = 2
    mov rsi, 1          ; SOCK_STREAM = 1
    mov rdx, 0          ; protocol 0
    syscall
    mov r12, rax        ; save socket fd in r12

    ; Prepare sockaddr_in structure (16 bytes) on the stack:
    ; struct sockaddr_in {
    ;   short sin_family;   // AF_INET (2)
    ;   unsigned short sin_port; // port in network byte order (htons(80) -> 0x5000 in little-endian)
    ;   struct in_addr sin_addr; // INADDR_ANY (0)
    ;   char sin_zero[8];   // zero-filled
    ; }
    sub rsp, 16         ; allocate 16 bytes on stack
    mov word [rsp], 2   ; sin_family = AF_INET
    mov word [rsp+2], 0x5000  ; sin_port = htons(80)
    mov dword [rsp+4], 0     ; sin_addr = INADDR_ANY (0)
    mov qword [rsp+8], 0     ; zero the rest

    ; Bind socket: bind(socket, sockaddr_in, 16)
    mov rax, 49         ; syscall: bind
    mov rdi, r12        ; socket fd
    mov rsi, rsp        ; pointer to sockaddr_in
    mov rdx, 16         ; size of sockaddr_in
    syscall
    add rsp, 16         ; restore stack pointer

    ; Listen on socket: listen(socket, backlog)
    mov rax, 50         ; syscall: listen
    mov rdi, r12
    mov rsi, 10        ; backlog of 10 connections
    syscall

accept_loop:
    ; Accept connection: accept(socket, NULL, NULL)
    mov rax, 43         ; syscall: accept
    mov rdi, r12
    xor rsi, rsi
    xor rdx, rdx
    syscall
    mov r13, rax        ; client socket fd in r13

    ; Send HTTP header: write(client_socket, header, header_len)
    mov rax, 1          ; syscall: write
    mov rdi, r13        ; client socket fd
    mov rsi, header
    mov rdx, header_len
    syscall

    ; Open index.html: open("index.html", O_RDONLY)
    mov rax, 2          ; syscall: open (read-only)
    lea rdi, [rel filename]
    xor rsi, rsi        ; flags = 0 (O_RDONLY)
    syscall
    mov r14, rax        ; file descriptor for index.html

read_file:
    ; Read file contents into buffer: read(file_fd, buffer, 4096)
    mov rax, 0          ; syscall: read
    mov rdi, r14        ; file descriptor
    lea rsi, [rel buffer]
    mov rdx, 4096
    syscall
    test rax, rax
    jle close_conn      ; if read <= 0, done reading

    ; Write file contents to client socket: write(client_socket, buffer, bytes_read)
    mov rdx, rax        ; number of bytes read
    mov rax, 1          ; syscall: write
    mov rdi, r13        ; client socket fd
    lea rsi, [rel buffer]
    syscall
    jmp read_file

close_conn:
    ; Close the client socket
    mov rax, 3          ; syscall: close
    mov rdi, r13
    syscall
    jmp accept_loop     ; wait for the next connection
