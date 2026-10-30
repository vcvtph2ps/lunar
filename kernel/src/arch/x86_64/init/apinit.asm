global g_apinit_start
global g_apinit_end

section .rodata align=16
align 16

%define off(addr) ebx + (addr - g_apinit_start)
%define off_cs(addr) cs:(addr - g_apinit_start)

bits 16
g_apinit_start:
    cli
    cld

    ; calculate the offset of the loaded code (cs * 0x10)
    xor ebx, ebx
    mov bx, cs
    shl ebx, 4

    o32 lgdt [off_cs(boot_info.gdtr)]

    ; set far jump offset
    lea eax, [off(.protected_entry)]
    mov dword [off_cs(.far_jmp32)], eax

    ; set protected mode bit
    mov eax, cr0
    or eax, 1
    mov cr0, eax

    ; far jump to protected mode
    o32 jmp far [off_cs(.far_jmp32)]
.far_jmp32:
    dd 0
    ; code32 selector
    dw 0x08

bits 32
.protected_entry:
    ; data32 selector
    mov eax, 0x10
    mov ds, eax
    mov ss, eax

    ; Set far jump offset
    lea eax, [off(.long_entry)]
    mov dword [off(.far_jmp64)], eax

    ; setup paging
    mov eax, [off(boot_info.cr3)]
    mov cr3, eax

    mov eax, cr4
    ; set cr4.PAE
    or eax, (1 << 5)

    cmp byte [off(boot_info.set_la57)], 0
    je .nola57
    ; set cr4.LA57
    or eax, (1 << 12)

.nola57:
    mov cr4, eax

    ; IA32_EFER
    mov ecx, 0xC0000080
    rdmsr

    ; set IA32_EFER.LME
    or eax, (1 << 8)

    cmp byte [off(boot_info.set_nx)], 0
    je .nonx
    ; set IA32_EFER.NXE
    or eax, (1 << 11)
.nonx:
    wrmsr

    mov eax, cr0
    ; Set CR0.PG and CR0.WP
    or eax, (1 << 31) | (1 << 16)
    mov cr0, eax

    ; Far jump to long mode
    jmp far [off(.far_jmp64)]
.far_jmp64:
    dd 0
    ; code64 selector
    dw 0x18

bits 64
.long_entry:
    mov rsp, [off(boot_info.stack)]
    mov rbp, rsp

    ; data64 selector
    mov rax, 0x20
    mov ds, rax
    mov ss, rax
    mov es, rax
    mov fs, rax
    mov gs, rax

    ; push invalid return address and base pointer
    push qword 0
    push qword 0

    ; save this info now, otherwise we could race with the bsp when we set booted_flag = 1
    mov rdi, qword [off(boot_info.core_boot_info)]
    mov rax, qword [off(boot_info.entry)]

    ; signal to the bsp we have booted
    lock inc byte [off(boot_info.booted_flag)]

    jmp rax

align 16
g_apinit_end:
boot_info:
    .cr3: dq 0
    .stack: dq 0
    .entry: dq 0
    .core_boot_info: dq 0
    .booted_flag: db 0
    .set_nx: db 0
    .set_la57: db 0
    .gdtr:
        dw 0
        dd 0
