.section .text
.globl dpc_execute_on_stack

/// @brief Executes a DPC on the target stack
/// @param a0 DPC function pointer
/// @param a1 DPC data
/// @param a2 Target stack pointer
/// @return void
dpc_execute_on_stack:
    addi sp, sp, -16
    sd ra, 8(sp)
    sd s0, 0(sp)
    mv s0, sp

    // switch to the dpc stack
    mv sp, a2
    andi sp, sp, -16

    // move fn into t0, and data into a0
    mv t0, a0
    mv a0, a1

    // call fn
    jalr t0

    // restore stack
    mv sp, s0
    ld s0, 0(sp)
    ld ra, 8(sp)
    addi sp, sp, 16
    ret
