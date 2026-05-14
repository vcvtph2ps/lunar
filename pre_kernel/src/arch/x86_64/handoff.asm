global x86_64_kernel_handoff

; rdi = entry point
; rsi = stack pointer
; rdx = top level page tables
; rcx = boot info pointer
; r8 = current core id
x86_64_kernel_handoff:
    mov cr3, rdx ; load the page tables

    ; setup stack
    xor rbp, rbp
    mov rsp, rsi
    push qword 0
    push qword 0

    ; push entry to stack for ret
    push rdi

    mov rdi, rcx
    mov rsi, r8

    xor rbx, rbx
    xor rcx, rcx
    xor rdx, rdx
    xor r8, r8
    xor r9, r9
    xor r10, r10
    xor r11, r11
    xor r12, r12
    xor r13, r13
    xor r14, r14
    xor r15, r15
    cld

    push qword 0x2
    popfq

    ret
