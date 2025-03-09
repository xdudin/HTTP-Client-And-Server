; Ultra-minimal HTTP webserver in x86_64 assembly for Linux
; Serves index.html to any request
; Fixed to prevent segmentation faults
; To assemble and link:
; nasm -f elf64 webserver.asm -o webserver.o
; ld -s webserver.o -o webserver

section .data
    ; Minimal required data
    http_header db "HTTP/1.1 200 OK", 0Dh, 0Ah
                db "Content-Type: text/html", 0Dh, 0Ah
                db "Connection: close", 0Dh, 0Ah, 0Dh, 0Ah
    header_len  equ $ - http_header

    filename    db "index.html", 0

section .bss
    buffer resb 4096                ; Buffer for file content and request

section .text
    global _start

_start:
    ; Create socket
    mov rax, 41                     ; syscall: socket
    mov rdi, 2                      ; AF_INET
    mov rsi, 1                      ; SOCK_STREAM
    xor rdx, rdx                    ; protocol = 0
    syscall

    mov r8, rax                     ; r8 = socket fd

    ; Set up sockaddr_in on stack with proper alignment
    sub rsp, 16                     ; Allocate 16 bytes on stack
    mov word [rsp], 2               ; AF_INET
    mov word [rsp+2], 0x901f        ; Port 8080 (network byte order)
    mov dword [rsp+4], 0            ; INADDR_ANY
    mov qword [rsp+8], 0            ; padding

    ; Bind
    mov rax, 49                     ; syscall: bind
    mov rdi, r8                     ; socket fd
    mov rsi, rsp                    ; pointer to sockaddr_in
    mov rdx, 16                     ; sizeof(sockaddr_in)
    syscall

    ; Listen
    mov rax, 50                     ; syscall: listen
    mov rdi, r8                     ; socket fd
    mov rsi, 5                      ; backlog
    syscall

accept_loop:
    ; Accept
    mov rax, 43                     ; syscall: accept
    mov rdi, r8                     ; socket fd
    xor rsi, rsi                    ; NULL addr
    xor rdx, rdx                    ; NULL addrlen
    syscall

    test rax, rax                   ; Check for error
    jl accept_loop                  ; Try again if error

    mov r9, rax                     ; r9 = client fd

    ; Read request (don't need content)
    xor rax, rax                    ; syscall: read
    mov rdi, r9                     ; client fd
    mov rsi, buffer                 ; buffer address
    mov rdx, 1024                   ; buffer size (reuse part of buffer)
    syscall

    ; Open file
    mov rax, 2                      ; syscall: open
    mov rdi, filename               ; filename
    xor rsi, rsi                    ; O_RDONLY
    xor rdx, rdx                    ; mode (not used)
    syscall

    test rax, rax                   ; Check for error
    jl close_client                 ; Skip if file error

    mov r10, rax                    ; r10 = file fd

    ; Send HTTP header
    mov rax, 1                      ; syscall: write
    mov rdi, r9                     ; client fd
    mov rsi, http_header            ; header text
    mov rdx, header_len             ; header length
    syscall

    ; Read file content
    xor rax, rax                    ; syscall: read
    mov rdi, r10                    ; file fd
    mov rsi, buffer                 ; buffer
    mov rdx, 4096                   ; buffer size
    syscall

    test rax, rax                   ; Check for error/EOF
    jle close_file                  ; Skip if error or empty file

    mov rdx, rax                    ; bytes read
    mov rax, 1                      ; syscall: write
    mov rdi, r9                     ; client fd
    mov rsi, buffer                 ; buffer with file content
    syscall

close_file:
    ; Close file
    mov rax, 3                      ; syscall: close
    mov rdi, r10                    ; file fd
    syscall

close_client:
    ; Close connection
    mov rax, 3                      ; syscall: close
    mov rdi, r9                     ; client fd
    syscall

    jmp accept_loop                 ; Loop back for next connection