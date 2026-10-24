.section .text
.globl dpc_execute_on_stack

# see src/arch/x86_64/core/interrupts/dpc.asm
dpc_execute_on_stack:
    unimp
