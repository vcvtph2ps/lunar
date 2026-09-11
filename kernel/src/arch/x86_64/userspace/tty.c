#include <common/fs/devfs.h>
#include <common/fs/io.h>
#include <common/log.h>
#include <common/sync/mutex.h>
#include <common/sync/wait_queue.h>
#include <common/userspace/tty.h>
#include <lib/string.h>
#include <memory/heap.h>

tty_t* tty_create() {
    tty_t* tty = heap_alloc(sizeof(tty_t));
    tty->mutex = MUTEX_INIT;
    tty->queue = WAIT_QUEUE_INIT;
    tty->head = 0;
    tty->tail = 0;
    tty->on_write = nullptr;
    tty->write_ctx = nullptr;
    memory_zero(tty->buf, TTY_RB_SIZE);
    return tty;
}

void tty_free(tty_t* tty) {
    assert(tty->queue.list.count == 0 && "tty freed with waiting threads");
    heap_free(tty, sizeof(tty_t));
}

static int tty_empty(const tty_t* tty) {
    return tty->head == tty->tail;
}

static int tty_full(const tty_t* tty) {
    return (tty->head + 1) % TTY_RB_SIZE == tty->tail;
}

bool tty_write(tty_t* tty, uint8_t c) {
    if(tty->on_write != nullptr) {
        tty->on_write(tty->write_ctx, c);
    }
    return true;
}

void tty_recv_generic(void* ctx, char c) {
    tty_t* tty = (tty_t*) ctx;
    if(c == '\r') {
        c = '\n';
    }
    if(!tty_put(tty, (uint8_t) c)) {
        LOG_WARN("tty full, dropping input\n");
    }
}

vfs_result_t tty_perform_io(void* ctx, io_request_t* request) {
    tty_t* tty = (tty_t*) ctx;
    if(request->type == IO_REQUEST_READ) {
        // @todo: tty settings
        uint8_t* read_buffer = request->read.buffer;
        size_t bytes_read = 0;
        for(size_t i = 0; i < request->read.count; i++) {
            char c = tty_read_blocking(tty);
            read_buffer[i] = c;
            if(c == '\n') {
                bytes_read = i + 1;
                break;
            }
        }
        request->read.bytes_read = bytes_read;
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

bool tty_put(tty_t* tty, uint8_t c) {
    mutex_acquire(&tty->mutex);

    if(tty_full(tty)) {
        mutex_release(&tty->mutex);
        return false;
    }

    tty->buf[tty->head] = c;
    tty->head = (tty->head + 1) % TTY_RB_SIZE;

    mutex_release(&tty->mutex);

    wait_queue_wake_one(&tty->queue);
    return true;
}

static bool tty_read_lockless(tty_t* tty, uint8_t* c) {
    if(tty_empty(tty)) {
        return false;
    }

    *c = tty->buf[tty->tail];
    tty->tail = (tty->tail + 1) % TTY_RB_SIZE;

    return true;
}

bool tty_read(tty_t* tty, uint8_t* c) {
    mutex_acquire(&tty->mutex);
    bool result = tty_read_lockless(tty, c);
    mutex_release(&tty->mutex);
    if(result) {
        tty->on_write(tty->write_ctx, *c);
    }
    return result;
}

uint8_t tty_read_blocking(tty_t* tty) {
    while(1) {
        uint8_t c;
        if(tty_read(tty, &c)) {
            return c;
        }
        wait_queue_join(&tty->queue);
    }
}
