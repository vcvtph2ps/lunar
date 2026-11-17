.intel_syntax noprefix
.section .text

.globl x86_64_load_gdt
.type x86_64_load_gdt, @function

/// @brief Loads a GDTR and reloads the code, data, and task segment registers
/// @param rdi Pointer to the GDTR structure
/// @param rsi Code segment selector
/// @param rdx Data segment selector
/// @param rcx Task segment selector
x86_64_load_gdt:
    lgdt [rdi]

    // far return to reload cs
    movzx rax, si
    push rax
    lea rax, .reload_cs
    push rax
    retfq
.reload_cs:
    mov ds, dx
    mov es, dx
    mov ss, dx
    ltr cx
    ret
.size x86_64_load_gdt, . - x86_64_load_gdt
