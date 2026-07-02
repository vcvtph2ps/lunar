#include <common/arch.h>
#include <common/log.h>
#include <stdint.h>

uint64_t __stack_chk_guard = 0xdeadbeefcafebabe; // NOLINT

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-prototypes"

[[gnu::used, noreturn]] void __stack_chk_fail(void) { // NOLINT
    arch_panic("stack smashing detected\n");
}

#pragma clang diagnostic pop
