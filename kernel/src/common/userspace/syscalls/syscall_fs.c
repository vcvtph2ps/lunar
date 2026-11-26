#include <common/cpu_local.h>
#include <common/fs/vfs.h>
#include <common/log.h>
#include <common/userspace/fd_store.h>
#include <common/userspace/pipe.h>
#include <common/userspace/syscall.h>
#include <common/userspace/syscall_defs.h>
#include <common/userspace/userptr.h>
#include <common/userspace/userspace.h>
#include <lib/math.h>
#include <lib/string.h>
#include <memory/heap.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <uapi/dirent.h>
#include <uapi/fcntl.h>
#include <uapi/limits.h>
#include <uapi/stat.h>
#include <uapi/unistd.h>

#include "common/fs/io.h"

/// @brief Turns the POSIX mode bits passed to open() into VFS permission bits
static vfs_node_permissions_t permissions_from_posix_mode(uint32_t mode) {
    return (vfs_node_permissions_t) {
        .user = (vfs_permissions_t) ((mode >> 6) & 0x7),
        .group = (vfs_permissions_t) ((mode >> 3) & 0x7),
        .other = (vfs_permissions_t) (mode & 0x7),
    };
}

SYSCALL_DEFINE(syscall_fs_open, SYSCALL_FS_OPEN, int32_t, void*, path, int, flags, uint32_t, mode);
static syscall_ret_t syscall_fs_open(syscall_args_t* argument_data, void* path_ptr, int flags, uint32_t mode) {
    (void) argument_data;

    process_t* process = CPU_LOCAL_GET_CURRENT_THREAD()->common.process;
    userptr_t* upath = userptr_create_string((uintptr_t) path_ptr, UAPI_LIMITS_PATH_MAX - 1);
    if(!upath) {
        return SYSCALL_RET_ERROR(SYSCALL_ERROR_TOO_BIG);
    }

    size_t path_size = upath->size + 1;
    char* path = heap_alloc(path_size);
    if(path == nullptr) {
        heap_free(upath, sizeof(userptr_t));
        return SYSCALL_RET_ERROR(SYSCALL_ERROR_NOMEM);
    }

    if(upath->size != 0 && !userptr_read_full(upath, path)) {
        heap_free(path, path_size);
        heap_free(upath, sizeof(userptr_t));
        return SYSCALL_RET_ERROR(SYSCALL_ERROR_FAULT);
    }

    path[upath->size] = '\0';
    heap_free(upath, sizeof(userptr_t));

    vfs_path_t vfs_path = VFS_MAKE_REL_PATH(process->current_working_dir, path);

    vfs_node_t* out_result_node = nullptr;
    vfs_result_t result = vfs_lookup(&vfs_path, (flags & UAPI_O_NOFOLLOW) == 0, &out_result_node);

    bool create = (flags & UAPI_O_CREATE) != 0;
    bool exclusive = (flags & UAPI_O_EXCL) != 0;
    bool truncate = (flags & UAPI_O_TRUNC) != 0;

    fd_store_access_t fd_access = (fd_store_access_t) { .read = (flags & UAPI_O_RDONLY) != 0, .write = (flags & UAPI_O_WRONLY) != 0 };
    fd_store_flags_t fd_flags = (fd_store_flags_t) {
        .non_blocking = (flags & UAPI_O_NONBLOCK) != 0,
        .append = (flags & UAPI_O_APPEND) != 0,
    };
    bool close_on_execute = (flags & UAPI_O_CLOEXEC) != 0;

    if(result == VFS_RESULT_OK && create && exclusive) {
        /// The entry already exists and O_EXCL was passed
        vfs_node_put(out_result_node);
        out_result_node = nullptr;
        result = VFS_RESULT_ERR_EXISTS;
    } else if(result == VFS_RESULT_ERR_NOT_FOUND && create) {
        result = vfs_create(&vfs_path, VFS_NODE_TYPE_FILE, permissions_from_posix_mode(mode), &out_result_node);
    } else if(result == VFS_RESULT_OK && truncate && fd_access.write) {
        /// O_TRUNC is ignored unless the file is opened for writing
        result = vfs_truncate_node(out_result_node, 0);
        if(result != VFS_RESULT_OK) {
            vfs_node_put(out_result_node);
            out_result_node = nullptr;
        }
    }
    // @todo: UAPI_O_DIRECTORY

    LOG_UTRC("pathname=%s, flags=%x, mode=%d | result=%d\n", path, flags, mode, result);
    heap_free(path, path_size);

    if(syscall_from_vfs_result(result).is_error) {
        return syscall_from_vfs_result(result);
    }

    fd_store_file_t* file = fd_store_file_create_vfs_node(out_result_node, fd_access, fd_flags);
    // @note: Since vfs_lookup and fd_store_file_create_vfs_node bump ref count, we drop our ref from vfs_lookup
    vfs_node_put(out_result_node);
    uint32_t fd = fd_store_insert_file(process->fd_store, file, close_on_execute);

    return SYSCALL_RET_VALUE(fd);
}

SYSCALL_DEFINE(syscall_fs_close, SYSCALL_FS_CLOSE, void, uint32_t, fd);
static syscall_ret_t syscall_fs_close(syscall_args_t* argument_data, uint32_t fd) {
    (void) argument_data;

    process_t* process = CPU_LOCAL_GET_CURRENT_THREAD()->common.process;

    fd_store_file_t* file = fd_store_get_fd(process->fd_store, fd, nullptr);
    if(file == nullptr) {
        LOG_UTRC("fd=%d | result=EBADFD\n", fd);
        return SYSCALL_RET_ERROR(SYSCALL_ERROR_BADF);
    }

    fd_store_free_fd(process->fd_store, fd);
    fd_store_file_put(file);
    LOG_UTRC("fd=%d | result=0\n", fd);
    return SYSCALL_RET_VALUE(0);
}

SYSCALL_DEFINE(syscall_fs_read, SYSCALL_FS_READ, uint64_t, uint32_t, fd, void*, buffer, size_t, count);
static syscall_ret_t syscall_fs_read(syscall_args_t* argument_data, uint32_t fd, void* buffer_ptr, size_t count) {
    (void) argument_data;

    process_t* process = CPU_LOCAL_GET_CURRENT_THREAD()->common.process;

    fd_store_file_t* file = fd_store_get_fd(process->fd_store, fd, nullptr);
    if(file == nullptr) {
        LOG_UTRC("fd=%d, buffer=0x%lx, count=%ld | result=EBADF\n", fd, (uintptr_t) buffer_ptr, count);
        return SYSCALL_RET_ERROR(SYSCALL_ERROR_BADF);
    }

    fd_store_access_t access;
    if(fd_store_get_access(process->fd_store, fd, &access).is_error || !access.read) {
        LOG_UTRC("fd=%d, buffer=0x%lx, count=%ld, access.read=false | result=EBADF\n", fd, (uintptr_t) buffer_ptr, count);
        fd_store_file_put(file);
        return SYSCALL_RET_ERROR(SYSCALL_ERROR_BADF);
    }

    char* buffer = heap_alloc(count);

    io_request_t io_req;
    io_req.type = IO_REQUEST_READ;
    io_req.read.buffer = IO_BUFFER_CREATE_KERNEL(buffer, count);
    io_req.read.count = count;
    io_req.read.offset = file->offset;
    io_req.read.bytes_read = 0;
    io_req.no_block = file->flags.non_blocking;

    vfs_result_t result = fd_store_file_perform_io(file, &io_req);
    if(syscall_from_vfs_result(result).is_error) {
        fd_store_file_put(file);
        return syscall_from_vfs_result(result);
    }

    file->offset += io_req.read.bytes_read;
    fd_store_file_put(file);

    vm_copy_to(process->address_space, (uintptr_t) buffer_ptr, buffer, io_req.read.bytes_read);
    heap_free(buffer, count);
    LOG_UTRC("fd=%d, buffer=0x%lx, count=%ld, offset=%ld | result=%ld\n", fd, (uintptr_t) buffer_ptr, count, io_req.read.offset, io_req.read.bytes_read);

    return SYSCALL_RET_VALUE(io_req.read.bytes_read);
}

SYSCALL_DEFINE(syscall_fs_write, SYSCALL_FS_WRITE, uint64_t, uint32_t, fd, void*, buffer, size_t, count);
static syscall_ret_t syscall_fs_write(syscall_args_t* argument_data, uint32_t fd, void* buffer_ptr, size_t count) {
    (void) argument_data;

    process_t* process = CPU_LOCAL_GET_CURRENT_THREAD()->common.process;

    fd_store_file_t* file = fd_store_get_fd(process->fd_store, fd, nullptr);
    if(file == nullptr) {
        LOG_UTRC("fd=%d, buffer=0x%lx, count=%ld | result=EBADF\n", fd, (uintptr_t) buffer_ptr, count);
        return SYSCALL_RET_ERROR(SYSCALL_ERROR_BADF);
    }

    fd_store_access_t access;
    if(fd_store_get_access(process->fd_store, fd, &access).is_error || !access.write) {
        LOG_UTRC("fd=%d, buffer=0x%lx, count=%ld, access.write=false | result=EBADF\n", fd, (uintptr_t) buffer_ptr, count);
        fd_store_file_put(file);
        return SYSCALL_RET_ERROR(SYSCALL_ERROR_BADF);
    }

    char* buffer = heap_alloc(count);
    vm_copy_from(buffer, process->address_space, (uintptr_t) buffer_ptr, count);

    /// With O_APPEND every write goes to the current end of the file
    if(file->flags.append) {
        vfs_node_attr_t attr = { 0 };
        vfs_result_t attr_result = fd_store_file_get_attributes(file, &attr);
        if(syscall_from_vfs_result(attr_result).is_error) {
            heap_free(buffer, count);
            fd_store_file_put(file);
            return syscall_from_vfs_result(attr_result);
        }
        file->offset = attr.size;
    }

    io_request_t io_req;
    io_req.type = IO_REQUEST_WRITE;
    io_req.write.buffer = IO_BUFFER_CREATE_KERNEL(buffer, count);
    io_req.write.count = count;
    io_req.write.offset = file->offset;
    io_req.write.bytes_written = 0;
    io_req.no_block = file->flags.non_blocking;

    vfs_result_t result = fd_store_file_perform_io(file, &io_req);
    if(syscall_from_vfs_result(result).is_error) {
        heap_free(buffer, count);
        fd_store_file_put(file);
        return syscall_from_vfs_result(result);
    }

    file->offset += io_req.write.bytes_written;
    fd_store_file_put(file);

    heap_free(buffer, count);
    LOG_UTRC("fd=%d, buffer=0x%lx, count=%ld, offset=%ld | result=%ld\n", fd, (uintptr_t) buffer_ptr, count, io_req.write.offset, io_req.write.bytes_written);

    return SYSCALL_RET_VALUE(io_req.write.bytes_written);
}

/// Seek from beginning of file.
#define SEEK_SET 0
/// Seek from current position.
#define SEEK_CUR 1
/// Seek from end of file.
#define SEEK_END 2

SYSCALL_DEFINE(syscall_fs_seek, SYSCALL_FS_SEEK, void, uint32_t, fd, int64_t, offset, int, whence);
static syscall_ret_t syscall_fs_seek(syscall_args_t* argument_data, uint32_t fd, int64_t offset, int whence) {
    (void) argument_data;

    process_t* process = CPU_LOCAL_GET_CURRENT_THREAD()->common.process;

    fd_store_file_t* file = fd_store_get_fd(process->fd_store, fd, nullptr);
    if(file == nullptr) {
        LOG_UTRC("fd=%d | result=EBADF\n", fd);
        return SYSCALL_RET_ERROR(SYSCALL_ERROR_BADF);
    }

    /// Pipes and anything not backed by a cacheable inode (devices, the tty, directories, ...) are not seekable
    if(file->type == FD_STORE_FILE_TYPE_PIPE || fd_store_file_get_inode(file) == nullptr) {
        LOG_UTRC("fd=%d | result=ESPIPE\n", fd);
        fd_store_file_put(file);
        return SYSCALL_RET_ERROR(SYSCALL_ERROR_SPIPE);
    }

    vfs_node_attr_t attr;
    vfs_result_t res = fd_store_file_get_attributes(file, &attr);
    if(res != VFS_RESULT_OK) {
        fd_store_file_put(file);
        return SYSCALL_RET_ERROR(res == VFS_RESULT_ERR_UNSUPPORTED ? SYSCALL_ERROR_SPIPE : SYSCALL_ERROR_FAULT);
    }

    uint64_t new_offset;

    switch(whence) {
        case SEEK_SET: new_offset = offset; break;
        case SEEK_CUR: new_offset = file->offset + offset; break;
        case SEEK_END: new_offset = attr.size + offset; break;
        default:       {
            LOG_UTRC("fd=%d offset=%ld whence=%d | result=EINVAL\n", fd, offset, whence);
            fd_store_file_put(file);
            return SYSCALL_RET_ERROR(SYSCALL_ERROR_INVAL);
        }
    }

    file->offset = new_offset;
    LOG_UTRC("fd=%d offset=%ld whence=%d | result=%ld\n", fd, offset, whence, new_offset);
    fd_store_file_put(file);
    return SYSCALL_RET_VALUE(0);
}

SYSCALL_DEFINE(syscall_fs_getcwd, SYSCALL_FS_GETCWD, void, void*, buffer, size_t, size);
static syscall_ret_t syscall_fs_getcwd(syscall_args_t* argument_data, void* buffer_ptr, size_t size) {
    (void) argument_data;
    process_t* process = CPU_LOCAL_GET_CURRENT_THREAD()->common.process;
    userptr_t* upath = userptr_create((uintptr_t) buffer_ptr, size);
    if(!upath) {
        return SYSCALL_RET_ERROR(SYSCALL_ERROR_FAULT);
    }

    vfs_node_t* cwd = process->current_working_dir;

    if(cwd == nullptr) {
        if(!userptr_write_full(upath, "/")) {
            heap_free(upath, sizeof(userptr_t));
            return SYSCALL_RET_ERROR(SYSCALL_ERROR_FAULT);
        }
        return SYSCALL_RET_VALUE(0);
    }

    char* path;
    size_t path_size;
    vfs_result_t result = vfs_path_to(cwd, &path, &path_size);
    if(syscall_from_vfs_result(result).is_error) {
        heap_free(upath, sizeof(userptr_t));
        return syscall_from_vfs_result(result);
    }

    if(path_size > size) {
        heap_free(path, path_size);
        heap_free(upath, sizeof(userptr_t));
        return SYSCALL_RET_ERROR(SYSCALL_ERROR_TOO_BIG);
    }

    if(!userptr_write(upath, path, 0, path_size)) {
        heap_free(path, path_size);
        heap_free(upath, sizeof(userptr_t));
        return SYSCALL_RET_ERROR(SYSCALL_ERROR_FAULT);
    }

    heap_free(path, path_size);
    return SYSCALL_RET_VALUE(0);
}

SYSCALL_DEFINE(syscall_fs_chdir, SYSCALL_FS_CHDIR, void, void*, path);
static syscall_ret_t syscall_fs_chdir(syscall_args_t* argument_data, void* path_ptr) {
    (void) argument_data;
    process_t* process = CPU_LOCAL_GET_CURRENT_THREAD()->common.process;
    userptr_t* upath = userptr_create_string((uintptr_t) path_ptr, UAPI_LIMITS_PATH_MAX - 1);
    if(!upath) {
        return SYSCALL_RET_ERROR(SYSCALL_ERROR_TOO_BIG);
    }

    size_t path_size = upath->size + 1;
    char* path = heap_alloc(path_size);
    if(path == nullptr) {
        heap_free(upath, sizeof(userptr_t));
        return SYSCALL_RET_ERROR(SYSCALL_ERROR_NOMEM);
    }

    if(upath->size != 0 && !userptr_read_full(upath, path)) {
        heap_free(path, path_size);
        heap_free(upath, sizeof(userptr_t));
        return SYSCALL_RET_ERROR(SYSCALL_ERROR_FAULT);
    }

    path[upath->size] = '\0';
    heap_free(upath, sizeof(userptr_t));

    vfs_node_t* out_result_node;
    vfs_result_t result = vfs_lookup(&VFS_MAKE_REL_PATH(process->current_working_dir, path), true, &out_result_node);
    LOG_UTRC("pathname=%s | result=%d\n", path, result);
    heap_free(path, path_size);

    if(syscall_from_vfs_result(result).is_error) {
        return syscall_from_vfs_result(result);
    }

    process->current_working_dir = out_result_node;
    return SYSCALL_RET_VALUE(0);
}

static syscall_ret_t stat_from_attributes(userptr_t* stat_buf, const vfs_node_attr_t* attr) {
    uapi_stat_t stat = { 0 };
    stat.st_size = attr->size;
    stat.st_nlink = 1; // @todo: implement hardlink support
    switch(attr->type) {
        case VFS_NODE_TYPE_DIR:         stat.st_mode = UAPI_STAT_IFDIR; break;
        case VFS_NODE_TYPE_FILE:        stat.st_mode = UAPI_STAT_IFREG; break;
        case VFS_NODE_TYPE_LINK:        stat.st_mode = UAPI_STAT_IFLNK; break;
        case VFS_NODE_TYPE_FIFO:        stat.st_mode = UAPI_STAT_IFIFO; break;
        case VFS_NODE_TYPE_CHAR_DEVICE: stat.st_mode = UAPI_STAT_IFCHR; break;
        default:                        user_assert(false && "Invalid return value");
    }

    stat.st_mode |= (attr->permissions.user) << 6;
    stat.st_mode |= (attr->permissions.group) << 3;
    stat.st_mode |= (attr->permissions.other) << 0;
    stat.st_ino = attr->inode;
    stat.st_rdev = attr->rdev;

    if(!userptr_write_full(stat_buf, &stat)) {
        heap_free(stat_buf, sizeof(userptr_t));
        return SYSCALL_RET_ERROR(SYSCALL_ERROR_FAULT);
    }

    heap_free(stat_buf, sizeof(userptr_t));
    return SYSCALL_RET_VALUE(0);
}

static syscall_ret_t stat_internal(userptr_t* stat_buf, vfs_node_t* target) {
    vfs_node_attr_t attr = { 0 };
    vfs_result_t res = target->ops->get_attributes(target, &attr);
    vfs_node_put(target);

    if(res != VFS_RESULT_OK) {
        assert(false);
        heap_free(stat_buf, sizeof(userptr_t));
        return SYSCALL_RET_ERROR(SYSCALL_ERROR_FAULT);
    }

    return stat_from_attributes(stat_buf, &attr);
}

static void log_dir_fd(uint32_t fd) {
    if(fd == (uint32_t) UAPI_AT_FDCWD) {
        log_print_lockless(LOG_LEVEL_STRC, "AT_FDCWD");
    } else {
        log_print_lockless(LOG_LEVEL_STRC, "%u", fd);
    }
}

static void log_at_flags(uint32_t flags) {
    static const struct {
        uint32_t bit;
        const char* name;
    } at_flags[] = {
        { UAPI_AT_SYMLINK_NOFOLLOW, "SYMLINK_NOFOLLOW" },
        { UAPI_AT_SYMLINK_FOLLOW,   "SYMLINK_FOLLOW"   },
        { UAPI_AT_REMOVEDIR,        "REMOVEDIR"        },
        { UAPI_AT_EACCESS,          "EACCESS"          },
        { UAPI_AT_EMPTY_PATH,       "EMPTY_PATH"       },
    };

    bool first = true;
    uint32_t remaining = flags;

    for(size_t i = 0; i < sizeof(at_flags) / sizeof(at_flags[0]); i++) {
        if((flags & at_flags[i].bit) == 0) {
            continue;
        }
        log_print_lockless(LOG_LEVEL_STRC, "%s%s", first ? "" : "|", at_flags[i].name);
        remaining &= ~at_flags[i].bit;
        first = false;
    }

    if(remaining != 0) {
        log_print_lockless(LOG_LEVEL_STRC, "%sunknown=0x%x", first ? "" : "|", remaining);
        first = false;
    }

    if(first) {
        log_print_lockless(LOG_LEVEL_STRC, "-");
    }
}

SYSCALL_DEFINE(syscall_fs_stat, SYSCALL_FS_STAT, void, uint32_t, fd, void*, stat);
static syscall_ret_t syscall_fs_stat(syscall_args_t* argument_data, uint32_t fd, void* stat_ptr) {
    (void) argument_data;
    process_t* process = CPU_LOCAL_GET_CURRENT_THREAD()->common.process;
    userptr_t* stat_buf = userptr_create((uintptr_t) stat_ptr, sizeof(uapi_stat_t));
    if(!stat_buf) {
        return SYSCALL_RET_ERROR(SYSCALL_ERROR_INVAL);
    }

    fd_store_file_t* file = fd_store_get_fd(process->fd_store, fd, nullptr);
    if(file == nullptr) {
        LOG_UTRC("fd=%d | result=EBADF\n", fd);
        heap_free(stat_buf, sizeof(userptr_t));
        return SYSCALL_RET_ERROR(SYSCALL_ERROR_BADF);
    }

    vfs_node_attr_t attr = { 0 };
    vfs_result_t res = fd_store_file_get_attributes(file, &attr);
    fd_store_file_put(file);

    if(syscall_from_vfs_result(res).is_error) {
        heap_free(stat_buf, sizeof(userptr_t));
        return syscall_from_vfs_result(res);
    }

    return stat_from_attributes(stat_buf, &attr);
}

SYSCALL_DEFINE_TRACED(syscall_fs_stat_at, SYSCALL_FS_STAT_AT, void, syscall_fs_stat_at_trace, uint32_t, fd, void*, path, void*, stat, uint32_t, flags);
static void syscall_fs_stat_at_trace(syscall_args_t* argument_data, uint32_t fd, void* path, void* stat, uint32_t flags) {
    (void) argument_data;

    log_print_lockless(LOG_LEVEL_STRC, "fd=");
    log_dir_fd(fd);
    log_print_lockless(LOG_LEVEL_STRC, ", path=0x%lx, stat=0x%lx, flags=[", (unsigned long) (uintptr_t) path, (unsigned long) (uintptr_t) stat);
    log_at_flags(flags);
    log_print_lockless(LOG_LEVEL_STRC, "]\n");
}

static syscall_ret_t syscall_fs_stat_at(syscall_args_t* argument_data, uint32_t fd, void* path_ptr, void* stat_ptr, uint32_t flags) {
    (void) argument_data;
    process_t* process = CPU_LOCAL_GET_CURRENT_THREAD()->common.process;
    userptr_t* stat_buf = userptr_create((uintptr_t) stat_ptr, sizeof(uapi_stat_t));
    if(!stat_buf) {
        return SYSCALL_RET_ERROR(SYSCALL_ERROR_INVAL);
    }

    userptr_t* upath = userptr_create_string((uintptr_t) path_ptr, UAPI_LIMITS_PATH_MAX - 1);
    if(!upath) {
        return SYSCALL_RET_ERROR(SYSCALL_ERROR_TOO_BIG);
    }

    size_t path_size = upath->size + 1;
    char* pathname = heap_alloc(path_size);
    if(pathname == nullptr) {
        heap_free(upath, sizeof(userptr_t));
        return SYSCALL_RET_ERROR(SYSCALL_ERROR_NOMEM);
    }

    if(upath->size != 0 && !userptr_read_full(upath, pathname)) {
        heap_free(pathname, path_size);
        heap_free(upath, sizeof(userptr_t));
        return SYSCALL_RET_ERROR(SYSCALL_ERROR_FAULT);
    }

    pathname[upath->size] = '\0';
    heap_free(upath, sizeof(userptr_t));

    vfs_path_t vfs_path;
    vfs_path.rel_path = pathname;

    if(pathname[0] == '/') {
        vfs_path.node = nullptr;
    } else if(fd == (uint32_t) UAPI_AT_FDCWD) {
        vfs_node_get(process->current_working_dir);
        vfs_path.node = process->current_working_dir;
    } else {
        fd_store_file_t* file = fd_store_get_fd(process->fd_store, fd, nullptr);
        if(file == nullptr) {
            LOG_UTRC("fd=%d | result=EBADF\n", fd);
            return SYSCALL_RET_ERROR(SYSCALL_ERROR_BADF);
        }
        vfs_node_t* dir_node = fd_store_file_get_node(file);
        if(dir_node == nullptr) {
            fd_store_file_put(file);
            return SYSCALL_RET_ERROR(SYSCALL_ERROR_BADF);
        }
        vfs_node_get(dir_node);
        vfs_path.node = dir_node;
        fd_store_file_put(file);
    }

    vfs_node_t* result_node;
    vfs_result_t result = vfs_lookup(&vfs_path, (flags & UAPI_AT_SYMLINK_NOFOLLOW) == 0, &result_node);
    if(syscall_from_vfs_result(result).is_error) {
        return syscall_from_vfs_result(result);
    }

    if(vfs_path.node) {
        vfs_node_put(vfs_path.node);
    }

    return stat_internal(stat_buf, result_node);
}

SYSCALL_DEFINE(syscall_fs_dup, SYSCALL_FS_DUP, int32_t, uint32_t, fd);
static syscall_ret_t syscall_fs_dup(syscall_args_t* argument_data, uint32_t fd) {
    (void) argument_data;
    // @note: Unused, always passed as 0 from mlibc
    // uint32_t flags = args->arg2;
    //
    process_t* process = CPU_LOCAL_GET_CURRENT_THREAD()->common.process;
    return fd_store_duplicate(process->fd_store, fd);
}

SYSCALL_DEFINE(syscall_fs_dup2, SYSCALL_FS_DUP2, void, uint32_t, fd, uint32_t, flags, uint32_t, newfd);
static syscall_ret_t syscall_fs_dup2(syscall_args_t* argument_data, uint32_t fd, uint32_t flags, uint32_t newfd) {
    (void) argument_data;

    process_t* process = CPU_LOCAL_GET_CURRENT_THREAD()->common.process;
    return fd_store_duplicate_to(process->fd_store, fd, newfd, (flags & UAPI_O_CLOEXEC) != 0);
}

SYSCALL_DEFINE(syscall_fs_fcntl, SYSCALL_FS_FCNTL, int32_t, int32_t, fd, int32_t, command, int64_t, argument);
static syscall_ret_t syscall_fs_fcntl(syscall_args_t* argument_data, int32_t fd, int32_t command, int64_t argument) {
    (void) argument_data;

    process_t* process = CPU_LOCAL_GET_CURRENT_THREAD()->common.process;

    switch(command) {
        case UAPI_F_DUPFD:
        case UAPI_F_DUPFD_CLOEXEC: {
            if(argument < 0 || argument > INT32_MAX) {
                return SYSCALL_RET_ERROR(SYSCALL_ERROR_INVAL);
            }

            return fd_store_duplicate_from(process->fd_store, (uint32_t) fd, (int32_t) argument, command == UAPI_F_DUPFD_CLOEXEC);
        }
        case UAPI_F_GETFD: {
            bool close_on_execute;
            syscall_ret_t result = fd_store_get_close_on_execute(process->fd_store, (uint32_t) fd, &close_on_execute);
            if(result.is_error) {
                return result;
            }

            return SYSCALL_RET_VALUE(close_on_execute ? UAPI_FD_CLOEXEC : 0);
        }
        case UAPI_F_SETFD: return fd_store_set_close_on_execute(process->fd_store, (uint32_t) fd, (argument & UAPI_FD_CLOEXEC) != 0);
        case UAPI_F_GETFL: {
            fd_store_access_t access;
            fd_store_flags_t flags;
            syscall_ret_t result = fd_store_get_status_flags(process->fd_store, (uint32_t) fd, &access, &flags);
            if(result.is_error) {
                return result;
            }

            int32_t status = 0;
            if(access.read && access.write) {
                status |= UAPI_O_RDWR;
            } else if(access.write) {
                status |= UAPI_O_WRONLY;
            } else {
                status |= UAPI_O_RDONLY;
            }
            if(flags.append) {
                status |= UAPI_O_APPEND;
            }
            if(flags.non_blocking) {
                status |= UAPI_O_NONBLOCK;
            }

            return SYSCALL_RET_VALUE(status);
        }
        case UAPI_F_SETFL: {
            fd_store_flags_t flags = {
                .non_blocking = (argument & UAPI_O_NONBLOCK) != 0,
                .append = (argument & UAPI_O_APPEND) != 0,
            };

            return fd_store_set_status_flags(process->fd_store, (uint32_t) fd, flags);
        }
        default: return SYSCALL_RET_ERROR(SYSCALL_ERROR_INVAL);
    }
}

/// @brief Convert posix access() mode bits into VFS permission bits
static vfs_permissions_t permissions_from_access_mode(int mode) {
    vfs_permissions_t required = 0;
    if((mode & UAPI_R_OK) != 0) {
        required |= VFS_PERM_READ;
    }
    if((mode & UAPI_W_OK) != 0) {
        required |= VFS_PERM_WRITE;
    }
    if((mode & UAPI_X_OK) != 0) {
        required |= VFS_PERM_EXECUTE;
    }
    return required;
}

static void log_access_modes(int mode) {
    static const struct {
        int bit;
        const char* name;
    } access_modes[] = {
        { UAPI_R_OK, "R_OK" },
        { UAPI_W_OK, "W_OK" },
        { UAPI_X_OK, "X_OK" },
    };

    if(mode == UAPI_F_OK) {
        log_print_lockless(LOG_LEVEL_STRC, "F_OK");
        return;
    }

    bool first = true;
    for(size_t i = 0; i < sizeof(access_modes) / sizeof(access_modes[0]); i++) {
        if((mode & access_modes[i].bit) == 0) {
            continue;
        }
        log_print_lockless(LOG_LEVEL_STRC, "%s%s", first ? "" : "|", access_modes[i].name);
        first = false;
    }
}

static syscall_ret_t access_check_node(vfs_node_t* node, int mode) {
    /// F_OK only checks whether the path resolves to a file
    if(mode == UAPI_F_OK) {
        vfs_node_put(node);
        return SYSCALL_RET_VALUE(0);
    }

    vfs_node_attr_t attr = { 0 };
    vfs_result_t result = vfs_get_attributes_node(node, &attr);
    vfs_node_put(node);

    if(result != VFS_RESULT_OK) {
        return SYSCALL_RET_ERROR(SYSCALL_ERROR_FAULT);
    }

    // @todo: actually implement permissions in a meaningful way
    vfs_permissions_t required = permissions_from_access_mode(mode);
    if((attr.permissions.user & required) != required) {
        return SYSCALL_RET_ERROR(SYSCALL_ERROR_ACCESS);
    }

    return SYSCALL_RET_VALUE(0);
}

SYSCALL_DEFINE_TRACED(syscall_fs_access, SYSCALL_FS_ACCESS, void, syscall_fs_access_trace, void*, path, int, mode);
static void syscall_fs_access_trace(syscall_args_t* argument_data, void* path, int mode) {
    (void) argument_data;

    log_print_lockless(LOG_LEVEL_STRC, "path=0x%lx, mode=[", (unsigned long) (uintptr_t) path);
    log_access_modes(mode);
    log_print_lockless(LOG_LEVEL_STRC, "]\n");
}

static syscall_ret_t syscall_fs_access(syscall_args_t* argument_data, void* path_ptr, int mode) {
    (void) argument_data;

    process_t* process = CPU_LOCAL_GET_CURRENT_THREAD()->common.process;

    if((mode & ~(UAPI_R_OK | UAPI_W_OK | UAPI_X_OK)) != 0) {
        return SYSCALL_RET_ERROR(SYSCALL_ERROR_INVAL);
    }

    userptr_t* upath = userptr_create_string((uintptr_t) path_ptr, UAPI_LIMITS_PATH_MAX - 1);
    if(!upath) {
        return SYSCALL_RET_ERROR(SYSCALL_ERROR_TOO_BIG);
    }

    size_t path_size = upath->size + 1;
    char* path = heap_alloc(path_size);
    if(path == nullptr) {
        heap_free(upath, sizeof(userptr_t));
        return SYSCALL_RET_ERROR(SYSCALL_ERROR_NOMEM);
    }

    if(upath->size != 0 && !userptr_read_full(upath, path)) {
        heap_free(path, path_size);
        heap_free(upath, sizeof(userptr_t));
        return SYSCALL_RET_ERROR(SYSCALL_ERROR_FAULT);
    }

    path[upath->size] = '\0';
    heap_free(upath, sizeof(userptr_t));

    vfs_node_t* node = nullptr;
    /// access() follows symlinks, unlike lstat()
    vfs_result_t result = vfs_lookup(&VFS_MAKE_REL_PATH(process->current_working_dir, path), true, &node);
    if(syscall_from_vfs_result(result).is_error) {
        LOG_UTRC("path=%s, mode=%d | result=%d\n", path, mode, result);
        heap_free(path, path_size);
        return syscall_from_vfs_result(result);
    }

    syscall_ret_t ret = access_check_node(node, mode);
    LOG_UTRC("path=%s, mode=%d | result=%d\n", path, mode, (int) ret.err);
    heap_free(path, path_size);
    return ret;
}

SYSCALL_DEFINE_TRACED(syscall_fs_faccessat, SYSCALL_FS_FACCESSAT, void, syscall_fs_faccessat_trace, uint32_t, fd, void*, path, int, mode, uint32_t, flags);
static void syscall_fs_faccessat_trace(syscall_args_t* argument_data, uint32_t fd, void* path, int mode, uint32_t flags) {
    (void) argument_data;

    log_print_lockless(LOG_LEVEL_STRC, "fd=");
    log_dir_fd(fd);
    log_print_lockless(LOG_LEVEL_STRC, ", path=0x%lx, mode=[", (unsigned long) (uintptr_t) path);
    log_access_modes(mode);
    log_print_lockless(LOG_LEVEL_STRC, "], flags=[");
    log_at_flags(flags);
    log_print_lockless(LOG_LEVEL_STRC, "]\n");
}

static syscall_ret_t syscall_fs_faccessat(syscall_args_t* argument_data, uint32_t fd, void* path_ptr, int mode, uint32_t flags) {
    (void) argument_data;

    process_t* process = CPU_LOCAL_GET_CURRENT_THREAD()->common.process;

    if((mode & ~(UAPI_R_OK | UAPI_W_OK | UAPI_X_OK)) != 0) {
        return SYSCALL_RET_ERROR(SYSCALL_ERROR_INVAL);
    }

    userptr_t* upath = userptr_create_string((uintptr_t) path_ptr, UAPI_LIMITS_PATH_MAX - 1);
    if(!upath) {
        return SYSCALL_RET_ERROR(SYSCALL_ERROR_TOO_BIG);
    }

    size_t path_size = upath->size + 1;
    char* pathname = heap_alloc(path_size);
    if(pathname == nullptr) {
        heap_free(upath, sizeof(userptr_t));
        return SYSCALL_RET_ERROR(SYSCALL_ERROR_NOMEM);
    }

    if(upath->size != 0 && !userptr_read_full(upath, pathname)) {
        heap_free(pathname, path_size);
        heap_free(upath, sizeof(userptr_t));
        return SYSCALL_RET_ERROR(SYSCALL_ERROR_FAULT);
    }

    pathname[upath->size] = '\0';
    heap_free(upath, sizeof(userptr_t));

    vfs_path_t vfs_path;
    vfs_path.rel_path = pathname;

    if(pathname[0] == '/') {
        vfs_path.node = nullptr;
    } else if(fd == (uint32_t) UAPI_AT_FDCWD) {
        vfs_node_get(process->current_working_dir);
        vfs_path.node = process->current_working_dir;
    } else {
        fd_store_file_t* file = fd_store_get_fd(process->fd_store, fd, nullptr);
        if(file == nullptr) {
            LOG_UTRC("fd=%u | result=EBADF\n", fd);
            heap_free(pathname, path_size);
            return SYSCALL_RET_ERROR(SYSCALL_ERROR_BADF);
        }
        vfs_node_t* dir_node = fd_store_file_get_node(file);
        if(dir_node == nullptr) {
            fd_store_file_put(file);
            heap_free(pathname, path_size);
            return SYSCALL_RET_ERROR(SYSCALL_ERROR_BADF);
        }
        vfs_node_get(dir_node);
        vfs_path.node = dir_node;
        fd_store_file_put(file);
    }

    vfs_node_t* node = nullptr;
    /// faccessat() follows symlinks unless AT_SYMLINK_NOFOLLOW is passed
    vfs_result_t result = vfs_lookup(&vfs_path, (flags & UAPI_AT_SYMLINK_NOFOLLOW) == 0, &node);
    if(vfs_path.node != nullptr) {
        vfs_node_put(vfs_path.node);
    }
    heap_free(pathname, path_size);

    if(syscall_from_vfs_result(result).is_error) {
        return syscall_from_vfs_result(result);
    }

    return access_check_node(node, mode);
}

SYSCALL_DEFINE(syscall_fs_getdents, SYSCALL_FS_GETDENTS, uint64_t, uint32_t, fd, void*, buffer, size_t, count);
static syscall_ret_t syscall_fs_getdents(syscall_args_t* argument_data, uint32_t fd, void* buffer_ptr, size_t count) {
    (void) argument_data;

    process_t* process = CPU_LOCAL_GET_CURRENT_THREAD()->common.process;

    fd_store_file_t* file = fd_store_get_fd(process->fd_store, fd, nullptr);
    if(file == nullptr) {
        LOG_UTRC("fd=%d | result=EBADF\n", fd);
        return SYSCALL_RET_ERROR(SYSCALL_ERROR_BADF);
    }

    vfs_node_t* dir_node = fd_store_file_get_node(file);
    if(dir_node == nullptr || dir_node->type != VFS_NODE_TYPE_DIR) {
        LOG_UTRC("fd=%d | result=NOTDIR\n", fd);
        fd_store_file_put(file);
        return SYSCALL_RET_ERROR(SYSCALL_ERROR_NOTDIR);
    }

    if(count == 0) {
        fd_store_file_put(file);
        return SYSCALL_RET_VALUE(0);
    }

    char* buffer = heap_alloc(count);
    if(buffer == nullptr) {
        fd_store_file_put(file);
        return SYSCALL_RET_ERROR(SYSCALL_ERROR_NOMEM);
    }
    memory_set(buffer, 0, count);

    size_t offset = file->offset;
    size_t used = 0;

    while(used < count) {
        size_t prev_offset = offset;
        vfs_dentry_t* child = nullptr;
        vfs_result_t result = fd_store_file_read_dir(file, &offset, &child);
        if(result != VFS_RESULT_OK) {
            heap_free(buffer, count);
            fd_store_file_put(file);
            return SYSCALL_RET_ERROR(SYSCALL_ERROR_INVAL);
        }

        if(child == nullptr) {
            break;
        }

        size_t name_len = (size_t) string_length(child->name);
        if(name_len > UAPI_DIRENT_NAME_MAX) {
            name_len = UAPI_DIRENT_NAME_MAX;
        }

        size_t reclen;
        {
            size_t raw = offsetof(uapi_dirent_t, d_name) + name_len + 1;
            reclen = MATH_ALIGN_UP(raw, alignof(uapi_dirent_t));
        }
        if(used + reclen > count) {
            offset = prev_offset;
            vfs_dentry_put(child);

            if(used == 0) {
                heap_free(buffer, count);
                fd_store_file_put(file);
                return SYSCALL_RET_ERROR(SYSCALL_ERROR_INVAL);
            }
            break;
        }

        uapi_dirent_t* dent = (uapi_dirent_t*) (buffer + used);
        if(child->node != nullptr) {
            vfs_node_attr_t attr = { 0 };

            lock_acquire(&child->node->lock);
            vfs_result_t res = child->node->ops->get_attributes(child->node, &attr);
            lock_release(&child->node->lock);

            dent->d_ino = res == VFS_RESULT_OK ? attr.inode : 0;

            switch(child->node->type) {
                case VFS_NODE_TYPE_DIR:         dent->d_type = UAPI_DIRENT_DIR; break;
                case VFS_NODE_TYPE_FILE:        dent->d_type = UAPI_DIRENT_REG; break;
                case VFS_NODE_TYPE_LINK:        dent->d_type = UAPI_DIRENT_LNK; break;
                case VFS_NODE_TYPE_FIFO:        dent->d_type = UAPI_DIRENT_FIFO; break;
                case VFS_NODE_TYPE_CHAR_DEVICE: dent->d_type = UAPI_DIRENT_CHR; break;
                default:                        dent->d_type = UAPI_DIRENT_UNKNOWN; break;
            }
        } else {
            dent->d_ino = 0;
            dent->d_type = UAPI_DIRENT_UNKNOWN;
        }
        dent->d_off = (int64_t) offset;
        dent->d_reclen = reclen;
        memory_copy(dent->d_name, child->name, name_len);
        dent->d_name[name_len] = '\0';

        vfs_dentry_put(child);
        used += reclen;
    }

    file->offset = offset;
    fd_store_file_put(file);

    if(used == 0) {
        heap_free(buffer, count);
        return SYSCALL_RET_VALUE(0);
    }

    vm_copy_to(process->address_space, (uintptr_t) buffer_ptr, buffer, used);
    heap_free(buffer, count);

    LOG_UTRC("fd=%d, buffer=0x%lx, max_size=%ld, offset=%ld | result=%ld\n", fd, (uintptr_t) buffer_ptr, count, offset, used);
    return SYSCALL_RET_VALUE(used);
}

SYSCALL_DEFINE(syscall_fs_readlinkat, SYSCALL_FS_READLINKAT, uint64_t, uint32_t, fd, void*, path, void*, buffer, size_t, count);
static syscall_ret_t syscall_fs_readlinkat(syscall_args_t* argument_data, uint32_t fd, void* path_ptr, void* buffer_ptr, size_t count) {
    (void) argument_data;

    process_t* process = CPU_LOCAL_GET_CURRENT_THREAD()->common.process;

    userptr_t* upath = userptr_create_string((uintptr_t) path_ptr, UAPI_LIMITS_PATH_MAX - 1);
    if(!upath) {
        return SYSCALL_RET_ERROR(SYSCALL_ERROR_TOO_BIG);
    }

    size_t path_size = upath->size + 1;
    char* pathname = heap_alloc(path_size);
    if(pathname == nullptr) {
        heap_free(upath, sizeof(userptr_t));
        return SYSCALL_RET_ERROR(SYSCALL_ERROR_NOMEM);
    }

    if(upath->size != 0 && !userptr_read_full(upath, pathname)) {
        heap_free(pathname, path_size);
        heap_free(upath, sizeof(userptr_t));
        return SYSCALL_RET_ERROR(SYSCALL_ERROR_FAULT);
    }

    pathname[upath->size] = '\0';
    heap_free(upath, sizeof(userptr_t));

    vfs_path_t vfs_path;
    vfs_path.rel_path = pathname;

    if(pathname[0] == '/') {
        vfs_path.node = nullptr;
    } else if(fd == (uint32_t) UAPI_AT_FDCWD) {
        vfs_node_get(process->current_working_dir);
        vfs_path.node = process->current_working_dir;
    } else {
        fd_store_file_t* entry = fd_store_get_fd(process->fd_store, fd, nullptr);
        if(entry == nullptr) {
            LOG_UTRC("fd=%d | result=EBADF\n", fd);
            heap_free(pathname, path_size);
            return SYSCALL_RET_ERROR(SYSCALL_ERROR_BADF);
        }
        vfs_node_t* dir_node = fd_store_file_get_node(entry);
        if(dir_node == nullptr) {
            fd_store_file_put(entry);
            heap_free(pathname, path_size);
            return SYSCALL_RET_ERROR(SYSCALL_ERROR_BADF);
        }
        vfs_node_get(dir_node);
        vfs_path.node = dir_node;
        fd_store_file_put(entry);
    }

    vfs_node_t* node;
    vfs_result_t result = vfs_lookup(&vfs_path, false, &node);

    if(vfs_path.node) {
        vfs_node_put(vfs_path.node);
    }
    heap_free(pathname, path_size);

    if(syscall_from_vfs_result(result).is_error) {
        return syscall_from_vfs_result(result);
    }

    if(node->type != VFS_NODE_TYPE_LINK) {
        vfs_node_put(node);
        return SYSCALL_RET_ERROR(SYSCALL_ERROR_INVAL);
    }

    vfs_node_attr_t attr = { 0 };
    result = vfs_get_attributes_node(node, &attr);
    if(result != VFS_RESULT_OK) {
        vfs_node_put(node);
        return SYSCALL_RET_ERROR(SYSCALL_ERROR_FAULT);
    }

    size_t link_size = attr.size;
    if(link_size > count) {
        link_size = count;
    }

    if(link_size == 0) {
        vfs_node_put(node);
        return SYSCALL_RET_VALUE(0);
    }

    char* buffer = heap_alloc(link_size);
    if(buffer == nullptr) {
        vfs_node_put(node);
        return SYSCALL_RET_ERROR(SYSCALL_ERROR_NOMEM);
    }

    io_request_t io_req;
    io_req.type = IO_REQUEST_READ;
    io_req.read.buffer = IO_BUFFER_CREATE_KERNEL(buffer, link_size);
    io_req.read.count = link_size;
    io_req.read.offset = 0;
    io_req.read.bytes_read = 0;
    io_req.no_block = false;

    result = vfs_perform_io_node(node, &io_req);
    vfs_node_put(node);

    if(result != VFS_RESULT_OK) {
        heap_free(buffer, link_size);
        return SYSCALL_RET_ERROR(SYSCALL_ERROR_FAULT);
    }

    vm_copy_to(process->address_space, (uintptr_t) buffer_ptr, buffer, io_req.read.bytes_read);
    heap_free(buffer, link_size);

    LOG_UTRC("fd=%d, buffer=0x%lx, max_size=%ld | result=%ld\n", fd, (uintptr_t) buffer_ptr, count, io_req.read.bytes_read);
    return SYSCALL_RET_VALUE(io_req.read.bytes_read);
}

SYSCALL_DEFINE(syscall_fs_ftruncate, SYSCALL_FS_FTRUNCATE, void, uint32_t, fd, int64_t, size);
static syscall_ret_t syscall_fs_ftruncate(syscall_args_t* argument_data, uint32_t fd, int64_t size) {
    (void) argument_data;

    process_t* process = CPU_LOCAL_GET_CURRENT_THREAD()->common.process;

    fd_store_file_t* file = fd_store_get_fd(process->fd_store, fd, nullptr);
    if(file == nullptr) {
        LOG_UTRC("fd=%d, size=%ld | result=EBADF\n", fd, size);
        return SYSCALL_RET_ERROR(SYSCALL_ERROR_BADF);
    }

    fd_store_access_t access;
    if(fd_store_get_access(process->fd_store, fd, &access).is_error || !access.write) {
        LOG_UTRC("fd=%d, size=%ld, access.write=false | result=EBADF\n", fd, size);
        fd_store_file_put(file);
        return SYSCALL_RET_ERROR(SYSCALL_ERROR_BADF);
    }

    if(size < 0) {
        LOG_UTRC("fd=%d, size=%ld | result=EINVAL\n", fd, size);
        fd_store_file_put(file);
        return SYSCALL_RET_ERROR(SYSCALL_ERROR_INVAL);
    }

    vfs_result_t result = fd_store_file_truncate(file, (size_t) size);
    LOG_UTRC("fd=%d, size=%ld | result=%d\n", fd, size, result);
    fd_store_file_put(file);
    /// posix wants EINVAL when the fd does not refer to a regular file
    if(result == VFS_RESULT_ERR_UNSUPPORTED)
        return SYSCALL_RET_ERROR(SYSCALL_ERROR_INVAL);

    return syscall_from_vfs_result(result);
}

SYSCALL_DEFINE(syscall_fs_pipe, SYSCALL_FS_PIPE, void, void*, fds, int, flags);
static syscall_ret_t syscall_fs_pipe(syscall_args_t* argument_data, void* fds_ptr, int flags) {
    (void) argument_data;

    process_t* process = CPU_LOCAL_GET_CURRENT_THREAD()->common.process;

    if((flags & ~(UAPI_O_NONBLOCK | UAPI_O_CLOEXEC)) != 0) {
        LOG_UTRC("fds=0x%lx, flags=%x | result=EINVAL\n", (uintptr_t) fds_ptr, flags);
        return SYSCALL_RET_ERROR(SYSCALL_ERROR_INVAL);
    }

    userptr_t* ufds = userptr_create((uintptr_t) fds_ptr, sizeof(int) * 2);
    if(ufds == nullptr) {
        return SYSCALL_RET_ERROR(SYSCALL_ERROR_FAULT);
    }

    pipe_t* pipe = pipe_create();
    if(pipe == nullptr) {
        heap_free(ufds, sizeof(userptr_t));
        return SYSCALL_RET_ERROR(SYSCALL_ERROR_NOMEM);
    }

    fd_store_flags_t fd_flags = (fd_store_flags_t) { .non_blocking = (flags & UAPI_O_NONBLOCK) != 0 };
    bool close_on_execute = (flags & UAPI_O_CLOEXEC) != 0;

    fd_store_file_t* read_file = fd_store_file_create(FD_STORE_FILE_TYPE_PIPE, &g_pipe_file_ops, pipe, (fd_store_access_t) { .read = true }, fd_flags);
    fd_store_file_t* write_file = fd_store_file_create(FD_STORE_FILE_TYPE_PIPE, &g_pipe_file_ops, pipe, (fd_store_access_t) { .write = true }, fd_flags);

    uint32_t read_fd = fd_store_insert_file(process->fd_store, read_file, close_on_execute);
    uint32_t write_fd = fd_store_insert_file(process->fd_store, write_file, close_on_execute);

    int fds[2] = { (int) read_fd, (int) write_fd };
    if(!userptr_write_full(ufds, fds)) {
        fd_store_free_fd(process->fd_store, read_fd);
        fd_store_free_fd(process->fd_store, write_fd);
        heap_free(ufds, sizeof(userptr_t));
        return SYSCALL_RET_ERROR(SYSCALL_ERROR_FAULT);
    }

    heap_free(ufds, sizeof(userptr_t));
    LOG_UTRC("fds=0x%lx, flags=%x | result=%d,%d\n", (uintptr_t) fds_ptr, flags, fds[0], fds[1]);
    return SYSCALL_RET_VOID();
}

SYSCALL_DEFINE(syscall_sys_ioctl, SYSCALL_FS_IOCTL, void, int, fd, unsigned long, request, void*, buffer);
static syscall_ret_t syscall_sys_ioctl(syscall_args_t* argument_data, int fd, unsigned long request, void* buffer) {
    (void) argument_data;

    process_t* process = CPU_LOCAL_GET_CURRENT_THREAD()->common.process;

    fd_store_file_t* file = fd_store_get_fd(process->fd_store, fd, nullptr);
    if(file == nullptr) {
        return SYSCALL_RET_ERROR(SYSCALL_ERROR_BADF);
    }

    io_request_t io_req;
    io_req.type = IO_REQUEST_IOCTL;
    // we don't actually know the size of the ioctl until the device processes it.... posix <3
    io_req.ioctl.request = request;
    io_req.ioctl.buffer = IO_BUFFER_CREATE_USER(process->address_space, buffer, 0);
    io_req.no_block = file->flags.non_blocking;

    vfs_result_t result = fd_store_file_perform_io(file, &io_req);
    fd_store_file_put(file);

    if(syscall_from_vfs_result(result).is_error) {
        return syscall_from_vfs_result(result);
    }

    return SYSCALL_RET_VOID();
}
