#include <common/arch.h>
#include <common/fs/io.h>
#include <common/fs/vfs.h>
#include <common/init.h>
#include <common/log.h>
#include <lib/string.h>
#include <lib/types.h>
#include <memory/vm.h>
#include <stdint.h>

#include "memory/heap.h"

void init_stage_vfs(uint32_t core_id) {
    if(!INIT_CORE_IS_BSP(core_id)) { return; }

    bootinfo_module_t* initramfs_module = nullptr;
    for(size_t i = 0; i < g_init_boot_info->module_count; i++) {
        bootinfo_module_t* module = &g_init_boot_info->modules[i];
        // @todo: Support more then just rdk
        if(string_compare(module->name, "/boot/initramfs.rdk") == 0) {
            initramfs_module = module;
            break;
        }
    }

    if(initramfs_module == nullptr) { arch_panic("Failed to find initramfs\n"); }

    vfs_result_t res = vfs_mount(&g_vfs_rdsk_ops, nullptr, (void*) (initramfs_module->phys_addr + g_init_boot_info->hhdm_offset));
    if(res != VFS_RESULT_OK) { arch_panic("Failed to mount initramfs (%d)\n", res); }
    LOG_OKAY("mounted initramfs\n");

    size_t offset = 0;
    vfs_dentry_t* dirent;
    while(true) {
        res = vfs_read_dir(&VFS_MAKE_ABS_PATH("/"), &offset, &dirent);
        if(res != VFS_RESULT_OK) { arch_panic("Failed to read root dir (%d)\n", res); }
        if(dirent == nullptr) break;
        LOG_INFO("root dirent: %s\n", dirent->name);
        vfs_dentry_put(dirent);
    }

    log_print(LOG_LEVEL_INFO, "\n");
    offset = 0;
    while(true) {
        res = vfs_read_dir(&VFS_MAKE_ABS_PATH("/test"), &offset, &dirent);
        if(res != VFS_RESULT_OK) { arch_panic("Failed to read test dir (%d)\n", res); }
        if(dirent == nullptr) break;
        LOG_INFO("/test dirent: %s\n", dirent->name);
        vfs_dentry_put(dirent);
    }
    log_print(LOG_LEVEL_INFO, "\n");
    offset = 0;
    while(true) {
        res = vfs_read_dir(&VFS_MAKE_ABS_PATH("/test/meow"), &offset, &dirent);
        if(res != VFS_RESULT_OK) { arch_panic("Failed to read test dir (%d)\n", res); }
        if(dirent == nullptr) break;
        LOG_INFO("/test/meow dirent: %s\n", dirent->name);
        vfs_dentry_put(dirent);
    }


    vfs_node_attr_t node;
    vfs_get_attributes(&VFS_MAKE_ABS_PATH("/test/meow/nesting.txt"), &node);
    LOG_INFO("nesting.txt size: %ld\n", node.size);
    LOG_INFO("nesting.txt type: %s\n", node.type == VFS_NODE_TYPE_FILE ? "file" : "dir");
    LOG_INFO(
        "nesting.txt permissions: user=%c%c%c, group=%c%c%c, other=%c%c%c\n",
        (node.permissions.user & VFS_PERM_READ) ? 'r' : '-',
        (node.permissions.user & VFS_PERM_WRITE) ? 'w' : '-',
        (node.permissions.user & VFS_PERM_EXECUTE) ? 'x' : '-',
        (node.permissions.group & VFS_PERM_READ) ? 'r' : '-',
        (node.permissions.group & VFS_PERM_WRITE) ? 'w' : '-',
        (node.permissions.group & VFS_PERM_EXECUTE) ? 'x' : '-',
        (node.permissions.other & VFS_PERM_READ) ? 'r' : '-',
        (node.permissions.other & VFS_PERM_WRITE) ? 'w' : '-',
        (node.permissions.other & VFS_PERM_EXECUTE) ? 'x' : '-'
    );


    io_request_t io_req;
    io_req.type = IO_REQUEST_READ;
    io_req.read.buffer = heap_alloc(256);
    io_req.read.count = 256;
    io_req.read.offset = 0;
    io_req.read.bytes_read = 0;

    res = vfs_perform_io(&VFS_MAKE_ABS_PATH("/test/meow/nesting.txt"), &io_req);
    LOG_OKAY("read /test/meow/nesting.txt: %.*s\n", (int) io_req.read.bytes_read, (char*) io_req.read.buffer);
}
