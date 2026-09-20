#include <common/cpu_local.h>
#include <common/fs/vfs.h>
#include <common/log.h>
#include <common/userspace/fd_store.h>
#include <common/userspace/syscall.h>
#include <common/userspace/syscall_defs.h>
#include <common/userspace/userspace.h>
#include <memory/heap.h>
#include <stdatomic.h>
#include <stdint.h>

#define O_RDONLY 00
#define O_WRONLY 01
#define O_RDWR 02

#define O_CREAT 0100
#define O_EXCL 0200
#define O_NOCTTY 0400
#define O_TRUNC 01000
#define O_APPEND 02000
#define O_NONBLOCK 04000
#define O_DSYNC 010000
#define O_ASYNC 020000
#define O_CLOEXEC 02000000
#define O_SYNC 04010000
#define O_RSYNC 04010000
#define O_NOATIME 01000000

syscall_ret_t syscall_sys_fs_open(syscall_args_t* args) {
    uintptr_t pathname_ubuffer = args->arg1;
    size_t pathname_ubuffer_size = args->arg2;
    int flags = args->arg3;
    uint32_t mode = args->arg4;
    (void) mode;

    process_t* process = CPU_LOCAL_GET_CURRENT_THREAD()->common.process;

    char* pathname = heap_alloc(pathname_ubuffer_size + 1);
    vm_copy_from(pathname, process->address_space, pathname_ubuffer, pathname_ubuffer_size);
    pathname[pathname_ubuffer_size] = '\0';

    vfs_node_t* out_result_node;
    vfs_result_t result = vfs_lookup(&VFS_MAKE_REL_PATH(process->current_working_dir, pathname), &out_result_node);
    heap_free(pathname, pathname_ubuffer_size + 1);
    LOG_UTRC("pathname=%s, flags=%x, mode=%d | result=%d\n", pathname, flags, mode, result);

    // user_assert(flags == 0 && "unimplemented");
    // user_assert(mode == 0 && "unimplemented");
    user_assert(pathname_ubuffer_size <= 1024);

    switch(result) {
        case VFS_RESULT_OK:              break;
        case VFS_RESULT_ERR_NOT_FOUND:   return SYSCALL_RET_ERROR(SYSCALL_ERROR_NOENT);
        case VFS_RESULT_ERR_WOULD_BLOCK: return SYSCALL_RET_ERROR(SYSCALL_ERROR_AGAIN);
        default:                         user_assert("Invalid return value");
    }

    fd_store_entry_t* entry;
    uint32_t fd = fd_store_create_fd(process->fd_store, out_result_node, &entry);
    // @note: since vfs_lookup and fd_store_create_fd bump ref count, we drop our ref from vfs_lookup
    vfs_node_put(out_result_node);

    int access_flags = flags & O_RDWR;
    entry->access.read = (access_flags == O_RDONLY || access_flags == O_RDWR);
    entry->access.write = (access_flags == O_WRONLY || access_flags == O_RDWR);
    entry->non_blocking = (flags & O_NONBLOCK) != 0;
    return SYSCALL_RET_VALUE(fd);
}

syscall_ret_t syscall_sys_fs_close(syscall_args_t* args) {
    uint32_t fd = args->arg1;

    process_t* process = CPU_LOCAL_GET_CURRENT_THREAD()->common.process;

    fd_store_entry_t* entry = fd_store_get_fd(process->fd_store, fd);
    if(entry == nullptr) {
        LOG_UTRC("fd=%d | result=BADFD\n", fd);
        return SYSCALL_RET_ERROR(SYSCALL_ERROR_BADFD);
    }

    fd_store_free_fd(process->fd_store, fd);
    LOG_UTRC("fd=%d | result=0\n", fd);
    return SYSCALL_RET_VALUE(0);
}

syscall_ret_t syscall_sys_fs_read(syscall_args_t* args) {
    uint32_t fd = args->arg1;
    uintptr_t ubuffer = args->arg2;
    size_t ubuffer_size = args->arg3;

    process_t* process = CPU_LOCAL_GET_CURRENT_THREAD()->common.process;

    fd_store_entry_t* entry = fd_store_get_fd(process->fd_store, fd);
    if(entry == nullptr) {
        LOG_UTRC("fd=%d, ubuffer=0x%lx, count=%ld | result=BADFD\n", fd, ubuffer, ubuffer_size);
        return SYSCALL_RET_ERROR(SYSCALL_ERROR_BADFD);
    }

    if(!entry->access.read) {
        LOG_UTRC("fd=%d, ubuffer=0x%lx, count=%ld, access.read=false | result=BADFD\n", fd, ubuffer, ubuffer_size);
        return SYSCALL_RET_ERROR(SYSCALL_ERROR_BADFD);
    }

    char* buffer = heap_alloc(ubuffer_size);

    io_request_t io_req;
    io_req.type = IO_REQUEST_READ;
    io_req.read.buffer = buffer;
    io_req.read.count = ubuffer_size;
    io_req.read.offset = entry->offset;
    io_req.read.bytes_read = 0;
    io_req.no_block = entry->non_blocking;

    vfs_result_t result = vfs_perform_io_node(entry->node, &io_req);
    switch(result) {
        case VFS_RESULT_OK: break;
        default:            user_assert("Invalid return value");
    }

    entry->offset += io_req.read.bytes_read;

    vm_copy_to(process->address_space, ubuffer, buffer, io_req.read.bytes_read);
    heap_free(buffer, ubuffer_size);
    LOG_UTRC("fd=%d, ubuffer=0x%lx, count=%ld, offset=%ld | result=%ld\n", fd, ubuffer, ubuffer_size, io_req.read.offset, io_req.read.bytes_read);

    return SYSCALL_RET_VALUE(io_req.read.bytes_read);
}

syscall_ret_t syscall_sys_fs_write(syscall_args_t* args) {
    uint32_t fd = args->arg1;
    uintptr_t ubuffer = args->arg2;
    size_t ubuffer_size = args->arg3;

    process_t* process = CPU_LOCAL_GET_CURRENT_THREAD()->common.process;

    fd_store_entry_t* entry = fd_store_get_fd(process->fd_store, fd);
    if(entry == nullptr) {
        LOG_UTRC("fd=%d, ubuffer=0x%lx, count=%ld | result=BADFD\n", fd, ubuffer, ubuffer_size);
        return SYSCALL_RET_ERROR(SYSCALL_ERROR_BADFD);
    }

    if(!entry->access.write) {
        LOG_UTRC("fd=%d, ubuffer=0x%lx, count=%ld, access.write=false | result=BADFD\n", fd, ubuffer, ubuffer_size);
        return SYSCALL_RET_ERROR(SYSCALL_ERROR_BADFD);
    }

    char* buffer = heap_alloc(ubuffer_size);
    vm_copy_from(buffer, process->address_space, ubuffer, ubuffer_size);

    io_request_t io_req;
    io_req.type = IO_REQUEST_WRITE;
    io_req.write.buffer = buffer;
    io_req.write.count = ubuffer_size;
    io_req.write.offset = entry->offset;
    io_req.write.bytes_written = 0;
    io_req.no_block = entry->non_blocking;

    vfs_result_t result = vfs_perform_io_node(entry->node, &io_req);
    switch(result) {
        case VFS_RESULT_OK: break;
        default:            user_assert("Invalid return value");
    }

    entry->offset += io_req.write.bytes_written;

    heap_free(buffer, ubuffer_size);
    LOG_UTRC("fd=%d, ubuffer=0x%lx, count=%ld, offset=%ld | result=%ld\n", fd, ubuffer, ubuffer_size, io_req.write.offset, io_req.write.bytes_written);

    return SYSCALL_RET_VALUE(io_req.write.bytes_written);
}

syscall_ret_t syscall_sys_fs_is_a_tty(syscall_args_t* args) {
    uint32_t fd = args->arg1;
    LOG_UTRC("fd=%d\n", fd);

    // @todo: STUB
    if(fd == 0 || fd == 1 || fd == 2) {
        return SYSCALL_RET_VALUE(0);
    }

    // fd_store_t* store = CPU_LOCAL_GET_CURRENT_THREAD()->common.process->fd_store;

    return SYSCALL_RET_ERROR(SYSCALL_ERROR_NOTTY);
}

/// Seek from beginning of file.
#define SEEK_SET 0
/// Seek from current position.
#define SEEK_CUR 1
/// Seek from end of file.
#define SEEK_END 2

syscall_ret_t syscall_sys_fs_seek(syscall_args_t* args) {
    uint32_t fd = args->arg1;
    uint64_t offset = args->arg2;
    int whence = args->arg3;
    process_t* process = CPU_LOCAL_GET_CURRENT_THREAD()->common.process;

    if(fd == 0 || fd == 1 || fd == 2) {
        return SYSCALL_RET_VALUE(0);
    }

    fd_store_entry_t* entry = fd_store_get_fd(process->fd_store, fd);
    if(entry == nullptr) {
        LOG_UTRC("fd=%d | result=BADFD\n", fd);
        return SYSCALL_RET_ERROR(SYSCALL_ERROR_BADFD);
    }

    uint64_t new_offset;
    if(whence == SEEK_SET) {
        new_offset = offset;
    } else if(whence == SEEK_CUR) {
        new_offset = entry->offset + offset;
    } else if(whence == SEEK_END) {
        new_offset = 0;
        user_assert(whence != SEEK_END && "unimplemented");
    } else {
        LOG_UTRC("fd=%d offset=%ld whence=%d | result=EINVAL\n", fd, offset, whence);
        return SYSCALL_RET_ERROR(SYSCALL_ERROR_INVAL);
    }

    entry->offset = new_offset;
    LOG_UTRC("fd=%d offset=%ld whence=%d | result=%ld\n", fd, offset, whence, new_offset);
    return SYSCALL_RET_VALUE(0);
}
