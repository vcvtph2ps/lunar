.section .text

.globl riscv64_context_switch
.balign 16
.type riscv64_context_switch, @function

/// @brief Switches the context from the current thread to the next thread
/// @param a0 Pointer to the current thread's arch specific structure
/// @param a1 Pointer to the next thread's arch specific structure
/// @return The pointer to the (now) old thread's arch specific structure
riscv64_context_switch:
    // save the callee-saved registers of the outgoing thread
    addi sp, sp, -104
    sd ra,  96(sp)
    sd s0,  88(sp)
    sd s1,  80(sp)
    sd s2,  72(sp)
    sd s3,  64(sp)
    sd s4,  56(sp)
    sd s5,  48(sp)
    sd s6,  40(sp)
    sd s7,  32(sp)
    sd s8,  24(sp)
    sd s9,  16(sp)
    sd s10,  8(sp)
    sd s11,  0(sp)

    // swap stacks
    sd sp, 0(a0)
    ld sp, 0(a1)

    // restore the callee-saved registers of the incoming thread
    ld s11,  0(sp)
    ld s10,  8(sp)
    ld s9,  16(sp)
    ld s8,  24(sp)
    ld s7,  32(sp)
    ld s6,  40(sp)
    ld s5,  48(sp)
    ld s4,  56(sp)
    ld s3,  64(sp)
    ld s2,  72(sp)
    ld s1,  80(sp)
    ld s0,  88(sp)
    ld ra,  96(sp)
    addi sp, sp, 104
    ret

.size riscv64_context_switch, . - riscv64_context_switch

.globl riscv64_kernel_thread_init
.balign 16
.type riscv64_kernel_thread_init, @function

/// @brief Entry point of a new kernel thread
/// @param a0 Pointer to the previous thread
/// @return Does not return, hands control to the thread, then exits
riscv64_kernel_thread_init:
    call arch_thread_init_common

    // call the thread's entry point
    li a0, 0
    jalr s0

    // if the thread did return, call thread exit
    li a0, 0
    jalr s1

.size riscv64_kernel_thread_init, . - riscv64_kernel_thread_init

.globl riscv64_userspace_init
.balign 16
.type riscv64_userspace_init, @function

/// @brief Entry point of a new userspace thread
/// @param a0 Pointer to the previous thread
/// @param s0 Pointer to the initial trap frame
riscv64_userspace_init:
    call arch_thread_init_common

    // t0 = frame pointer
    mv t0, s0

    ld t1, 256(t0)  // sepc
    csrw sepc, t1
    ld t1, 264(t0)  // sstatus
    csrw sstatus, t1

    // restore every register except x2/sp and x5/t0
    ld x1, 8(t0)    // ra
    ld x3, 24(t0)   // gp
    ld x4, 32(t0)   // tp
    ld x6, 48(t0)   // t1
    ld x7, 56(t0)   // t2
    ld x8, 64(t0)   // s0
    ld x9, 72(t0)   // s1
    ld x10, 80(t0)  // a0
    ld x11, 88(t0)  // a1
    ld x12, 96(t0)  // a2
    ld x13, 104(t0) // a3
    ld x14, 112(t0) // a4
    ld x15, 120(t0) // a5
    ld x16, 128(t0) // a6
    ld x17, 136(t0) // a7
    ld x18, 144(t0) // s2
    ld x19, 152(t0) // s3
    ld x20, 160(t0) // s4
    ld x21, 168(t0) // s5
    ld x22, 176(t0) // s6
    ld x23, 184(t0) // s7
    ld x24, 192(t0) // s8
    ld x25, 200(t0) // s9
    ld x26, 208(t0) // s10
    ld x27, 216(t0) // s11
    ld x28, 224(t0) // t3
    ld x29, 232(t0) // t4
    ld x30, 240(t0) // t5
    ld x31, 248(t0) // t6

    ld x2, 16(t0)   // sp
    ld x5, 40(t0)   // t0, must be last as it drops the frame pointer

    sret
    unimp

.size riscv64_userspace_init, . - riscv64_userspace_init
