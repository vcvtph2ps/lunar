.intel_syntax noprefix
.section .text

.globl x86_64_load_idt
.type x86_64_load_idt, @function

/// @brief Loads an IDTR
/// @param rdi Pointer to the IDTR structure
/// @return void
x86_64_load_idt:
    lidt [rdi]
    ret
.size x86_64_load_idt, . - x86_64_load_idt

.macro isr_no_error num
.globl isr\num
.balign 16
.type isr\num, @function
isr\num:
    push 0
    push \num
    jmp handler_common
.size isr\num, . - isr\num
.endm

.macro isr_error num
.globl isr\num
.balign 16
.type isr\num, @function
isr\num:
    push \num
    jmp handler_common
.size isr\num, . - isr\num
.endm

.macro SWAPGS_IF_FROM_RING3
    test qword ptr [rsp + 24], 3
    jz 1f
    swapgs
1:
.endm

.globl handler_common
.type handler_common, @function
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
.size handler_common, . - handler_common

// @safety: see src/arch/x86_64/userspace/syscall.S
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

.set i, 21
.rept 255 - 21
    .set i, i + 1
    .altmacro
    isr_no_error %i
    .noaltmacro
.endr

.section .rodata
.globl x86_64_isr_stub_table

.macro isr_table_entry num
    .quad isr\num
.endm

.balign 8
x86_64_isr_stub_table:
.set i, 0
.rept 256
    .altmacro
    isr_table_entry %i
    .noaltmacro
    .set i, i + 1
.endr
