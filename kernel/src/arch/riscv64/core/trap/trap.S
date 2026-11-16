.extern riscv64_dispatch_trap

.align 4
.globl riscv64_trap_handler
riscv64_trap_handler:
    # generate these offsets from a header file or something, that's atleast static asserted...
    # sscratch = cpulocal
    #
    # cpulocal + 0 = current cpu local
    # cpulocal + 8 = current thread struct
    # cpulocal + 16 = scratch space
    # cpulocal + 24 = scratch space
    # cpulocal + 32 = scratch space
    #
    # thread + 0 = stack_pointer
    # thread + 8 = syscall_storage
    # thread + 16 = kernel_stack_top
    #

    # t0 = sscratch
    # sscratch = t0
    csrrw t0, sscratch, t0 # swap sscratch and t0 to get cpu local

    # (cpu_local + 24) = t1
    sd t1, 24(t0) # save t1 into the cpu local scratch space 2

    # t1 = old t0 (sscratch)
    # sscratch = old sscratch (t0)
    csrrw t1, sscratch, t0 # move cpu local back into sscratch and put the old t0 value into t1

    # (cpu_local + 24) = old t0 (t1)
    sd t1, 16(t0)

    # t0 = cpu local pointer
    # t1 = scratch space
    # (cpu_local + 24) = old stack pointer
    sd sp, 32(t0)
    # Check sstatus.SPP (bit 8)
    csrr t1, sstatus
    andi t1, t1, (1 << 8) # 0 = from usermode
    bnez t1, .no_swap

    # t1 = (cpu_local + 8) (current_thread)
    ld t1, 8(t0)
    # (current_thread + 8) (syscall_storage)
    ld sp, 8(t1)
.no_swap:
    # Interrupt frame
    addi sp, sp, -288

    # Save stack pointer
    ld t1, 32(t0)
    sd t1, 16(sp)

    # Restore t0 and t1
    ld t1, 24(t0)
    ld t0, 16(t0)

    # We don't save x2/sp here
    .irp n,1,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
    sd x\n, \n*8(sp)
    .endr

    # save sepc
    csrr t0, sepc
    sd t0, 256(sp)
    # save sstatus
    csrr t1, sstatus
    sd t1, 264(sp)
    # save scause
    csrr t2, scause
    sd t2, 272(sp)
    # save stval
    csrr t3, stval
    sd t3, 280(sp)

    mv a0, sp
    call riscv64_trap_dispatch
    csrci sstatus, 2 # disable interrupts

    ld t0, 256(sp)    # restore sepc
    csrw sepc, t0
    ld t0, 264(sp)    # restore sstatus
    csrw sstatus, t0

    # We only skip x2/sp here, it is restored last
    .irp n,1,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
    ld x\n, \n*8(sp)
    .endr

    # restore x2/sp
    ld sp, 16(sp)
    sret


.option push
.option norvc
.balign 4096
.globl riscv64_trap_handler_vectored
riscv64_trap_handler_vectored:
.rept 32
    j riscv64_trap_handler
.endr
.option pop
