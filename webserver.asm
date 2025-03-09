; optimized_webserver.asm – Optimized minimal x86_64 assembly web server
; Compile: nasm -felf64 optimized_webserver.asm -o optimized_webserver.o
; Link: ld optimized_webserver.o -o optimized_webserver
; Run as root: sudo ./optimized_webserver

section .data
    hdr    db "HTTP/1.0 200 OK",13,10,13,10
    hdrlen equ $-hdr
    file   db "index.html",0
    ; Predefined sockaddr_in structure for AF_INET, port 80 (0x5000 in little-endian), INADDR_ANY:
    addr   dw 2,0x5000     ; sin_family, sin_port
           dd 0          ; sin_addr (0 for INADDR_ANY)
           dq 0          ; padding

section .bss
    buf    resb 4096

section .text
    global _start
_start:
    ; socket(AF_INET, SOCK_STREAM, 0)
    mov   rax, 41      ; sys_socket
    mov   rdi, 2       ; AF_INET
    mov   rsi, 1       ; SOCK_STREAM
    xor   rdx, rdx     ; protocol 0
    syscall
    mov   r12, rax     ; save socket fd in r12

    ; bind(socket, addr, 16)
    mov   rax, 49      ; sys_bind
    mov   rdi, r12
    mov   rsi, addr
    mov   rdx, 16
    syscall

    ; listen(socket, 10)
    mov   rax, 50      ; sys_listen
    mov   rdi, r12
    mov   rsi, 10
    syscall

.accept_loop:
    ; accept(socket, NULL, NULL)
    mov   rax, 43      ; sys_accept
    mov   rdi, r12
    xor   rsi, rsi
    xor   rdx, rdx
    syscall
    mov   rbx, rax     ; client socket fd in rbx

    ; write(client, hdr, hdrlen)
    mov   rax, 1       ; sys_write
    mov   rdi, rbx
    mov   rsi, hdr
    mov   rdx, hdrlen
    syscall

    ; open("index.html", O_RDONLY)
    mov   rax, 2       ; sys_open
    lea   rdi, [rel file]
    xor   rsi, rsi     ; flags = O_RDONLY (0)
    syscall
    mov   rcx, rax     ; file descriptor for index.html

.read_loop:
    ; read(file, buf, 4096)
    mov   rax, 0       ; sys_read
    mov   rdi, rcx
    mov   rsi, buf
    mov   rdx, 4096
    syscall
    test  rax, rax
    jle   .close_client
    ; write(client, buf, bytes_read)
    mov   rdx, rax
    mov   rax, 1       ; sys_write
    mov   rdi, rbx
    mov   rsi, buf
    syscall
    jmp   .read_loop

.close_client:
    mov   rax, 3       ; sys_close (client socket)
    mov   rdi, rbx
    syscall
    jmp   .accept_loop
