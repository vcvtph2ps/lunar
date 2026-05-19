#include <arch/gdt.h>

#define DEFINE_SEGMENT(ACCESS, FLAGS) { .limit_low = 0, .base_low = 0, .base_mid = 0, .access = (ACCESS), .limit_high_flags = ((FLAGS) << 4), .base_high = 0 }

const arch_gdt_t g_arch_gdt_static_data = {
    {}, // null @ 0x00
    DEFINE_SEGMENT(0x9b, 0xA), // lmode kernel code @ 0x08
    DEFINE_SEGMENT(0x93, 0xC), // lmode kernel data @ 0x10
    DEFINE_SEGMENT(0xfb, 0xA), // cmode user code @ 0x18
    DEFINE_SEGMENT(0xf3, 0xC), // lmode user data @ 0x20
    DEFINE_SEGMENT(0xfb, 0xA), // lmode user code @ 0x28
    {} // tss @ 0x30
};
