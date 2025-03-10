section .data
    hdr    db "HTTP/1.0 200 OK",13,10,13,10
    hdrlen equ $-hdr
    file   db "index.html",0
    ; sockaddr_in structure for AF_INET, port 8080:
    ; sin_family = AF_INET (2)
    ; sin_port = htons(8080) = 0x901F (in little-endian)
    addr   dw 2,0x901F     ; sin_family, sin_port
           dd 0          ; sin_addr (0 for INADDR_ANY)
           dq 0          ; padding
    buf    times 27 db 0   ; allocate 27 bytes for the file content

section .text
    global _start
_start:
    ; Create socket: socket(AF_INET, SOCK_STREAM, 0)
    mov   rax, 41      ; sys_socket
    mov   rdi, 2       ; AF_INET
    mov   rsi, 1       ; SOCK_STREAM
    xor   rdx, rdx     ; protocol 0
    syscall
    mov   r12, rax     ; save socket fd in r12

    ; Bind: bind(socket, addr, 16)
    mov   rax, 49      ; sys_bind
    mov   rdi, r12
    mov   rsi, addr
    mov   rdx, 16
    syscall

    ; Listen: listen(socket, 10)
    mov   rax, 50      ; sys_listen
    mov   rdi, r12
    mov   rsi, 10
    syscall

.accept_loop:
    ; Accept connection: accept(socket, NULL, NULL)
    mov   rax, 43      ; sys_accept
    mov   rdi, r12
    xor   rsi, rsi
    xor   rdx, rdx
    syscall
    mov   rbx, rax     ; client socket fd in rbx

    ; Write HTTP header: write(client, hdr, hdrlen)
    mov   rax, 1       ; sys_write
    mov   rdi, rbx
    mov   rsi, hdr
    mov   rdx, hdrlen
    syscall

    ; Open index.html: open("index.html", O_RDONLY)
    mov   rax, 2       ; sys_open
    lea   rdi, [rel file]
    xor   rsi, rsi     ; flags = O_RDONLY (0)
    syscall
    mov   rcx, rax     ; file descriptor for index.html

.read_loop:
    ; Read file: read(file, buf, 27)
    mov   rax, 0       ; sys_read
    mov   rdi, rcx
    mov   rsi, buf
    mov   rdx, 27
    syscall
    test  rax, rax
    jle   .shutdown_client
    ; Write file content: write(client, buf, bytes_read)
    mov   rdx, rax
    mov   rax, 1       ; sys_write
    mov   rdi, rbx
    mov   rsi, buf
    syscall
    jmp   .read_loop

.shutdown_client:
    ; Close the client socket
    mov   rax, 3       ; sys_close
    mov   rdi, rbx
    syscall
    jmp   .accept_loop
