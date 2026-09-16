#include <common/assert.h>
#include <common/userspace/syscall.h>

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-prototypes"

[[noreturn]] void arch_syscall_init() {
    assert(false && "Unimplemented");
}

#pragma clang diagnostic pop
