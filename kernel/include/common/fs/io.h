#pragma once
#include <memory/vm.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    IO_REQUEST_READ,
    IO_REQUEST_WRITE,
    IO_REQUEST_IOCTL
} io_request_type_t;

typedef struct {
    vm_address_space_t* address_space;
    void* buffer;
    size_t size;
} io_buffer_t;

#define IO_BUFFER_CREATE_KERNEL(BUFFER, SIZE)                                          \
    (io_buffer_t) {                                                                    \
        .address_space = g_vm_global_address_space, .buffer = (BUFFER), .size = (SIZE) \
    }

#define IO_BUFFER_CREATE_USER(ADDRESS_SPACE, BUFFER, SIZE)                   \
    (io_buffer_t) {                                                          \
        .address_space = (ADDRESS_SPACE), .buffer = (BUFFER), .size = (SIZE) \
    }

typedef struct {
    io_request_type_t type;

    union {
        struct {
            io_buffer_t buffer;
            size_t count;
            size_t offset;
            size_t bytes_read;
        } read;
        struct {
            // @todo: this should be const
            io_buffer_t buffer;
            size_t count;
            size_t offset;
            size_t bytes_written;
        } write;
        struct {
            io_buffer_t buffer;
            uint64_t request;
        } ioctl;
    };

    bool no_block; // driver must return some form of `WOULD_BLOCK` error instead of blocking
} io_request_t;
