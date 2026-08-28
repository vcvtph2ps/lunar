#include <common/assert.h>
#include <common/ldr/bin/elf.h>
#include <common/ldr/ldr.h>
#include <common/log.h>
#include <lib/math.h>
#include <lib/string.h>
#include <memory/heap.h>

#define LDR_MAGIC_SNIFF_SIZE 64

ldr_loader_t* g_loaders[] = { &g_elf_loader };

static bool ldr_read(const vfs_path_t* path, size_t offset, void* buffer, size_t size, size_t* out_read) {
    io_request_t io_req;
    io_req.type = IO_REQUEST_READ;
    io_req.read.buffer = buffer;
    io_req.read.count = size;
    io_req.read.offset = offset;
    io_req.read.bytes_read = 0;

    vfs_result_t result = vfs_perform_io(path, &io_req);
    if(result != VFS_RESULT_OK) {
        return false;
    }

    if(out_read != nullptr) {
        *out_read = io_req.read.bytes_read;
    }
    return true;
}

bool ldr_setup_process(vm_address_space_t* address_space, const vfs_path_t* path, const ldr_process_load_info_t* load_info, uintptr_t* inout_user_stack, uintptr_t* entry_point) {
    vfs_node_attr_t attributes;
    if(vfs_get_attributes(path, &attributes) != VFS_RESULT_OK) {
        LOG_FAIL("ldr: failed to get attributes of file\n");
        return false;
    }

    assert(attributes.type == VFS_NODE_TYPE_FILE);

    size_t sniff_size = math_min(attributes.size, (size_t) LDR_MAGIC_SNIFF_SIZE);
    if(sniff_size == 0) {
        LOG_FAIL("ldr: file is empty\n");
        return false;
    }

    uint8_t* buffer = heap_alloc(sniff_size);

    size_t read_size = 0;
    if(!ldr_read(path, 0, buffer, sniff_size, &read_size)) {
        LOG_FAIL("ldr: failed to read file beginning\n");
        heap_free(buffer, sniff_size);
        return false;
    }

    ldr_loader_t* loader = nullptr;
    for(size_t i = 0; i < sizeof(g_loaders) / sizeof(g_loaders[0]); i++) {
        if(g_loaders[i]->can_load(buffer, read_size)) {
            loader = g_loaders[i];
            break;
        }
    }

    heap_free(buffer, sniff_size);

    if(loader == nullptr) {
        // @TODO: #!interpreter [optional-one-arg-only]
        LOG_FAIL("ldr: failed to find a loader for the file\n");
        return false;
    }

    ldr_image_info_t image;
    void* format_info = nullptr;
    if(!loader->load(address_space, path, &image, &format_info)) {
        LOG_FAIL("ldr: loader failed\n");
        return false;
    }

    if(!loader->load_abi(address_space, inout_user_stack, load_info, format_info)) {
        LOG_FAIL("ldr: ABI setup failed\n");
        return false;
    }

    *entry_point = image.entry_point;
    return true;
}
