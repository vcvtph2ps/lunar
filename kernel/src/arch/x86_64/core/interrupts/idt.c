#include <arch/cpu_local.h>
#include <arch/internal/cr.h>
#include <arch/interrupts/interrupt.h>
#include <common/arch.h>
#include <common/init.h>
#include <common/log.h>
#include <lib/types.h>

typedef struct [[gnu::packed]] {
    uint16_t limit;
    uint64_t base;
} idtr_t;

typedef struct [[gnu::packed]] {
    uint16_t base_low;

    uint16_t selector;
    uint8_t ist;
    uint8_t type_attr;

    uint16_t base_mid;
    uint32_t base_high;
    uint32_t zero;
} idt_entry_t;

typedef struct {
    arch_interrupt_regs_t regs;
    uint64_t vector, error;
    arch_interrupt_sregs_t state;
} arch_interrupt_idt_frame_t;

extern void x86_64_dispatch_interrupt(arch_interrupt_frame_t* frame);
extern void x86_64_load_idt(idtr_t* idtr);

extern uintptr_t x86_64_isr_stub_table[256];
static idt_entry_t g_idt[256];

static void set_idtr_entry(int vector, uintptr_t isr_addr, uint8_t type_attr, int ist) {
    g_idt[vector] = (idt_entry_t) { .base_low = isr_addr & 0xFFFF, .base_mid = (isr_addr >> 16) & 0xFFFF, .selector = 0x08, .ist = ist, .type_attr = type_attr, .base_high = (isr_addr >> 32) & 0xFFFFFFFF };
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-prototypes"

__attribute__((no_sanitize("undefined"))) // @todo: ubsan reports unaligned access for the x86_isr_stub_table
void idt_init(uint32_t core_id) {
    if(INIT_CORE_IS_BSP(core_id)) {
        for(int i = 0; i < 256; ++i) { set_idtr_entry(i, x86_64_isr_stub_table[i], 0x8E, 0); }

        // #BP
        set_idtr_entry(0x03, x86_64_isr_stub_table[0x03], 0xEE, 0);
        // #NMI
        set_idtr_entry(0x02, x86_64_isr_stub_table[0x02], 0x8E, 1);
        // #DF
        set_idtr_entry(0x08, x86_64_isr_stub_table[0x08], 0x8E, 2);
        // #MC
        set_idtr_entry(0x12, x86_64_isr_stub_table[0x12], 0x8E, 3);
    }

    idtr_t idtr;
    idtr.limit = (sizeof(idt_entry_t) * 256) - 1;
    idtr.base = (virt_addr_t) &g_idt;

    x86_64_load_idt(&idtr);
}

void idt_set_usermode_stack(uint64_t stack_pointer) {
    CPU_LOCAL_WRITE(tss.rsp0, stack_pointer);
}

void x86_64_idt_handler(arch_interrupt_idt_frame_t* frame) {
    arch_interrupt_frame_t interrupt_frame;
    interrupt_frame.vector = frame->vector;
    interrupt_frame.error = frame->error;
    if(frame->vector == 0x0E) {
        interrupt_frame.interrupt_data = arch_cr_read_cr2();
    } else {
        interrupt_frame.interrupt_data = 0;
    }
    interrupt_frame.regs = &frame->regs;
    interrupt_frame.is_user = (frame->state.cs & 0x3) == 3;
    interrupt_frame.state = &frame->state;

    x86_64_dispatch_interrupt(&interrupt_frame);
}

#pragma clang diagnostic pop
