section .text

global x86_64_load_idt
extern x86_64_idt_handler

; @brief loads an idtr
; @param idt (rdi): idtr* - pointer to the idt structure
; @return void
x86_64_load_idt:
    lidt [rdi]
    ret

%macro isr_no_error 1
global isr%1
align 16
isr%1:
    push 0
    push %1
    jmp handler_common
%endmacro

%macro isr_error 1
global isr%1
align 16
isr%1:
    push %1
    jmp handler_common
%endmacro

%macro SWAPGS_IF_FROM_RING3 0
        test qword [rsp + 24], 3
        jz %%noswap
        swapgs
    %%noswap:
%endmacro

handler_common:
    cld
    SWAPGS_IF_FROM_RING3

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

    mov rdi, rsp
    call x86_64_idt_handler

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
    pop rdx
    pop rcx
    pop rbx
    pop rax
    SWAPGS_IF_FROM_RING3
    add rsp, 16
    iretq

isr_no_error 0
isr_no_error 1
isr_no_error 2
isr_no_error 3
isr_no_error 4
isr_no_error 5
isr_no_error 6
isr_no_error 7
isr_error    8
isr_no_error 9
isr_error    10
isr_error    11
isr_error    12
isr_error    13
isr_error    14
isr_no_error 15
isr_no_error 16
isr_error    17
isr_no_error 18
isr_no_error 19
isr_no_error 20
isr_error    21

%assign i 21
%rep 255 - 21
    %assign i i + 1
    isr_no_error i
%endrep

section .rodata
global x86_64_isr_stub_table

align 8
x86_64_isr_stub_table:
%assign i 0
%rep 256
    extern isr%+i
    dq isr%+i
    %assign i i+1
%endrep
