#include <arch/x86_64/hardware/16550uart.h>
#include <common/arch.h>
#include <common/assert.h>
#include <common/fs/devfs.h>
#include <common/fs/io.h>
#include <common/fs/vfs.h>
#include <common/init.h>
#include <common/kgz.h>
#include <common/log.h>
#include <lib/string.h>
#include <lib/types.h>
#include <memory/heap.h>
#include <memory/vm.h>
#include <nanoprintf/nanoprintf.h>
#include <stdint.h>

typedef enum {
    INITRAMFS_TYPE_GZIP,
    INITRAMFS_TYPE_RDSK,
    INITRAMFS_TYPE_UNKNOWN
} initramfs_type_t;

static initramfs_type_t determine_initramfs_type(void* initramfs_data) {
    uint8_t* signature = (uint8_t*) initramfs_data;
    if(signature[0] == 'R' && signature[1] == 'D' && signature[2] == 'S' && signature[3] == 'K') {
        return INITRAMFS_TYPE_RDSK;
    }
    if(signature[0] == 0x1F && signature[1] == 0x8B) {
        return INITRAMFS_TYPE_GZIP;
    }
    return INITRAMFS_TYPE_UNKNOWN;
}

static void* try_decompress(void* initramfs_data, size_t size) {
    uint8_t* signature = (uint8_t*) initramfs_data;

    if(signature[0] != 0x1F || signature[1] != 0x8B) {
        arch_panic("Unknown initramfs compression type\n");
    }

    LOG_INFO("Decompressing initramfs...\n");
    void* decompressed_buffer = kgz_gzip_decompress((void*) initramfs_data, size, nullptr, nullptr);
    if(decompressed_buffer == nullptr) {
        arch_panic("Failed to decompress initramfs file\n");
    }
    return decompressed_buffer;
}

static vfs_result_t mount_initramfs(void* initramfs_data) {
    uint8_t* signature = (uint8_t*) initramfs_data;
    if(signature[0] != 'R' || signature[1] != 'D' || signature[2] != 'S' || signature[3] != 'K') {
        assert("unknown initramfs type\n");
    }

    return vfs_mount(&g_vfs_rdsk_ops, nullptr, initramfs_data);
}

void init_stage_vfs(uint32_t core_id) {
    if(!INIT_CORE_IS_BSP(core_id)) {
        return;
    }

    bootinfo_module_t* initramfs_module = nullptr;
    for(size_t i = 0; i < g_init_boot_info->module_count; i++) {
        bootinfo_module_t* module = &g_init_boot_info->modules[i];
        // @todo: Support more then just rdk
        if(string_compare_n_case(module->name, "/boot/initramfs", 15) == 0) {
            initramfs_module = module;
            break;
        }
    }

    if(initramfs_module == nullptr) {
        arch_panic("Failed to find initramfs\n");
    }

    void* initramfs_data = (void*) (initramfs_module->phys_addr + g_init_boot_info->hhdm_offset);
    initramfs_type_t type = determine_initramfs_type(initramfs_data);
    if(type == INITRAMFS_TYPE_GZIP) {
        initramfs_data = try_decompress(initramfs_data, initramfs_module->size);
        type = determine_initramfs_type(initramfs_data);
    } else {
        LOG_WARN("Detected uncompressed initramfs\n");
    }

    vfs_result_t res = mount_initramfs(initramfs_data);
    if(res != VFS_RESULT_OK) {
        arch_panic("Failed to mount initramfs (%d)\n", res);
    }
    LOG_OKAY("mounted initramfs\n");

    vfs_path_t dev_path = VFS_MAKE_ABS_PATH("/dev");
    res = vfs_mount(&g_vfs_devfs_ops, &dev_path, nullptr);
    if(res != VFS_RESULT_OK) {
        arch_panic("Failed to mount devfs at /dev (%d) ensure /dev exists in initramfs\n", res);
    }
    LOG_OKAY("mounted devfs at /dev\n");
}
