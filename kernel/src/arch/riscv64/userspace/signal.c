#include <common/interrupts/interrupt.h>
#include <common/userspace/signal.h>

void signal_arch_setup_frame(arch_interrupt_frame_t* frame, uapi_signal_info_t* signal_info, uapi_signal_handler_t* handler) {
    (void) frame;
    (void) signal_info;
    (void) handler;

    /// @todo: implement signal delivery for riscv64
}

[[noreturn]] void signal_arch_enter_handler(arch_interrupt_frame_t* frame) {
    (void) frame;

    ASSERT_TODO();
}

[[noreturn]] void signal_arch_restore_frame(uapi_signal_ucontext_t* context) {
    (void) context;

    ASSERT_TODO();
}
