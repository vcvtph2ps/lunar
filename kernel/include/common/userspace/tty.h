#pragma once

#include <common/fs/devfs.h>
#include <common/fs/io.h>
#include <common/sync/mutex.h>
#include <common/sync/wait_obj.h>
#include <stddef.h>
#include <stdint.h>

#define TTY_RB_SIZE 256

typedef struct {
    mutex_t mutex;
    wait_obj_t wait_obj;

    uint8_t buf[TTY_RB_SIZE];
    /// @brief Where the next input byte is written
    size_t head;

    /// @brief Where the next byte readable by a consumer is
    size_t tail;

    /// @brief Bytes in [tail, commit) are readable, bytes in [commit, head) are still being line edited (canonical mode) and must not be consumed yet
    size_t commit;

    struct {
        /// @brief canonical/cooked mode, input is line buffered with line editing
        bool canonical;
        /// @brief echo input back to the output
        bool echo;
        /// @brief echo control characters as ^X
        bool echo_ctrl;

        struct {
            uint8_t backspace; // defaults: ^H
            uint8_t intr; // default: ^C
            uint8_t eof; // default: ^D
            uint8_t kill; // default: ^U
            uint8_t erase_word; // default: ^W
            uint8_t del; // default: <DEL>
        } control_chars;
    } mode;

    /// @brief How many committed lines are still in the tty buffer
    /// @note only updated in canonical mode
    ATOMIC uint16_t line_count;

    /// @brief Set when a EOF is entered on an empty line, until new input arrives.
    bool eof;

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
