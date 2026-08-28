#pragma once

#include <common/sched/thread.h>
#include <lib/types.h>

typedef struct {
    virt_addr_t stack_pointer;
    virt_addr_t syscall_storage;
    virt_addr_t kernel_stack_top;

    thread_t common;

    void* fpu_area;
    virt_addr_t gsbase;
    virt_addr_t fsbase;
} x86_64_thread_t; // NOLINT
