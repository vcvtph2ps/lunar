section .text

global x86_64_signal_iret_to_user

; @brief Restores a full userspace register state and returns to it.
; @param rdi (arch_interrupt_idt_frame_t*): frame holding the state to restore
; @note sysret is unable to restore r15, rcx, and r15. so iretq is used.
x86_64_signal_iret_to_user:
    ; user data segments
    mov ax, 0x23
    mov ds, ax
    mov es, ax

    ; the frame is laid out exactly like the ISR stub frame, so reuse its epilogue
    mov rsp, rdi

    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax

    ; skip the vector and error code
    add rsp, 16

    ; restore the userspace GS base
    swapgs
    iretq
