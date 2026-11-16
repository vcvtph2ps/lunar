.rodata
.balign 8
.extern arch_init_ap
.global g_apinit_end

.global g_apinit_start
.type   g_apinit_start, @function
g_apinit_start:
    # load ap stack
    ld sp, 8(a1)

    # Load temp values into csrs
    li t0, 0x200000000
    csrw sstatus, t0
    csrw stvec, 0
    csrw sie, 0

    ld t0, 16(a1)
    csrw satp, t0
    sfence.vma zero, zero

    ld t0, 32(a1)
    csrw sstatus, t1
    ld t0, 40(a1)
    csrw stvec, 0
    ld t0, 48(a1)
    csrw sie, 0

    # set booted flag to 1
    li t0, 1
    mv a2, a1
    addi a2, a2, 56
    amoswap.d.rl zero, t0, (a2)

    # load cpu local
    ld a0, 0(a1)
    ld t0, 24(a1)
    jr t0
    unimp
.balign 8
g_apinit_end:
