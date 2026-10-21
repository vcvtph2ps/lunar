#include <common/assert.h>
#include <common/cpu_local.h>
#include <common/fs/vfs.h>
#include <common/interrupts/interrupt.h>
#include <common/log.h>
#include <common/sched/process.h>
#include <common/sched/process_group.h>
#include <common/sched/sched.h>
#include <common/sched/thread.h>
#include <common/sync/spinlock.h>
#include <common/userspace/syscall.h>
#include <common/userspace/syscall_defs.h>
#include <common/userspace/userptr.h>
#include <common/userspace/userspace.h>
#include <lib/helpers.h>
#include <lib/string.h>
#include <memory/heap.h>
#include <memory/vm.h>
#include <uapi/limits.h>
#include <uapi/types.h>

[[noreturn]] syscall_ret_t syscall_sys_proc_exit(syscall_args_t* args) {
    LOG_UTRC("exit_code=%d\n", (int) args->arg1);

    thread_t* thread = &CPU_LOCAL_GET_CURRENT_THREAD()->common;
    process_t* process = thread->process;

    ATOMIC_STORE(&thread->exit_code, (int32_t) args->arg1, ATOMIC_RELAXED);

    process_kill(process, PROCESS_EXITED, (int32_t) args->arg1);
    ASSERT_UNREACHABLE();
}

/// @brief Resolves a pid to a referenced process, accepting UINT32_MAX or a direct child of `parent`
static process_t* find_self_or_child(process_t* parent, uint32_t pid) {
    if(pid == UINT32_MAX || pid == parent->process_id) {
        return process_ref(parent);
    }

    process_t* found = nullptr;
    spinlock_nodw_lock(&parent->children_lock);
    LIST_FOR_EACH(&parent->children_list, node) {
        process_t* candidate = CONTAINER_OF(node, process_t, list_node_sibling);
        if(candidate->process_id == pid) {
            found = process_ref(candidate);
            break;
        }
    }
    spinlock_nodw_unlock(&parent->children_lock);
    return found;
}

#define SYSCALL_PROC_INFO_GROUP_ID 0
#define SYSCALL_PROC_INFO_EGROUP_ID 1
#define SYSCALL_PROC_INFO_USER_ID 2
#define SYSCALL_PROC_INFO_EUSER_ID 3
#define SYSCALL_PROC_INFO_PROCESS_ID 4
#define SYSCALL_PROC_INFO_PARENT_PROCESS 5
#define SYSCALL_PROC_INFO_THREAD_ID 6
#define SYSCALL_PROC_INFO_PROCESS_GROUP_ID 7

static syscall_ret_t internal_setpgid(process_t* target, int32_t pgid) {
    if(sched_arch_thread_current()->process == target) {
        LOG_UTRC("pid=%d, pgid=%d\n", target->process_id, pgid);
    } else {
        LOG_UTRC("pid=%d (from %d), pgid=%d\n", target->process_id, sched_arch_thread_current()->process->process_id, pgid);
    }

    if(pgid < 0) {
        return SYSCALL_RET_ERROR(SYSCALL_ERROR_INVAL);
    }

    uint32_t new_pgid = pgid == 0 ? target->process_id : (uint32_t) pgid;
    bool joined = process_group_join(target, new_pgid);
    process_deref(target);

    if(!joined) {
        return SYSCALL_RET_ERROR(SYSCALL_ERROR_NOMEM);
    }

    return SYSCALL_RET_VALUE(0);
}

syscall_ret_t syscall_sys_proc_setinfo(syscall_args_t* args) {
    process_t* process;
    if(args->arg1 == UINT64_MAX) {
        process = sched_arch_thread_current()->process;
        process_ref(process);
    } else {
        process = find_self_or_child(sched_arch_thread_current()->process, (uint32_t) args->arg1);
        if(process == nullptr) {
            return SYSCALL_RET_ERROR(SYSCALL_ERROR_SEARCH);
        }
    }

    switch(args->arg2) {
        case SYSCALL_PROC_INFO_GROUP_ID:         user_assert(false && "SYSCALL_PROC_INFO_GROUP_ID unimplemented!\n"); break;
        case SYSCALL_PROC_INFO_EGROUP_ID:        user_assert(false && "SYSCALL_PROC_INFO_EGROUP_ID unimplemented!\n"); break;
        case SYSCALL_PROC_INFO_USER_ID:          user_assert(false && "SYSCALL_PROC_INFO_USER_ID unimplemented!\n"); break;
        case SYSCALL_PROC_INFO_EUSER_ID:         user_assert(false && "SYSCALL_PROC_INFO_EUSER_ID unimplemented!\n"); break;
        case SYSCALL_PROC_INFO_PROCESS_ID:       user_assert(false && "SYSCALL_PROC_INFO_PROCESS_ID unimplemented!\n"); break;
        case SYSCALL_PROC_INFO_PARENT_PROCESS:   user_assert(false && "SYSCALL_PROC_INFO_PARENT_PROCESS unimplemented!\n"); break;
        case SYSCALL_PROC_INFO_THREAD_ID:        user_assert(false && "SYSCALL_PROC_INFO_THREAD_ID unimplemented!\n"); break;
        case SYSCALL_PROC_INFO_PROCESS_GROUP_ID: return internal_setpgid(process, args->arg3);
        default:                                 user_assert(false && "unreachable"); break;
    }

    ASSERT_UNREACHABLE();
}

syscall_ret_t syscall_sys_proc_getinfo(syscall_args_t* args) {
    process_t* process;
    if(args->arg1 == UINT64_MAX) {
        process = sched_arch_thread_current()->process;
        process_ref(process);
    } else {
        process = find_self_or_child(sched_arch_thread_current()->process, (uint32_t) args->arg1);
        if(process == nullptr) {
            return SYSCALL_RET_ERROR(SYSCALL_ERROR_SEARCH);
        }
    }

    process_t* current_process = CPU_LOCAL_GET_CURRENT_THREAD()->common.process;
    switch(args->arg2) {
        case SYSCALL_PROC_INFO_GROUP_ID:         LOG_UTRC("SYSCALL_PROC_INFO_GROUP_ID unimplemented!\n"); return SYSCALL_RET_VALUE(0);
        case SYSCALL_PROC_INFO_EGROUP_ID:        LOG_UTRC("SYSCALL_PROC_INFO_EGROUP_ID unimplemented!\n"); return SYSCALL_RET_VALUE(0);
        case SYSCALL_PROC_INFO_USER_ID:          LOG_UTRC("SYSCALL_PROC_INFO_USER_ID unimplemented!\n"); return SYSCALL_RET_VALUE(0);
        case SYSCALL_PROC_INFO_EUSER_ID:         LOG_UTRC("SYSCALL_PROC_INFO_EUSER_ID unimplemented!\n"); return SYSCALL_RET_VALUE(0);
        case SYSCALL_PROC_INFO_PROCESS_ID:       return SYSCALL_RET_VALUE(current_process->process_id);
        case SYSCALL_PROC_INFO_PARENT_PROCESS:   return SYSCALL_RET_VALUE(current_process->parent_process_id);
        case SYSCALL_PROC_INFO_THREAD_ID:        return SYSCALL_RET_VALUE(CPU_LOCAL_GET_CURRENT_THREAD()->common.tid);
        case SYSCALL_PROC_INFO_PROCESS_GROUP_ID: return SYSCALL_RET_VALUE(process_group_id(current_process));
        default:                                 user_assert(false && "unreachable"); break;
    }

    ASSERT_UNREACHABLE();
}

syscall_ret_t syscall_sys_proc_fork(syscall_args_t* args) {
    LOG_UTRC("process forked\n");
    arch_interrupt_regs_t* frame = args->regs;

    uintptr_t entry = arch_interrupt_regs_syscall_get_user_ip(frame);

    process_t* new_process = process_fork(sched_arch_thread_current()->process);
    if(new_process == nullptr) {
        return SYSCALL_RET_ERROR(SYSCALL_ERROR_NOMEM);
    }

    thread_t* thread = sched_arch_fork_thread_user(new_process, sched_arch_thread_current(), args->user_rsp, entry, frame);

    /// The thread references its process for as long as it lives
    process_ref(new_process);
    list_push(&new_process->thread_list, &thread->list_node_process);
    sched_thread_schedule(thread);

    return SYSCALL_RET_VALUE(new_process->process_id);
}

/// Return immediately if no child has exited yet
#define SYSCALL_WAITPID_NOHANG 1

syscall_ret_t syscall_sys_proc_waitpid(syscall_args_t* args) {
    int32_t pid = (int32_t) args->arg1;
    uintptr_t status_ptr = args->arg2; // int*
    int flags = (int) args->arg3;
    // uintptr_t ru = args->arg4; // struct rusage*, unimplemented

    LOG_UTRC("pid=%d, flags=%d\n", pid, flags);

    process_t* parent = CPU_LOCAL_GET_CURRENT_THREAD()->common.process;

    if(pid == 0 || pid < -1) {
        user_assert(false && "unimplemented");
    }

    while(true) {
        process_t* child = nullptr;

        spinlock_nodw_lock(&parent->children_lock);
        LIST_FOR_EACH(&parent->children_list, node) {
            process_t* candidate = CONTAINER_OF(node, process_t, list_node_sibling);

            if(pid > 0 && candidate->process_id != (uint32_t) pid) {
                continue;
            }

            spinlock_nodw_lock(&candidate->status.lock);
            if(candidate->status.state != PROCESS_RUNNING) {
                spinlock_nodw_unlock(&candidate->status.lock);
                list_node_delete(&parent->children_list, &candidate->list_node_sibling);
                child = candidate;
                break;
            }
            spinlock_nodw_unlock(&candidate->status.lock);
        }

        /// Determine whether any child matching the request is still alive so we know whether to wait or to fail with ECHILD
        bool child_exists = child != nullptr;
        if(!child_exists) {
            LIST_FOR_EACH(&parent->children_list, node) {
                process_t* candidate = CONTAINER_OF(node, process_t, list_node_sibling);
                if(pid == -1 || candidate->process_id == (uint32_t) pid) {
                    child_exists = true;
                    break;
                }
            }
        }
        spinlock_nodw_unlock(&parent->children_lock);

        if(child != nullptr) {
            uint32_t status = 0;
            uint32_t child_pid = child->process_id;

            spinlock_nodw_lock(&child->status.lock);

            switch(child->status.state) {
                case PROCESS_EXITED:     status = ((uint32_t) child->status.exited.exit_code & 0xffu) << 8; break;
                case PROCESS_TERMINATED: status = (uint32_t) child->status.terminated.signal_number & 0x7fu; break;
                default:                 ASSERT_UNREACHABLE();
            }

            child->status.state = PROCESS_PENDING_REAP;
            spinlock_nodw_unlock(&child->status.lock);

            /// The parent's children list held a reference, it was taken off the list above
            process_deref(child);

            if(status_ptr != 0) {
                if(vm_copy_to(parent->address_space, status_ptr, &status, sizeof(status)) != sizeof(status)) {
                    return SYSCALL_RET_ERROR(SYSCALL_ERROR_FAULT);
                }
            }
            return SYSCALL_RET_VALUE(child_pid);
        }

        /// No child (left) matches the request
        if(!child_exists) {
            return SYSCALL_RET_ERROR(SYSCALL_ERROR_CHILD);
        }

        if(flags & SYSCALL_WAITPID_NOHANG) {
            return SYSCALL_RET_VALUE(0);
        }

        sched_wait_single_interruptible(&parent->child_exit_event, UINT64_MAX);

        /// a signal interrupted the wait, let userspace handle it and retry if needed
        if(sched_thread_interrupted()) {
            return SYSCALL_RET_ERROR(SYSCALL_ERROR_INTR);
        }
    }
}

static void free_string_array(char** strings, int count) {
    if(strings == nullptr) {
        return;
    }

    for(int i = 0; i < count; i++) {
        if(strings[i] != nullptr) {
            heap_free(strings[i], string_length(strings[i]) + 1);
        }
    }

    heap_free(strings, sizeof(char*) * count);
}

static syscall_ret_t read_user_string_array(userptr_t* u_array, size_t* byte_counter, char*** out_strings, int* out_count) {
    int count = (int) (u_array->size / sizeof(uintptr_t));

    if(count == 0) {
        *out_strings = nullptr;
        *out_count = 0;
        return SYSCALL_RET_VALUE(0);
    }

    uintptr_t* user_strings = heap_alloc(u_array->size);
    if(user_strings == nullptr) {
        return SYSCALL_RET_ERROR(SYSCALL_ERROR_NOMEM);
    }

    if(!userptr_read(u_array, user_strings)) {
        heap_free(user_strings, u_array->size);
        return SYSCALL_RET_ERROR(SYSCALL_ERROR_FAULT);
    }

    char** strings = heap_zalloc(sizeof(char*) * count);
    if(strings == nullptr) {
        heap_free(user_strings, u_array->size);
        return SYSCALL_RET_ERROR(SYSCALL_ERROR_NOMEM);
    }

    for(int i = 0; i < count; i++) {
        userptr_t* u_string = userptr_create_string(user_strings[i], UAPI_LIMITS_ARG_MAX - 1);
        if(u_string == nullptr) {
            free_string_array(strings, count);
            heap_free(user_strings, u_array->size);
            return SYSCALL_RET_ERROR(SYSCALL_ERROR_FAULT);
        }

        size_t string_size = u_string->size + 1;
        if(*byte_counter + string_size > UAPI_LIMITS_ARG_MAX) {
            heap_free(u_string, sizeof(userptr_t));
            free_string_array(strings, count);
            heap_free(user_strings, u_array->size);
            return SYSCALL_RET_ERROR(SYSCALL_ERROR_TOO_BIG);
        }
        *byte_counter += string_size;

        char* string = heap_alloc(string_size);
        if(string == nullptr) {
            heap_free(u_string, sizeof(userptr_t));
            free_string_array(strings, count);
            heap_free(user_strings, u_array->size);
            return SYSCALL_RET_ERROR(SYSCALL_ERROR_NOMEM);
        }

        if(u_string->size != 0 && !userptr_read(u_string, string)) {
            heap_free(string, string_size);
            heap_free(u_string, sizeof(userptr_t));
            free_string_array(strings, count);
            heap_free(user_strings, u_array->size);
            return SYSCALL_RET_ERROR(SYSCALL_ERROR_FAULT);
        }

        string[u_string->size] = '\0';
        strings[i] = string;

        heap_free(u_string, sizeof(userptr_t));
    }

    heap_free(user_strings, u_array->size);

    *out_strings = strings;
    *out_count = count;
    return SYSCALL_RET_VALUE(0);
}

static syscall_ret_t execve_internal(vfs_node_t* node, uintptr_t argv_ptr, uintptr_t envp_ptr) {
    userptr_t* u_argv = userptr_create_array(argv_ptr, sizeof(char*));
    if(u_argv == nullptr) {
        return SYSCALL_RET_ERROR(SYSCALL_ERROR_TOO_BIG);
    }

    userptr_t* u_envp = userptr_create_array(envp_ptr, sizeof(char*));
    if(u_envp == nullptr) {
        heap_free(u_argv, sizeof(userptr_t));
        return SYSCALL_RET_ERROR(SYSCALL_ERROR_TOO_BIG);
    }

    size_t argument_bytes = 0;

    char** argv;
    char** envp;
    int argc;
    int envc;

    syscall_ret_t ret = read_user_string_array(u_argv, &argument_bytes, &argv, &argc);
    heap_free(u_argv, sizeof(userptr_t));
    if(ret.is_error) {
        heap_free(u_envp, sizeof(userptr_t));
        return ret;
    }

    ret = read_user_string_array(u_envp, &argument_bytes, &envp, &envc);
    heap_free(u_envp, sizeof(userptr_t));
    if(ret.is_error) {
        free_string_array(argv, argc);
        return ret;
    }

    LOG_UTRC("argc=%d, envc=%d, argument_bytes=%zu\n", argc, envc, argument_bytes);

    for(int i = 0; i < argc; i++) {
        LOG_UTRC("argv[%d] = \"%s\"\n", i, argv[i]);
    }
    for(int i = 0; i < envc; i++) {
        LOG_UTRC("envp[%d] = \"%s\"\n", i, envp[i]);
    }

    ldr_process_load_info_t load_info;
    load_info.argv = (const char**) argv;
    load_info.envp = (const char**) envp;
    load_info.argc = argc;
    load_info.envc = envc;

    LOG_UTRC("process exec'd\n");

    thread_t* thread;
    if(process_exec_from_file(sched_arch_thread_current()->process, node, &load_info, &thread)) {
        sched_thread_schedule(thread);
        sched_terminate_current_thread();
    }

    free_string_array(argv, argc);
    free_string_array(envp, envc);

    return SYSCALL_RET_ERROR(SYSCALL_ERROR_NO_EXEC);
}

syscall_ret_t syscall_sys_proc_execve(syscall_args_t* args) {
    userptr_t* upath = userptr_create_string(args->arg1, UAPI_LIMITS_PATH_MAX - 1);
    if(!upath) {
        return SYSCALL_RET_ERROR(SYSCALL_ERROR_TOO_BIG);
    }

    size_t path_size = upath->size + 1;
    char* path = heap_alloc(path_size);
    if(path == nullptr) {
        heap_free(upath, sizeof(userptr_t));
        return SYSCALL_RET_ERROR(SYSCALL_ERROR_NOMEM);
    }

    if(upath->size != 0 && !userptr_read(upath, path)) {
        heap_free(path, path_size);
        heap_free(upath, sizeof(userptr_t));
        return SYSCALL_RET_ERROR(SYSCALL_ERROR_FAULT);
    }

    path[upath->size] = '\0';
    heap_free(upath, sizeof(userptr_t));
    LOG_UTRC("exec'ing %s\n", path);

    vfs_node_t* node;
    // @todo: this probably shouldn't be VFS_MAKE_ABS_PATH
    vfs_result_t result = vfs_lookup(&VFS_MAKE_ABS_PATH(path), true, &node);
    heap_free(path, path_size);

    if(result != VFS_RESULT_OK) {
        LOG_UTRC("failed with vfs result %d\n", result);
    }

    switch(result) {
        case VFS_RESULT_OK:              break;
        case VFS_RESULT_ERR_NOT_FOUND:   return SYSCALL_RET_ERROR(SYSCALL_ERROR_NOENT);
        case VFS_RESULT_ERR_WOULD_BLOCK: return SYSCALL_RET_ERROR(SYSCALL_ERROR_AGAIN);
        default:                         user_assert(false && "Invalid return value");
    }

    syscall_ret_t ret = execve_internal(node, args->arg2, args->arg3);
    vfs_node_put(node);
    return ret;
}

syscall_ret_t syscall_sys_proc_fexecve(syscall_args_t* args) {
    (void) args;
    ASSERT_TODO();
    // int fd = args->arg1;

    // return execve_internal(node, args->arg2, args->arg3);
}
