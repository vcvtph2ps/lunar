#include <common/cpu_local.h>
#include <common/fs/vfs.h>
#include <common/log.h>
#include <common/userspace/fd_store.h>
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

SYSCALL_DEFINE(syscall_fs_open, SYSCALL_FS_OPEN, int32_t, void*, path, int, flags, uint32_t, mode);
static syscall_ret_t syscall_fs_open(syscall_args_t* argument_data, void* path_ptr, int flags, uint32_t mode) {
    (void) argument_data;
    (void) mode;

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
    vfs_result_t result = vfs_lookup(&VFS_MAKE_REL_PATH(process->current_working_dir, path), (flags & UAPI_O_NOFOLLOW) == 0, &out_result_node);

    LOG_UTRC("pathname=%s, flags=%x, mode=%d | result=%d\n", path, flags, mode, result);
    heap_free(path, path_size);
    if(flags & UAPI_O_CREATE) {
        LOG_UTRC("O_CREATE unimplemented\n");
    }
    // @todo: UAPI_O_DIRECTORY

    if(syscall_from_vfs_result(result).is_error) {
        return syscall_from_vfs_result(result);
    }

    fd_store_entry_t* entry;
    uint32_t fd = fd_store_create_fd(process->fd_store, out_result_node, &entry);
    // @note: since vfs_lookup and fd_store_create_fd bump ref count, we drop our ref from vfs_lookup
    vfs_node_put(out_result_node);

    int access_flags = flags & UAPI_O_RDWR;
    entry->access.read = (access_flags & UAPI_O_RDONLY) != 0;
    entry->access.write = (access_flags & UAPI_O_WRONLY) != 0;
    entry->flags.non_blocking = (flags & UAPI_O_NONBLOCK) != 0;
    entry->flags.close_on_execute = (flags & UAPI_O_CLOEXEC) != 0;
    return SYSCALL_RET_VALUE(fd);
}

SYSCALL_DEFINE(syscall_fs_close, SYSCALL_FS_CLOSE, void, uint32_t, fd);
static syscall_ret_t syscall_fs_close(syscall_args_t* argument_data, uint32_t fd) {
    (void) argument_data;

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

SYSCALL_DEFINE(syscall_fs_read, SYSCALL_FS_READ, uint64_t, uint32_t, fd, void*, buffer, size_t, count);
static syscall_ret_t syscall_fs_read(syscall_args_t* argument_data, uint32_t fd, void* buffer_ptr, size_t count) {
    (void) argument_data;

    process_t* process = CPU_LOCAL_GET_CURRENT_THREAD()->common.process;

    fd_store_entry_t* entry = fd_store_get_fd(process->fd_store, fd);
    if(entry == nullptr) {
        LOG_UTRC("fd=%d, buffer=0x%lx, count=%ld | result=BADFD\n", fd, (uintptr_t) buffer_ptr, count);
        return SYSCALL_RET_ERROR(SYSCALL_ERROR_BADFD);
    }

    if(!entry->access.read) {
        LOG_UTRC("fd=%d, buffer=0x%lx, count=%ld, access.read=false | result=BADFD\n", fd, (uintptr_t) buffer_ptr, count);
        return SYSCALL_RET_ERROR(SYSCALL_ERROR_BADFD);
    }

    char* buffer = heap_alloc(count);

    io_request_t io_req;
    io_req.type = IO_REQUEST_READ;
    io_req.read.buffer = buffer;
    io_req.read.count = count;
    io_req.read.offset = entry->offset;
    io_req.read.bytes_read = 0;
    io_req.no_block = entry->flags.non_blocking;

    vfs_result_t result = vfs_perform_io_node(entry->node, &io_req);
    if(syscall_from_vfs_result(result).is_error) {
        return syscall_from_vfs_result(result);
    }

    entry->offset += io_req.read.bytes_read;

    vm_copy_to(process->address_space, (uintptr_t) buffer_ptr, buffer, io_req.read.bytes_read);
    heap_free(buffer, count);
    LOG_UTRC("fd=%d, buffer=0x%lx, count=%ld, offset=%ld | result=%ld\n", fd, (uintptr_t) buffer_ptr, count, io_req.read.offset, io_req.read.bytes_read);

    return SYSCALL_RET_VALUE(io_req.read.bytes_read);
}

SYSCALL_DEFINE(syscall_fs_write, SYSCALL_FS_WRITE, uint64_t, uint32_t, fd, void*, buffer, size_t, count);
static syscall_ret_t syscall_fs_write(syscall_args_t* argument_data, uint32_t fd, void* buffer_ptr, size_t count) {
    (void) argument_data;

    process_t* process = CPU_LOCAL_GET_CURRENT_THREAD()->common.process;

    fd_store_entry_t* entry = fd_store_get_fd(process->fd_store, fd);
    if(entry == nullptr) {
        LOG_UTRC("fd=%d, buffer=0x%lx, count=%ld | result=BADFD\n", fd, (uintptr_t) buffer_ptr, count);
        return SYSCALL_RET_ERROR(SYSCALL_ERROR_BADFD);
    }

    if(!entry->access.write) {
        LOG_UTRC("fd=%d, buffer=0x%lx, count=%ld, access.write=false | result=BADFD\n", fd, (uintptr_t) buffer_ptr, count);
        return SYSCALL_RET_ERROR(SYSCALL_ERROR_BADFD);
    }

    char* buffer = heap_alloc(count);
    vm_copy_from(buffer, process->address_space, (uintptr_t) buffer_ptr, count);

    io_request_t io_req;
    io_req.type = IO_REQUEST_WRITE;
    io_req.write.buffer = buffer;
    io_req.write.count = count;
    io_req.write.offset = entry->offset;
    io_req.write.bytes_written = 0;
    io_req.no_block = entry->flags.non_blocking;

    vfs_result_t result = vfs_perform_io_node(entry->node, &io_req);
    if(syscall_from_vfs_result(result).is_error) {
        return syscall_from_vfs_result(result);
    }

    entry->offset += io_req.write.bytes_written;

    heap_free(buffer, count);
    LOG_UTRC("fd=%d, buffer=0x%lx, count=%ld, offset=%ld | result=%ld\n", fd, (uintptr_t) buffer_ptr, count, io_req.write.offset, io_req.write.bytes_written);

    return SYSCALL_RET_VALUE(io_req.write.bytes_written);
}

SYSCALL_DEFINE(syscall_fs_is_a_tty, SYSCALL_FS_ISATTY, void, uint32_t, fd);
static syscall_ret_t syscall_fs_is_a_tty(syscall_args_t* argument_data, uint32_t fd) {
    (void) argument_data;
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

SYSCALL_DEFINE(syscall_fs_seek, SYSCALL_FS_SEEK, void, uint32_t, fd, int64_t, offset, int, whence);
static syscall_ret_t syscall_fs_seek(syscall_args_t* argument_data, uint32_t fd, int64_t offset, int whence) {
    (void) argument_data;

    process_t* process = CPU_LOCAL_GET_CURRENT_THREAD()->common.process;

    if(fd == 0 || fd == 1 || fd == 2) {
        return SYSCALL_RET_VALUE(0);
    }

    fd_store_entry_t* entry = fd_store_get_fd(process->fd_store, fd);
    if(entry == nullptr) {
        LOG_UTRC("fd=%d | result=BADFD\n", fd);
        return SYSCALL_RET_ERROR(SYSCALL_ERROR_BADFD);
    }

    vfs_node_attr_t attr;
    vfs_result_t res = entry->node->ops->get_attributes(entry->node, &attr);
    if(res != VFS_RESULT_OK) {
        assert(false);
        return SYSCALL_RET_ERROR(SYSCALL_ERROR_FAULT);
    }

    uint64_t new_offset;

    switch(whence) {
        case SEEK_SET: new_offset = offset; break;
        case SEEK_CUR: new_offset = entry->offset + offset; break;
        case SEEK_END: new_offset = attr.size + offset; break;
        default:       {
            LOG_UTRC("fd=%d offset=%ld whence=%d | result=EINVAL\n", fd, offset, whence);
            return SYSCALL_RET_ERROR(SYSCALL_ERROR_INVAL);
        }
    }

    entry->offset = new_offset;
    LOG_UTRC("fd=%d offset=%ld whence=%d | result=%ld\n", fd, offset, whence, new_offset);
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

static syscall_ret_t stat_internal(userptr_t* stat_buf, vfs_node_t* target) {
    vfs_node_attr_t attr = { 0 };
    vfs_result_t res = target->ops->get_attributes(target, &attr);
    if(res != VFS_RESULT_OK) {
        assert(false);
        return SYSCALL_RET_ERROR(SYSCALL_ERROR_FAULT);
    }

    uapi_stat_t stat = { 0 };
    stat.st_size = attr.size;
    stat.st_nlink = 1; // @todo: implement hardlink support
    switch(attr.type) {
        case VFS_NODE_TYPE_DIR:  stat.st_mode = UAPI_STAT_IFDIR; break;
        case VFS_NODE_TYPE_FILE: stat.st_mode = UAPI_STAT_IFREG; break;
        case VFS_NODE_TYPE_LINK: stat.st_mode = UAPI_STAT_IFLNK; break;
        default:                 user_assert(false && "Invalid return value");
    }

    stat.st_mode |= (attr.permissions.user) << 6;
    stat.st_mode |= (attr.permissions.group) << 3;
    stat.st_mode |= (attr.permissions.other) << 0;
    stat.st_ino = attr.inode;

    if(!userptr_write_full(stat_buf, &stat)) {
        vfs_node_put(target);
        heap_free(stat_buf, sizeof(userptr_t*));
        return SYSCALL_RET_ERROR(SYSCALL_ERROR_FAULT);
    }

    vfs_node_put(target);
    heap_free(stat_buf, sizeof(userptr_t*));
    return SYSCALL_RET_VALUE(0);
}

SYSCALL_DEFINE(syscall_fs_stat, SYSCALL_FS_STAT, void, uint32_t, fd, void*, stat);
static syscall_ret_t syscall_fs_stat(syscall_args_t* argument_data, uint32_t fd, void* stat_ptr) {
    (void) argument_data;
    process_t* process = CPU_LOCAL_GET_CURRENT_THREAD()->common.process;
    userptr_t* stat_buf = userptr_create((uintptr_t) stat_ptr, sizeof(uapi_stat_t));
    if(!stat_buf) {
        return SYSCALL_RET_ERROR(SYSCALL_ERROR_INVAL);
    }

    fd_store_entry_t* entry = fd_store_get_fd(process->fd_store, fd);
    if(entry == nullptr) {
        LOG_UTRC("fd=%d | result=BADFD\n", fd);
        return SYSCALL_RET_ERROR(SYSCALL_ERROR_BADFD);
    }

    vfs_node_get(entry->node);
    return stat_internal(stat_buf, entry->node);
}

SYSCALL_DEFINE(syscall_fs_stat_at, SYSCALL_FS_STAT_AT, void, uint32_t, fd, void*, path, void*, stat, uint32_t, flags);
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
        fd_store_entry_t* entry = fd_store_get_fd(process->fd_store, fd);
        if(entry == nullptr) {
            LOG_UTRC("fd=%d | result=BADFD\n", fd);
            return SYSCALL_RET_ERROR(SYSCALL_ERROR_BADFD);
        }
        vfs_node_get(entry->node);
        vfs_path.node = entry->node;
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
    // @note: unused, always passed as 0 from mlibc
    // uint32_t flags = args->arg2;
    //
    process_t* process = CPU_LOCAL_GET_CURRENT_THREAD()->common.process;
    uint32_t new_fd;
    vfs_result_t result = fd_store_duplicate(process->fd_store, fd, &new_fd);
    if(syscall_from_vfs_result(result).is_error) {
        return syscall_from_vfs_result(result);
    }

    return SYSCALL_RET_VALUE(new_fd);
}

SYSCALL_DEFINE(syscall_fs_dup2, SYSCALL_FS_DUP2, void, uint32_t, fd, uint32_t, flags, uint32_t, newfd);
static syscall_ret_t syscall_fs_dup2(syscall_args_t* argument_data, uint32_t fd, uint32_t flags, uint32_t newfd) {
    (void) argument_data;

    process_t* process = CPU_LOCAL_GET_CURRENT_THREAD()->common.process;
    vfs_result_t result = fd_store_duplicate_to(process->fd_store, fd, newfd, (flags & UAPI_O_CLOEXEC) != 0);
    if(syscall_from_vfs_result(result).is_error) {
        return syscall_from_vfs_result(result);
    }

    return SYSCALL_RET_VALUE(0);
}

SYSCALL_DEFINE(syscall_fs_getdents, SYSCALL_FS_GETDENTS, uint64_t, uint32_t, fd, void*, buffer, size_t, count);
static syscall_ret_t syscall_fs_getdents(syscall_args_t* argument_data, uint32_t fd, void* buffer_ptr, size_t count) {
    (void) argument_data;

    process_t* process = CPU_LOCAL_GET_CURRENT_THREAD()->common.process;

    fd_store_entry_t* entry = fd_store_get_fd(process->fd_store, fd);
    if(entry == nullptr) {
        LOG_UTRC("fd=%d | result=BADFD\n", fd);
        return SYSCALL_RET_ERROR(SYSCALL_ERROR_BADFD);
    }

    if(entry->node->type != VFS_NODE_TYPE_DIR) {
        LOG_UTRC("fd=%d | result=NOTDIR\n", fd);
        return SYSCALL_RET_ERROR(SYSCALL_ERROR_NOTDIR);
    }

    if(count == 0) {
        return SYSCALL_RET_VALUE(0);
    }

    char* buffer = heap_alloc(count);
    if(buffer == nullptr) {
        return SYSCALL_RET_ERROR(SYSCALL_ERROR_NOMEM);
    }
    memory_set(buffer, 0, count);

    vfs_path_t dir_path = VFS_MAKE_REL_PATH(entry->node, "");
    size_t offset = entry->offset;
    size_t used = 0;

    while(used < count) {
        size_t prev_offset = offset;
        vfs_dentry_t* child = nullptr;
        vfs_result_t result = vfs_read_dir(&dir_path, &offset, &child);
        if(result != VFS_RESULT_OK) {
            heap_free(buffer, count);
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
                case VFS_NODE_TYPE_DIR:  dent->d_type = UAPI_DIRENT_DIR; break;
                case VFS_NODE_TYPE_FILE: dent->d_type = UAPI_DIRENT_REG; break;
                case VFS_NODE_TYPE_LINK: dent->d_type = UAPI_DIRENT_LNK; break;
                default:                 dent->d_type = UAPI_DIRENT_UNKNOWN; break;
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

    entry->offset = offset;

    if(used == 0) {
        heap_free(buffer, count);
        return SYSCALL_RET_VALUE(0);
    }

    vm_copy_to(process->address_space, (uintptr_t) buffer_ptr, buffer, used);
    heap_free(buffer, count);

    LOG_UTRC("fd=%d, buffer=0x%lx, max_size=%ld, offset=%ld | result=%ld\n", fd, (uintptr_t) buffer_ptr, count, entry->offset, used);
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
        fd_store_entry_t* entry = fd_store_get_fd(process->fd_store, fd);
        if(entry == nullptr) {
            LOG_UTRC("fd=%d | result=BADFD\n", fd);
            heap_free(pathname, path_size);
            return SYSCALL_RET_ERROR(SYSCALL_ERROR_BADFD);
        }
        vfs_node_get(entry->node);
        vfs_path.node = entry->node;
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
    io_req.read.buffer = buffer;
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
