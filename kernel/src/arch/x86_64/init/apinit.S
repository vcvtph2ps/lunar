.intel_syntax noprefix

.globl g_apinit_start
.globl g_apinit_end

// cpp helpers (replace NASM's %define off/off_cs)
#define OFF(name) [ebx + OFF_##name]
#define OFF_CS(name) cs:[OFF_##name]

.section .rodata
.balign 16

.code16
g_apinit_start:
.Lapinit_base:
    cli
    cld

    // calculate the offset of the loaded code (cs * 0x10)
    xor ebx, ebx
    mov bx, cs
    shl ebx, 4

    data32 lgdt OFF_CS(boot_info_gdtr)

    // set far jump offset
    lea eax, OFF(protected_entry)
    mov dword ptr OFF_CS(far_jmp32), eax

    // set protected mode bit
    mov eax, cr0
    or eax, 1
    mov cr0, eax

    // far jump to protected mode
    jmp fword ptr OFF_CS(far_jmp32)
.Lfar_jmp32:
    .long 0
    // code32 selector
    .word 0x08

.code32
.Lprotected_entry:
    // data32 selector
    mov eax, 0x10
    mov ds, ax
    mov ss, ax

    // Set far jump offset
    lea eax, OFF(long_entry)
    mov dword ptr OFF(far_jmp64), eax

    // setup paging
    mov eax, dword ptr OFF(boot_info_cr3)
    mov cr3, eax

    mov eax, cr4
    // set cr4.PAE
    or eax, (1 << 5)

    cmp byte ptr OFF(boot_info_set_la57), 0
    je .Lnola57
    // set cr4.LA57
    or eax, (1 << 12)

.Lnola57:
    mov cr4, eax

    // IA32_EFER
    mov ecx, 0xC0000080
    rdmsr

    // set IA32_EFER.LME
    or eax, (1 << 8)

    cmp byte ptr OFF(boot_info_set_nx), 0
    je .Lnonx
    // set IA32_EFER.NXE
    or eax, (1 << 11)
.Lnonx:
    wrmsr

    mov eax, cr0
    // Set CR0.PG and CR0.WP
    or eax, (1 << 31) | (1 << 16)
    mov cr0, eax

    // Far jump to long mode
    jmp fword ptr OFF(far_jmp64)
.Lfar_jmp64:
    .long 0
    // code64 selector
    .word 0x18

.code64
.Llong_entry:
    mov rsp, qword ptr OFF(boot_info_stack)
    mov rbp, rsp

    // data64 selector
    mov eax, 0x20
    mov ds, ax
    mov ss, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    // push invalid return address and base pointer
    push 0
    push 0

    // save this info now, otherwise we could race with the bsp when we set booted_flag = 1
    mov rdi, qword ptr OFF(boot_info_core_boot_info)
    mov rax, qword ptr OFF(boot_info_entry)

    // signal to the bsp we have booted
    lock inc byte ptr OFF(boot_info_booted_flag)

    jmp rax

.balign 16
g_apinit_end:
boot_info_cr3:            .quad 0
boot_info_stack:          .quad 0
boot_info_entry:          .quad 0
boot_info_core_boot_info: .quad 0
boot_info_booted_flag:    .byte 0
boot_info_set_nx:         .byte 0
boot_info_set_la57:       .byte 0
boot_info_gdtr:
    .word 0
    .long 0

.equ OFF_protected_entry, .Lprotected_entry - .Lapinit_base
.equ OFF_long_entry, .Llong_entry - .Lapinit_base
.equ OFF_far_jmp32, .Lfar_jmp32 - .Lapinit_base
.equ OFF_far_jmp64, .Lfar_jmp64 - .Lapinit_base
.equ OFF_boot_info_cr3, boot_info_cr3 - .Lapinit_base
.equ OFF_boot_info_stack, boot_info_stack - .Lapinit_base
.equ OFF_boot_info_entry, boot_info_entry - .Lapinit_base
.equ OFF_boot_info_core_boot_info, boot_info_core_boot_info - .Lapinit_base
.equ OFF_boot_info_booted_flag, boot_info_booted_flag - .Lapinit_base
.equ OFF_boot_info_set_nx, boot_info_set_nx - .Lapinit_base
.equ OFF_boot_info_set_la57, boot_info_set_la57 - .Lapinit_base
.equ OFF_boot_info_gdtr, boot_info_gdtr - .Lapinit_base
