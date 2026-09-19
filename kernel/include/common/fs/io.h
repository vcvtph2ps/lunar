#pragma once
#include <stddef.h>

typedef enum {
    IO_REQUEST_READ,
    IO_REQUEST_WRITE
} io_request_type_t;

typedef struct {
    io_request_type_t type;

    union {
        struct {
            void* buffer; // @note must be sized atleast count bytes
            size_t count;
            size_t offset;
            size_t bytes_read;
        } read;
        struct {
            const void* buffer; // @note must be sized atleast count bytes
            size_t count;
            size_t offset;
            size_t bytes_written;
        } write;
    };

    bool no_block; // driver must some form of `WOULD_BLOCK` error instead of blocking
} io_request_t;
