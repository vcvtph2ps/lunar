#include <common/fs/devfs.h>
#include <common/fs/io.h>
#include <common/fs/vfs.h>
#include <common/log.h>
#include <common/sched/process.h>
#include <common/sched/process_group.h>
#include <common/sched/sched.h>
#include <common/sync/lock.h>
#include <common/sync/wait_obj.h>
#include <common/userspace/signal.h>
#include <common/userspace/tty.h>
#include <lib/helpers.h>
#include <lib/string.h>
#include <memory/heap.h>
#include <uapi/ioctls.h>
#include <uapi/signal.h>
#include <uapi/termios.h>

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

static tty_t* g_default_tty = nullptr;

void tty_set_default(tty_t* tty) {
    g_default_tty = tty;
}

tty_t* tty_get_default() {
    return g_default_tty;
}

tty_t* tty_create() {
    tty_t* tty = heap_alloc(sizeof(tty_t));
    tty->lock = LOCK_INIT;
    tty->wait_obj = WAIT_OBJ_INIT_SYNCHRONIZATION();
    tty->head = 0;
    tty->tail = 0;
    tty->commit = 0;
    tty->eof = false;
    ATOMIC_STORE(&tty->foreground_pgid, 0, ATOMIC_RELAXED);

    tty->current_mode.canonical = true;
    tty->current_mode.echo = true;
    tty->current_mode.echo_ctrl = true;
    tty->current_mode.signal = true;
    ATOMIC_STORE(&tty->line_count, 0, ATOMIC_RELAXED);

    tty->current_mode.control_chars.backspace = CHAR_BACKSPACE;
    tty->current_mode.control_chars.intr = CHAR_INTR;
    tty->current_mode.control_chars.eof = CHAR_EOF;
    tty->current_mode.control_chars.kill = CHAR_KILL;
    tty->current_mode.control_chars.erase_word = CHAR_ERASE_WORD;
    tty->current_mode.control_chars.del = CHAR_DEL;

    tty->on_write = nullptr;
    tty->write_ctx = nullptr;
    tty->on_flush = nullptr;
    tty->flush_ctx = nullptr;

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

void tty_flush(tty_t* tty) {
    if(tty->on_flush != nullptr) {
        tty->on_flush(tty->flush_ctx);
    }
}

void tty_recv_generic(void* ctx, char c) {
    tty_t* tty = (tty_t*) ctx;
    if(!tty_put(tty, (uint8_t) c)) {
        LOG_WARN("tty full, dropping input\n");
    }
}

/// @brief Echo a single byte to the output if enabled
static void tty_echo(tty_t* tty, uint8_t c) {
    if(tty->on_write == nullptr || !tty->current_mode.echo) {
        return;
    }
    if(c == '\n') {
        tty->on_write(tty->write_ctx, '\r');
    }
    tty->on_write(tty->write_ctx, c);
}

/// @brief Echo a control character as ^X if enabled
static void tty_echo_ctrl(tty_t* tty, uint8_t c) {
    if(tty->on_write == nullptr || !tty->current_mode.echo || !tty->current_mode.echo_ctrl) {
        return;
    }
    tty->on_write(tty->write_ctx, '^');
    uint8_t plain = (c == CHAR_DEL) ? '?' : (uint8_t) (c + 0x40);
    tty->on_write(tty->write_ctx, plain);
}

/// @brief Echo a line ending, moving the cursor back to column 0 on the next line
static void tty_echo_newline(tty_t* tty) {
    if(tty->on_write == nullptr || !tty->current_mode.echo) {
        return;
    }
    tty->on_write(tty->write_ctx, '\r');
    tty->on_write(tty->write_ctx, '\n');
}

/// @brief Echo an input byte the way it should appear on screen
static void tty_echo_char(tty_t* tty, uint8_t c) {
    if(!tty->current_mode.echo) {
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

/// @brief Number of screen columns the echo of a byte occupies
static int tty_echo_cols(uint8_t c) {
    if((c < 0x20 && c != '\t') || c == CHAR_DEL) {
        return 2;
    }
    return 1;
}

/// @note Must hold the tty lock
static bool tty_put_raw(tty_t* tty, uint8_t c) {
    if(tty_full(tty)) {
        return false;
    }
    tty->buf[tty->head] = c;
    tty->head = ring_next(tty->head);
    return true;
}

/// @note Must hold the tty lock. erases the last pending byte, undoing its echo
static void tty_erase_one(tty_t* tty) {
    if(tty_pending_empty(tty)) {
        return;
    }
    uint8_t last = tty->buf[ring_prev(tty->head)];
    tty->head = ring_prev(tty->head);
    if(tty->on_write == nullptr || !tty->current_mode.echo) {
        return;
    }
    int cols = tty_echo_cols(last);
    for(int i = 0; i < cols; i++) {
        tty->on_write(tty->write_ctx, '\b');
        tty->on_write(tty->write_ctx, ' ');
        tty->on_write(tty->write_ctx, '\b');
    }
}

/// @note Must hold the tty lock
static void tty_erase_char(tty_t* tty) {
    if(tty_pending_empty(tty)) {
        tty_echo(tty, '\a');
        return;
    }
    tty_erase_one(tty);
}

/// @note Must hold the tty lock
static void tty_kill_line(tty_t* tty) {
    while(!tty_pending_empty(tty)) {
        tty_erase_one(tty);
    }
}

/// @note Must hold the tty lock
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

/// @note Must hold the tty lock
static void tty_handle_eof(tty_t* tty) {
    if(tty_pending_empty(tty)) {
        tty->eof = true;
        return;
    }
    tty->commit = tty->head;
    tty->eof = false;
    ATOMIC_LOAD_ADD(&tty->line_count, 1, ATOMIC_RELEASE);
}

/// @note Must hold the tty lock
static void tty_handle_intr(tty_t* tty, uint8_t c) {
    tty_kill_line(tty);
    tty_echo_ctrl(tty, c);
    tty_echo_newline(tty);
    if(tty->current_mode.signal)
        tty_signal_foreground(tty, UAPI_SIGNAL_INT);
}

static bool tty_put_canonical(tty_t* tty, uint8_t c) {
    // ICRNL: translate carriage returns to newlines before processing
    // @todo: this is a setting
    if(c == '\r') {
        c = '\n';
    }

    const tty_mode_t* mode = &tty->current_mode;
    bool wake = false;
    if(c == '\n') {
        // Commit the current line, making it visible to readers.
        if(tty_put_raw(tty, '\n')) {
            tty->commit = tty->head;
            tty->eof = false;
            ATOMIC_LOAD_ADD(&tty->line_count, 1, ATOMIC_RELEASE);
            tty_echo_newline(tty);
            wake = true;
        }
    } else if(c == mode->control_chars.backspace || c == mode->control_chars.del) {
        tty_erase_char(tty);
    } else if(c == mode->control_chars.kill) {
        tty_kill_line(tty);
    } else if(c == mode->control_chars.erase_word) {
        tty_erase_word(tty);
    } else if(mode->signal && c == mode->control_chars.intr) {
        tty_handle_intr(tty, c);
    } else if(c == mode->control_chars.eof) {
        tty_handle_eof(tty);
        wake = true;
    } else if(tty_put_raw(tty, c)) {
        tty->eof = false;
        tty_echo_char(tty, c);
    }

    if(wake) {
        wait_obj_signal(&tty->wait_obj, 1);
    }
    return true;
}

bool tty_put(tty_t* tty, uint8_t c) {
    lock_acquire(&tty->lock);

    if(tty->current_mode.canonical) {
        bool res = tty_put_canonical(tty, c);
        lock_release(&tty->lock);
        tty_flush(tty);
        return res;
    }

    // for raw mode everything is committed immediately.
    bool ok = tty_put_raw(tty, c);
    if(ok) {
        tty->commit = tty->head;
        tty->eof = false;
        tty_echo_char(tty, c);
    }
    lock_release(&tty->lock);

    tty_flush(tty);

    if(ok) {
        wait_obj_signal(&tty->wait_obj, 1);
    }
    return ok;
}

/// @note Must hold the tty lock, only consumes bytes up to commit
static bool tty_read_lockless(tty_t* tty, uint8_t* c) {
    if(!tty_readable(tty)) {
        return false;
    }

    *c = tty->buf[tty->tail];
    tty->tail = ring_next(tty->tail);

    return true;
}

bool tty_read(tty_t* tty, uint8_t* c) {
    lock_acquire(&tty->lock);
    bool result = tty_read_lockless(tty, c);
    lock_release(&tty->lock);

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

void tty_set_foreground_pgid(tty_t* tty, uint32_t pgid) {
    ATOMIC_STORE(&tty->foreground_pgid, pgid, ATOMIC_RELEASE);
}

uint32_t tty_get_foreground_pgid(tty_t* tty) {
    return ATOMIC_LOAD(&tty->foreground_pgid, ATOMIC_ACQUIRE);
}

void tty_signal_foreground(tty_t* tty, int signal_number) {
    uint32_t pgid = tty_get_foreground_pgid(tty);
    if(pgid == 0) {
        return;
    }

    size_t count = 0;
    process_t** processes = process_group_find_processes(pgid, &count);
    if(processes == nullptr) {
        return;
    }

    uapi_signal_info_t info = { .signal_number = signal_number, .code = UAPI_SIGNAL_KERNEL, .pid = 0 };
    for(size_t i = 0; i < count; i++) {
        signal_process(processes[i], signal_number, &info);
        process_deref(processes[i]);
    }

    heap_free(processes, sizeof(process_t*) * count);
}

static vfs_result_t tty_vfs_read(tty_t* tty, io_request_t* request) {
    // @todo: user buffers
    uint8_t* read_buffer = request->read.buffer.buffer;
    size_t count = request->read.count;

    lock_acquire(&tty->lock);

    // wait until there is committed data, or EOF.
    while(!tty_readable(tty)) {
        if(tty->eof) {
            request->read.bytes_read = 0;
            tty->eof = false;
            lock_release(&tty->lock);
            return VFS_RESULT_OK;
        }

        lock_release(&tty->lock);

        if(request->no_block) {
            return VFS_RESULT_ERR_WOULD_BLOCK;
        }

        sched_wait_single_interruptible(&tty->wait_obj, UINT64_MAX);
        bool interrupted = sched_thread_interrupted();
        lock_acquire(&tty->lock);

        /// a signal arrived while we were blocked, report EINTR if there is still nothing to read
        if(!tty_readable(tty) && interrupted) {
            lock_release(&tty->lock);
            return VFS_RESULT_ERR_INTERRUPTED;
        }
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

    if(tty->current_mode.canonical && line_done) {
        ATOMIC_LOAD_SUB(&tty->line_count, 1, ATOMIC_RELEASE);
    }

    lock_release(&tty->lock);
    return VFS_RESULT_OK;
}

static inline uapi_termios_t tty_to_termios(tty_mode_t mode) {
    uapi_termios_t termios = {};
    if(mode.canonical)
        termios.c_lflag |= UAPI_TERMIOS_ICANON;
    if(mode.echo)
        termios.c_lflag |= UAPI_TERMIOS_ECHO;
    if(mode.echo_ctrl)
        termios.c_lflag |= UAPI_TERMIOS_ECHOCTL;
    if(mode.signal)
        termios.c_lflag |= UAPI_TERMIOS_ISIG;
    termios.c_cc[UAPI_TERMIOS_VINTR] = mode.control_chars.intr;
    termios.c_cc[UAPI_TERMIOS_VERASE] = mode.control_chars.backspace;
    termios.c_cc[UAPI_TERMIOS_VKILL] = mode.control_chars.kill;
    termios.c_cc[UAPI_TERMIOS_VEOF] = mode.control_chars.eof;
    termios.c_cc[UAPI_TERMIOS_VWERASE] = mode.control_chars.erase_word;
    return termios;
}

static inline uint8_t tty_control_char(uint8_t value, uint8_t fallback) {
    /// @note A zero c_cc entry means the control character is disabled
    return value == 0 ? fallback : value;
}

static inline tty_mode_t termios_to_tty(uapi_termios_t termios) {
    tty_mode_t mode = {};
    mode.canonical = (termios.c_lflag & UAPI_TERMIOS_ICANON) != 0;
    mode.echo = (termios.c_lflag & UAPI_TERMIOS_ECHO) != 0;
    mode.echo_ctrl = (termios.c_lflag & UAPI_TERMIOS_ECHOCTL) != 0;
    mode.signal = (termios.c_lflag & UAPI_TERMIOS_ISIG) != 0;
    mode.control_chars.intr = tty_control_char(termios.c_cc[UAPI_TERMIOS_VINTR], CHAR_INTR);
    mode.control_chars.backspace = tty_control_char(termios.c_cc[UAPI_TERMIOS_VERASE], CHAR_BACKSPACE);
    mode.control_chars.kill = tty_control_char(termios.c_cc[UAPI_TERMIOS_VKILL], CHAR_KILL);
    mode.control_chars.eof = tty_control_char(termios.c_cc[UAPI_TERMIOS_VEOF], CHAR_EOF);
    mode.control_chars.erase_word = tty_control_char(termios.c_cc[UAPI_TERMIOS_VWERASE], CHAR_ERASE_WORD);
    mode.control_chars.del = CHAR_DEL;
    return mode;
}

static vfs_result_t tty_ioctl(tty_t* tty, io_request_t* request) {
    (void) tty;
    if(request->ioctl.request == UAPI_IOCTLS_TIOCGWINSZ) {
        uapi_ioctls_winsize_t winsize;
        winsize.ws_col = 80;
        winsize.ws_row = 20;
        winsize.ws_xpixel = 0;
        winsize.ws_ypixel = 0;
        size_t sz = vm_copy_to(request->ioctl.buffer.address_space, (uintptr_t) request->ioctl.buffer.buffer, &winsize, sizeof(winsize));
        if(sz != sizeof(winsize)) {
            return VFS_RESULT_ERR_FAULT;
        }
        request->ioctl.buffer.size = sizeof(winsize);
        return VFS_RESULT_OK;
    } else if(request->ioctl.request == UAPI_IOCTLS_TCGETS) {
        lock_acquire(&tty->lock);
        uapi_termios_t termios = tty_to_termios(tty->current_mode);
        lock_release(&tty->lock);
        size_t sz = vm_copy_to(request->ioctl.buffer.address_space, (uintptr_t) request->ioctl.buffer.buffer, &termios, sizeof(termios));
        if(sz != sizeof(termios)) {
            return VFS_RESULT_ERR_FAULT;
        }
        request->ioctl.buffer.size = sizeof(termios);
        return VFS_RESULT_OK;
    } else if(request->ioctl.request == UAPI_IOCTLS_TCSETS || request->ioctl.request == UAPI_IOCTLS_TCSETSW || request->ioctl.request == UAPI_IOCTLS_TCSETSF) {
        uapi_termios_t termios;
        size_t sz = vm_copy_from(&termios, request->ioctl.buffer.address_space, (uintptr_t) request->ioctl.buffer.buffer, sizeof(termios));
        if(sz != sizeof(termios)) {
            return VFS_RESULT_ERR_FAULT;
        }
        tty_mode_t mode = termios_to_tty(termios);
        lock_acquire(&tty->lock);
        switch(request->ioctl.request) {
            case UAPI_IOCTLS_TCSETS:  break;
            case UAPI_IOCTLS_TCSETSW: /* we need to flush the tty output buffer, tho we never *really* handle that */ break;
            case UAPI_IOCTLS_TCSETSF:
                tty->head = 0;
                tty->tail = 0;
                tty->commit = 0;
                tty->eof = false;
                ATOMIC_STORE(&tty->line_count, 0, ATOMIC_RELEASE);
                break;
        }
        tty->current_mode = mode;
        lock_release(&tty->lock);
        return VFS_RESULT_OK;
    }

    return VFS_RESULT_ERR_UNSUPPORTED_IOCTL;
}

vfs_result_t tty_perform_io(void* ctx, io_request_t* request) {
    tty_t* tty = (tty_t*) ctx;
    if(request->type == IO_REQUEST_READ) {
        return tty_vfs_read(tty, request);
    } else if(request->type == IO_REQUEST_IOCTL) {
        return tty_ioctl(tty, request);
    } else if(request->type == IO_REQUEST_WRITE) {
        for(size_t i = 0; i < request->write.count; i++) {
            // @todo: user buffers
            bool success = tty_write(tty, ((uint8_t*) request->write.buffer.buffer)[i]);
            if(!success) {
                request->write.bytes_written = i;
                tty_flush(tty);
                return VFS_RESULT_ERR_NO_SPACE;
            }
        }
        request->write.bytes_written = request->write.count;
        tty_flush(tty);
        return VFS_RESULT_OK;
    }

    return VFS_RESULT_ERR_UNSUPPORTED;
}

const devfs_device_ops_t g_tty_devfs_ops = { .perform_io = tty_perform_io, .get_attributes = nullptr };
