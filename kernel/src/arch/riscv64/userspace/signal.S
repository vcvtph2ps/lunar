    .text

    .globl riscv64_signal_sret_to_user
    .balign 16
    .type riscv64_signal_sret_to_user, @function
# @brief Restores a full userspace register state and returns to it.
# @param a0 Pointer to an arch_interrupt_regs_t holding the state to restore.
riscv64_signal_sret_to_user:
    ld t0, 256(a0)
    csrw sepc, t0
    ld t0, 264(a0)
    csrw sstatus, t0

    # x5/t0 is the frame pointer and is restored last, x2/sp right before it
    mv t0, a0
    ld x1, 8(t0)    # ra
    ld x3, 24(t0)   # gp
    ld x4, 32(t0)   # tp
    ld x6, 48(t0)   # t1
    ld x7, 56(t0)   # t2
    ld x8, 64(t0)   # s0
    ld x9, 72(t0)   # s1
    ld x10, 80(t0)  # a0
    ld x11, 88(t0)  # a1
    ld x12, 96(t0)  # a2
    ld x13, 104(t0) # a3
    ld x14, 112(t0) # a4
    ld x15, 120(t0) # a5
    ld x16, 128(t0) # a6
    ld x17, 136(t0) # a7
    ld x18, 144(t0) # s2
    ld x19, 152(t0) # s3
    ld x20, 160(t0) # s4
    ld x21, 168(t0) # s5
    ld x22, 176(t0) # s6
    ld x23, 184(t0) # s7
    ld x24, 192(t0) # s8
    ld x25, 200(t0) # s9
    ld x26, 208(t0) # s10
    ld x27, 216(t0) # s11
    ld x28, 224(t0) # t3
    ld x29, 232(t0) # t4
    ld x30, 240(t0) # t5
    ld x31, 248(t0) # t6

    ld x2, 16(t0)   # sp
    ld x5, 40(t0)   # t0, must be the last load as it drops the frame pointer

    sret
    .size riscv64_signal_sret_to_user, . - riscv64_signal_sret_to_user
