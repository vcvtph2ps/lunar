#include <common/cpu_local.h>
#include <common/fs/io.h>
#include <common/sched/process.h>
#include <common/sched/sched.h>
#include <common/sched/thread.h>
#include <common/sync/lock.h>
#include <common/sync/wait_obj.h>
#include <common/userspace/fd_store.h>
#include <common/userspace/pipe.h>
#include <common/userspace/signal.h>
#include <lib/helpers.h>
#include <lib/math.h>
#include <lib/string.h>
#include <memory/heap.h>
#include <stddef.h>
#include <stdint.h>
#include <uapi/limits.h>
#include <uapi/signal.h>

/// @brief Instance of a pipe, both the reader and the writer fd point at the same instance
struct pipe {
    lock_t lock;

    /// Signaled whenever there is data for a blocked reader to consume, or the writers closed
    wait_obj_t read_wait;

    /// Signaled whenever there is room for a blocked writer, or the readers closed
    wait_obj_t write_wait;

    /// The ring buffer holding the buffered bytes
    uint8_t* buffer;

    /// The size of @p buffer in bytes
    size_t capacity;

    /// The index of the next byte to read
    size_t head;

    /// The index the next byte will be written to
    size_t tail;

    /// The number of bytes currently buffered
    size_t count;

    /// The number of open file descriptors referring to the reader end
    size_t readers_open;

    /// The number of open file descriptors referring to the writer end
    size_t writers_open;
};

/// @brief Copies @p count bytes into the pipe buffer, wrapping around the end of the ring
static void pipe_buffer_write(pipe_t* pipe, const uint8_t* source, size_t count) {
    size_t first = MATH_MIN(count, pipe->capacity - pipe->tail);
    memory_copy(pipe->buffer + pipe->tail, source, first);
    if(count > first) {
        memory_copy(pipe->buffer, source + first, count - first);
    }

    pipe->tail = (pipe->tail + count) % pipe->capacity;
    pipe->count += count;
}

/// @brief Copies @p count bytes out of the pipe buffer, wrapping around the end of the ring
static void pipe_buffer_read(pipe_t* pipe, uint8_t* destination, size_t count) {
    size_t first = MATH_MIN(count, pipe->capacity - pipe->head);
    memory_copy(destination, pipe->buffer + pipe->head, first);
    if(count > first) {
        memory_copy(destination + first, pipe->buffer, count - first);
    }

    pipe->head = (pipe->head + count) % pipe->capacity;
    pipe->count -= count;
}

/// @brief Raises SIGPIPE on the calling process for a write to a pipe with no readers
static void pipe_raise_sigpipe(void) {
    process_t* process = sched_arch_thread_current()->process;
    uapi_signal_info_t info = { .code = UAPI_SIGNAL_KERNEL, .signal_number = UAPI_SIGNAL_PIPE };
    signal_process(process, UAPI_SIGNAL_PIPE, &info);
}

/// @brief Reads up to the requested number of bytes out of the pipe
static vfs_result_t pipe_read(pipe_t* pipe, io_request_t* request) {
    uint8_t* buffer = request->read.buffer.buffer;
    size_t count = request->read.count;
    bool interrupted = false;

    request->read.bytes_read = 0;
    if(count == 0) {
        return VFS_RESULT_OK;
    }

    lock_acquire(&pipe->lock);
    for(;;) {
        /// Anything buffered can be returned immediately, even a partial read
        if(pipe->count > 0) {
            break;
        }

        /// No data, and no writer left to produce any: end of file
        if(pipe->writers_open == 0) {
            lock_release(&pipe->lock);
            return VFS_RESULT_OK;
        }

        if(request->no_block) {
            lock_release(&pipe->lock);
            return VFS_RESULT_ERR_WOULD_BLOCK;
        }

        /// A signal that interrupted an earlier wait still has to abort the read
        if(interrupted) {
            lock_release(&pipe->lock);
            return VFS_RESULT_ERR_INTERRUPTED;
        }

        lock_release(&pipe->lock);
        sched_wait_single_interruptible(&pipe->read_wait, UINT64_MAX);
        interrupted = sched_thread_interrupted();
        lock_acquire(&pipe->lock);
    }

    size_t to_read = MATH_MIN(count, pipe->count);
    pipe_buffer_read(pipe, buffer, to_read);
    bool more = pipe->count > 0;
    lock_release(&pipe->lock);

    /// Space was freed, let a blocked writer make progress
    wait_obj_signal(&pipe->write_wait, 1);

    /// Data remains buffered, let another blocked reader make progress
    if(more) {
        wait_obj_signal(&pipe->read_wait, 1);
    }

    request->read.bytes_read = to_read;
    return VFS_RESULT_OK;
}

/// @brief Writes up to the requested number of bytes into the pipe
static vfs_result_t pipe_write(pipe_t* pipe, io_request_t* request) {
    const uint8_t* buffer = request->write.buffer.buffer;
    size_t count = request->write.count;
    bool interrupted = false;

    request->write.bytes_written = 0;
    if(count == 0) {
        return VFS_RESULT_OK;
    }

    /// Writes of up to PIPE_BUF bytes must not be interleaved with other writes
    bool atomic = count <= UAPI_LIMITS_PIPE_BUF;

    lock_acquire(&pipe->lock);
    for(;;) {
        /// A pipe with no readers is broken: raise SIGPIPE and report EPIPE
        if(pipe->readers_open == 0) {
            lock_release(&pipe->lock);
            pipe_raise_sigpipe();
            return VFS_RESULT_ERR_PIPE;
        }

        size_t free_space = pipe->capacity - pipe->count;

        /// An atomic write needs room for the whole buffer, a larger one only needs a single byte
        if(atomic ? (free_space >= count) : (free_space > 0)) {
            break;
        }

        if(request->no_block) {
            lock_release(&pipe->lock);
            return VFS_RESULT_ERR_WOULD_BLOCK;
        }

        if(interrupted) {
            lock_release(&pipe->lock);
            return VFS_RESULT_ERR_INTERRUPTED;
        }

        lock_release(&pipe->lock);
        sched_wait_single_interruptible(&pipe->write_wait, UINT64_MAX);
        interrupted = sched_thread_interrupted();
        lock_acquire(&pipe->lock);
    }

    /// A write larger than PIPE_BUF may be satisfied by a partial transfer
    size_t to_write = atomic ? count : MATH_MIN(count, pipe->capacity - pipe->count);
    pipe_buffer_write(pipe, buffer, to_write);
    bool space_left = pipe->count < pipe->capacity;
    lock_release(&pipe->lock);

    /// Data is available, let a blocked reader make progress
    wait_obj_signal(&pipe->read_wait, 1);

    /// Room remains, let another blocked writer make progress
    if(space_left) {
        wait_obj_signal(&pipe->write_wait, 1);
    }

    request->write.bytes_written = to_write;
    return VFS_RESULT_OK;
}

/// @brief Performs IO on a pipe file descriptor
static vfs_result_t pipe_perform_io(fd_store_file_t* file, io_request_t* request) {
    pipe_t* pipe = file->data;

    switch(request->type) {
        case IO_REQUEST_READ:  return pipe_read(pipe, request);
        case IO_REQUEST_WRITE: return pipe_write(pipe, request);
        case IO_REQUEST_IOCTL: return VFS_RESULT_ERR_UNSUPPORTED_IOCTL;
    }

    return VFS_RESULT_ERR_UNSUPPORTED;
}

/// @brief Reports a pipe as a FIFO
static vfs_result_t pipe_get_attributes(fd_store_file_t* file, vfs_node_attr_t* out_attr) {
    (void) file;

    out_attr->size = 0;
    out_attr->type = VFS_NODE_TYPE_FIFO;
    out_attr->inode = 0;
    out_attr->permissions = (vfs_node_permissions_t) {
        .user = VFS_PERM_READ | VFS_PERM_WRITE,
        .group = VFS_PERM_READ | VFS_PERM_WRITE,
        .other = VFS_PERM_READ | VFS_PERM_WRITE,
    };
    return VFS_RESULT_OK;
}

/// @brief Drops one endpoint of a pipe, freeing it once both ends are closed
static void pipe_release(fd_store_file_t* file) {
    pipe_t* pipe = file->data;

    lock_acquire(&pipe->lock);

    if(file->access.read) {
        assert(pipe->readers_open > 0);
        pipe->readers_open--;
    } else {
        assert(pipe->writers_open > 0);
        pipe->writers_open--;
    }

    bool no_readers = pipe->readers_open == 0;
    bool no_writers = pipe->writers_open == 0;

    uint8_t* buffer = pipe->buffer;
    size_t capacity = pipe->capacity;
    lock_release(&pipe->lock);

    if(no_writers) {
        /// Wake blocked readers so they observe the end of file
        wait_obj_signal(&pipe->read_wait, 1);
    }
    if(no_readers) {
        /// Wake blocked writers so they observe the broken pipe
        wait_obj_signal(&pipe->write_wait, 1);
    }

    if(no_readers && no_writers) {
        heap_free(buffer, capacity);
        heap_free(pipe, sizeof(pipe_t));
    }
}

const fd_store_file_ops_t g_pipe_file_ops = {
    .perform_io = pipe_perform_io,
    .get_attributes = pipe_get_attributes,
    .release = pipe_release,
};

pipe_t* pipe_create() {
    pipe_t* pipe = heap_alloc(sizeof(pipe_t));
    if(pipe == nullptr) {
        return nullptr;
    }

    pipe->buffer = heap_alloc(PIPE_CAPACITY);
    if(pipe->buffer == nullptr) {
        heap_free(pipe, sizeof(pipe_t));
        return nullptr;
    }

    pipe->lock = LOCK_INIT;
    pipe->read_wait = WAIT_OBJ_INIT_SYNCHRONIZATION();
    pipe->write_wait = WAIT_OBJ_INIT_SYNCHRONIZATION();
    pipe->capacity = PIPE_CAPACITY;
    pipe->head = 0;
    pipe->tail = 0;
    pipe->count = 0;
    pipe->readers_open = 1;
    pipe->writers_open = 1;

    return pipe;
}
