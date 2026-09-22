global x86_64_handle_syscall
extern x86_64_dispatch_syscall

x86_64_handle_syscall:
    swapgs

    mov r15, qword [gs:8]
    mov qword [r15 + 8], rsp
    mov rsp, qword [r15 + 16]

    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
    push qword [r15 + 8]

    mov rdi, rsp

    sti
    call x86_64_dispatch_syscall
    cli

    xor r12, r12
    mov ds, r12
    mov es, r12

    add rsp, 8
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    add rsp, 8
    pop rcx
    pop rbx
    add rsp, 8

    mov r15, qword [gs:8]
    mov rsp, qword [r15 + 8]
    xor r15, r15

    swapgs
    o64 sysret
