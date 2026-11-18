#pragma once

#include <stdint.h>

typedef struct {
    uint64_t core_id;
    uint64_t stack;
    uint64_t satp;
    uint64_t entry;

    uint64_t sstatus;
    uint64_t stvec;
    uint64_t sie;

    uint64_t global_pointer;

    uint64_t booted_flag;
} arch_ap_info_t;
