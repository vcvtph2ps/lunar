#include <common/fs/devfs.h>
#include <common/fs/io.h>
#include <common/fs/vfs.h>
#include <common/log.h>
#include <common/sched/sched.h>
#include <common/sync/mutex.h>
#include <common/sync/wait_obj.h>
#include <common/userspace/tty.h>
#include <lib/helpers.h>
#include <lib/string.h>
#include <memory/heap.h>

// default input characters handled by the canonical mode
// probably gonna forgot what these mean in like, a few days
// ^H: backspace
#define CHAR_BACKSPACE 0x08
// ^C: discard the current input line
#define CHAR_INTR 0x03
// ^D: end of file
#define CHAR_EOF 0x04
// ^U: discard the current input line
#define CHAR_KILL 0x15
// ^W: erase previous word
#define CHAR_ERASE_WORD 0x17
// <DEL>: backspace
#define CHAR_DEL 0x7f

static size_t ring_prev(size_t index) {
    return (index - 1 + TTY_RB_SIZE) % TTY_RB_SIZE;
}

static size_t ring_next(size_t index) {
    return (index + 1) % TTY_RB_SIZE;
}

tty_t* tty_create() {
    tty_t* tty = heap_alloc(sizeof(tty_t));
    tty->mutex = MUTEX_INIT;
    tty->wait_obj = WAIT_OBJ_INIT(WAIT_OBJ_SYNCHRONIZATION);
    tty->head = 0;
    tty->tail = 0;
    tty->commit = 0;
    tty->eof = false;

    tty->mode.canonical = true;
    tty->mode.echo = true;
    tty->mode.echo_ctrl = true;

    tty->mode.control_chars.backspace = CHAR_BACKSPACE;
    tty->mode.control_chars.intr = CHAR_INTR;
    tty->mode.control_chars.eof = CHAR_EOF;
    tty->mode.control_chars.kill = CHAR_KILL;
    tty->mode.control_chars.erase_word = CHAR_ERASE_WORD;
    tty->mode.control_chars.del = CHAR_DEL;

    tty->on_write = nullptr;
    tty->write_ctx = nullptr;

    memory_zero(tty->buf, TTY_RB_SIZE);
    return tty;
}

void tty_free(tty_t* tty) {
    assert(tty->wait_obj.wait_blocks.count == 0 && "tty freed with waiting threads");
    heap_free(tty, sizeof(tty_t));
}

/// @brief Whether the ring buffer has room for another byte
static bool tty_full(const tty_t* tty) {
    return ring_next(tty->head) == tty->tail;
}

/// @brief Whether the buffer contains committed bytes available for reading
static bool tty_readable(const tty_t* tty) {
    return tty->tail != tty->commit;
}

/// @brief Whether the current (line edited) pending input is empty
static bool tty_pending_empty(const tty_t* tty) {
    return tty->head == tty->commit;
}

bool tty_write(tty_t* tty, uint8_t c) {
    if(tty->on_write != nullptr) {
        tty->on_write(tty->write_ctx, c);
    }
    return true;
}

void tty_recv_generic(void* ctx, char c) {
    tty_t* tty = (tty_t*) ctx;
    if(!tty_put(tty, (uint8_t) c)) {
        LOG_WARN("tty full, dropping input\n");
    }
}

/// @brief echo a single byte to the output if enabled
static void tty_echo(tty_t* tty, uint8_t c) {
    if(tty->on_write == nullptr || !tty->mode.echo) {
        return;
    }
    if(c == '\n') {
        tty->on_write(tty->write_ctx, '\r');
    }
    tty->on_write(tty->write_ctx, c);
}

/// @brief echo a control character as ^X if enabled
static void tty_echo_ctrl(tty_t* tty, uint8_t c) {
    if(tty->on_write == nullptr || !tty->mode.echo || !tty->mode.echo_ctrl) {
        return;
    }
    tty->on_write(tty->write_ctx, '^');
    uint8_t plain = (c == CHAR_DEL) ? '?' : (uint8_t) (c + 0x40);
    tty->on_write(tty->write_ctx, plain);
}

/// @brief echo a line ending, moving the cursor back to column 0 on the next line
static void tty_echo_newline(tty_t* tty) {
    if(tty->on_write == nullptr || !tty->mode.echo) {
        return;
    }
    tty->on_write(tty->write_ctx, '\r');
    tty->on_write(tty->write_ctx, '\n');
}

/// @brief echo an input byte the way it should appear on screen
static void tty_echo_char(tty_t* tty, uint8_t c) {
    if(!tty->mode.echo) {
        return;
    }
    if(c == '\n') {
        tty_echo_newline(tty);
    } else if((c < 0x20 && c != '\t') || c == CHAR_DEL) {
        tty_echo_ctrl(tty, c);
    } else {
        tty_echo(tty, c);
    }
}

/// @brief number of screen columns the echo of a byte occupies
static int tty_echo_cols(uint8_t c) {
    if((c < 0x20 && c != '\t') || c == CHAR_DEL) {
        return 2;
    }
    return 1;
}

/// @note must hold the tty mutex
static bool tty_put_raw(tty_t* tty, uint8_t c) {
    if(tty_full(tty)) {
        return false;
    }
    tty->buf[tty->head] = c;
    tty->head = ring_next(tty->head);
    return true;
}

/// @note must hold the tty mutex. erases the last pending byte, undoing its echo
static void tty_erase_one(tty_t* tty) {
    if(tty_pending_empty(tty)) {
        return;
    }
    uint8_t last = tty->buf[ring_prev(tty->head)];
    tty->head = ring_prev(tty->head);
    if(tty->on_write == nullptr || !tty->mode.echo) {
        return;
    }
    int cols = tty_echo_cols(last);
    for(int i = 0; i < cols; i++) {
        tty->on_write(tty->write_ctx, '\b');
        tty->on_write(tty->write_ctx, ' ');
        tty->on_write(tty->write_ctx, '\b');
    }
}

/// @note must hold the tty mutex
static void tty_erase_char(tty_t* tty) {
    if(tty_pending_empty(tty)) {
        tty_echo(tty, '\a');
        return;
    }
    tty_erase_one(tty);
}

/// @note must hold the tty mutex
static void tty_kill_line(tty_t* tty) {
    while(!tty_pending_empty(tty)) {
        tty_erase_one(tty);
    }
}

/// @note must hold the tty mutex
static void tty_erase_word(tty_t* tty) {
    if(tty_pending_empty(tty)) {
        tty_echo(tty, '\a');
        return;
    }
    while(!tty_pending_empty(tty)) {
        uint8_t c = tty->buf[ring_prev(tty->head)];
        if(c != ' ' && c != '\t') {
            break;
        }
        tty_erase_one(tty);
    }
    while(!tty_pending_empty(tty)) {
        uint8_t c = tty->buf[ring_prev(tty->head)];
        if(c == ' ' || c == '\t') {
            break;
        }
        tty_erase_one(tty);
    }
}

/// @note must hold the tty mutex
static void tty_handle_eof(tty_t* tty) {
    if(tty_pending_empty(tty)) {
        tty->eof = true;
        return;
    }
    tty->commit = tty->head;
    tty->eof = false;
    ATOMIC_LOAD_ADD(&tty->line_count, 1, ATOMIC_RELEASE);
}

/// @note must hold the tty mutex
static void tty_handle_intr(tty_t* tty) {
    tty_kill_line(tty);
    tty_echo_ctrl(tty, CHAR_INTR);
    tty_echo_newline(tty);
    // @todo: send SIGINT to the fg pgrp
}

static bool tty_put_canonical(tty_t* tty, uint8_t c) {
    // ICRNL: translate carriage returns to newlines before processing
    // @todo: this is a setting
    if(c == '\r') {
        c = '\n';
    }

    bool wake = false;
    switch(c) {
        case '\n': {
            // Commit the current line, making it visible to readers.
            if(tty_put_raw(tty, '\n')) {
                tty->commit = tty->head;
                tty->eof = false;
                ATOMIC_LOAD_ADD(&tty->line_count, 1, ATOMIC_RELEASE);
                tty_echo_newline(tty);
                wake = true;
            }
            break;
        }
        case CHAR_BACKSPACE:
        case CHAR_DEL:        tty_erase_char(tty); break;
        case CHAR_KILL:       tty_kill_line(tty); break;
        case CHAR_ERASE_WORD: tty_erase_word(tty); break;
        case CHAR_INTR:       tty_handle_intr(tty); break;
        case CHAR_EOF:
            tty_handle_eof(tty);
            wake = true;
            break;
        default:
            if(tty_put_raw(tty, c)) {
                tty->eof = false;
                tty_echo_char(tty, c);
            }
            break;
    }

    if(wake) {
        wait_obj_signal(&tty->wait_obj, 1);
    }
    return true;
}

bool tty_put(tty_t* tty, uint8_t c) {
    mutex_acquire(&tty->mutex);

    if(tty->mode.canonical) {
        bool res = tty_put_canonical(tty, c);
        mutex_release(&tty->mutex);
        return res;
    }

    // for raw mode everything is committed immediately.
    bool ok = tty_put_raw(tty, c);
    if(ok) {
        tty->commit = tty->head;
        tty->eof = false;
        tty_echo_char(tty, c);
    }
    mutex_release(&tty->mutex);

    if(ok) {
        wait_obj_signal(&tty->wait_obj, 1);
    }
    return ok;
}

/// @note must hold the tty mutex, only consumes bytes up to commit.
static bool tty_read_lockless(tty_t* tty, uint8_t* c) {
    if(!tty_readable(tty)) {
        return false;
    }

    *c = tty->buf[tty->tail];
    tty->tail = ring_next(tty->tail);

    return true;
}

bool tty_read(tty_t* tty, uint8_t* c) {
    mutex_acquire(&tty->mutex);
    bool result = tty_read_lockless(tty, c);
    mutex_release(&tty->mutex);

    return result;
}

uint8_t tty_read_blocking(tty_t* tty) {
    while(1) {
        uint8_t c;
        if(tty_read(tty, &c)) {
            return c;
        }
        sched_wait_single(&tty->wait_obj, UINT64_MAX);
    }
}

static vfs_result_t tty_vfs_read(tty_t* tty, io_request_t* request) {
    uint8_t* read_buffer = request->read.buffer;
    size_t count = request->read.count;

    mutex_acquire(&tty->mutex);

    // wait until there is committed data, or EOF.
    while(!tty_readable(tty)) {
        if(tty->eof) {
            request->read.bytes_read = 0;
            tty->eof = false;
            mutex_release(&tty->mutex);
            return VFS_RESULT_OK;
        }

        mutex_release(&tty->mutex);

        if(request->no_block) {
            return VFS_RESULT_ERR_WOULD_BLOCK;
        }

        sched_wait_single(&tty->wait_obj, UINT64_MAX);
        mutex_acquire(&tty->mutex);
    }

    // consume committed bytes until the line ends
    bool line_done = false;
    for(size_t i = 0; i < count; i++) {
        uint8_t c;
        if(!tty_read_lockless(tty, &c)) {
            line_done = true;
            break;
        }
        read_buffer[i] = c;
        request->read.bytes_read = i + 1;
        if(c == '\n') {
            line_done = true;
            break;
        }
    }

    if(tty->mode.canonical && line_done) {
        ATOMIC_LOAD_SUB(&tty->line_count, 1, ATOMIC_RELEASE);
    }

    mutex_release(&tty->mutex);
    return VFS_RESULT_OK;
}

vfs_result_t tty_perform_io(void* ctx, io_request_t* request) {
    tty_t* tty = (tty_t*) ctx;
    if(request->type == IO_REQUEST_READ) {
        return tty_vfs_read(tty, request);
    } else if(request->type == IO_REQUEST_WRITE) {
        for(size_t i = 0; i < request->write.count; i++) {
            bool success = tty_write(tty, ((uint8_t*) request->write.buffer)[i]);
            if(!success) {
                request->write.bytes_written = i;
                return VFS_RESULT_ERR_NO_SPACE;
            }
        }
        request->write.bytes_written = request->write.count;
    }

    return VFS_RESULT_OK;
}

const devfs_device_ops_t g_tty_devfs_ops = { .perform_io = tty_perform_io, .get_attributes = nullptr };
