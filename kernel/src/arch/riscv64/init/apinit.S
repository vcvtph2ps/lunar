#include <arch/riscv64/internal/asm_offsets.h>

.section .rodata

.extern arch_init_ap
.globl g_apinit_start
.globl g_apinit_end

.balign 8
.type g_apinit_start, @function
g_apinit_start:
    // load ap stack
    ld sp, AP_INFO_OFF_STACK(a1)

    // Load temp values into csrs
    li t0, 0x200000000
    csrw sstatus, t0
    csrw stvec, 0
    csrw sie, 0

    ld t0, AP_INFO_OFF_SATP(a1)
    csrw satp, t0
    sfence.vma zero, zero

    ld t0, AP_INFO_OFF_SSTATUS(a1)
    csrw sstatus, t0
    ld t0, AP_INFO_OFF_STVEC(a1)
    csrw stvec, t0
    ld t0, AP_INFO_OFF_SIE(a1)
    csrw sie, t0

    // set booted flag to 1
    li t0, 1
    mv a2, a1
    addi a2, a2, AP_INFO_OFF_BOOTED_FLAG
    amoswap.d.rl zero, t0, (a2)

    // restore the global pointer so gp-relative accesses work
    ld gp, AP_INFO_OFF_GLOBAL_POINTER(a1)

    // load cpu local and jump to the entry point
    ld a0, AP_INFO_OFF_CORE_ID(a1)
    ld t0, AP_INFO_OFF_ENTRY(a1)
    jr t0
    unimp

.balign 8
g_apinit_end:
