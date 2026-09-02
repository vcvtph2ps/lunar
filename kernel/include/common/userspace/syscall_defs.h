#pragma once

#include <common/userspace/syscall.h>

// NOLINTBEGIN

syscall_ret_t syscall_sys_proc_exit(syscall_args_t* args);
syscall_ret_t syscall_sys_thread_set_tcb(syscall_args_t* args);
syscall_ret_t syscall_sys_debug_log(syscall_args_t* args);

syscall_ret_t syscall_sys_vm_map(syscall_args_t* args);
syscall_ret_t syscall_sys_vm_unmap(syscall_args_t* args);
syscall_ret_t syscall_sys_vm_protect(syscall_args_t* args);

syscall_ret_t syscall_sys_fs_open(syscall_args_t* args);
syscall_ret_t syscall_sys_fs_read(syscall_args_t* args);
syscall_ret_t syscall_sys_fs_write(syscall_args_t* args);
syscall_ret_t syscall_sys_fs_close(syscall_args_t* args);
syscall_ret_t syscall_sys_fs_is_a_tty(syscall_args_t* args);
syscall_ret_t syscall_sys_fs_seek(syscall_args_t* args);

// NOLINTEND
