global x86_64_load_gdt

; @brief: loads a gdtr, code segment, data segments into ds, es, ss, and task segment
; @param: gdt (rdi): gdtr* - pointer to the gdtr structure
; @param: cs (si): uint16_t - code segment selector
; @param: ds (dx): uint16_t - data segment selector
; @param: tr (cx): uint16_t - task segment selector
; @returns: void
x86_64_load_gdt:
    lgdt [rdi]

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
