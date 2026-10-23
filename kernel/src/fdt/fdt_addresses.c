#if defined(__riscv) && __riscv_xlen == 64
#include <common/libfdt.h>
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdocumentation"
#include "libfdt/fdt_addresses.c"
#pragma clang diagnostic pop
#endif
