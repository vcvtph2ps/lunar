#pragma once

#include <common/fs/devfs.h>
#include <common/fs/io.h>
#include <common/sync/mutex.h>
#include <common/sync/wait_queue.h>
#include <stddef.h>
#include <stdint.h>

#define TTY_RB_SIZE 256

typedef struct {
    mutex_t mutex;
    wait_queue_t queue;
    uint8_t buf[TTY_RB_SIZE];
    size_t head;
    size_t tail;

    struct {
        bool canonical;
    } mode;

    /// @brief How many commited (newline sent) lines are still in the tty buffer
    /// @note only updated in cannonical mode
    ATOMIC uint16_t line_count;

    /// @brief Called when a byte is written to the tty output (e.g. to forward to UART)
    void (*on_write)(void* ctx, uint8_t c);
    void* write_ctx;
} tty_t;

/// @brief devfs ops for a tty device; pass the tty_t* as the ctx when calling devfs_bind
extern const devfs_device_ops_t g_tty_devfs_ops;

tty_t* tty_create();
void tty_free(tty_t* tty);

/**
 * @brief Callback suitable for use as a on_recv handler
 * @param ctx pointer to a tty_t instance
 * @param c char to forward to the tty's input buffer
 */
void tty_recv_generic(void* ctx, char c);

/// @brief Performs IO on the tty device for vfs operations
vfs_result_t tty_perform_io(void* ctx, io_request_t* request);

/**
 * @brief Write byte to tty output
 * @note calls on_write if set
 */
bool tty_write(tty_t* tty, uint8_t c);

/// @brief Puts a byte into the tty input buffer
bool tty_put(tty_t* tty, uint8_t c);

/// @brief Reads a byte from the tty input buffer, returns false if empty
bool tty_read(tty_t* tty, uint8_t* c);

/// @brief Reads a byte from the tty input buffer, blocks if no input is ready
uint8_t tty_read_blocking(tty_t* tty);
