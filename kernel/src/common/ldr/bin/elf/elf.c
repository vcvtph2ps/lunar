#include <common/assert.h>
#include <common/ldr/bin/elf.h>
#include <common/log.h>

#include "common/fs/vfs.h"
#include "elf_fmt.h"
#include "lib/string.h"
#include "lib/types.h"
#include "memory/heap.h"
#include "memory/vm.h"


#if defined(__ARCH_X86_64__)
#define ELF_MACHINE_EXPECTED ELF_MACHINE_X86_64
#define ELF_MACHINE_EXPECTED_NAME "ELF_MACHINE_X86_64"
#elif defined(__ARCH_RISCV__)
#define ELF_MACHINE_EXPECTED ELF_MACHINE_RISCV
#define ELF_MACHINE_EXPECTED_NAME "ELF_MACHINE_RISCV"
#endif

static bool elf_file_supported(const elf64_elf_header_t* elf_header) {
    assert(elf_header);

    if(memory_compare(elf_header->ident, "\x7f" "ELF", 4) != 0) {
        LOG_STRC("elf: invalid elf magic '%c%c%c%c' != '%cELF'\n", elf_header->ident[0], elf_header->ident[1], elf_header->ident[2], elf_header->ident[3], 0x7F);
        return false;
    }

    if(elf_header->ident[ELF_CLASS_IDX] != ELF_CLASS_64_BIT) {
        LOG_STRC("elf: invalid elf class '%d' != ELF_CLASS_64_BIT\n", elf_header->ident[ELF_CLASS_IDX]);
        return false;
    }

    if(elf_header->ident[ELF_DATA_IDX] != ELF_DATA_2LSB) {
        LOG_STRC("elf: invalid elf data format '%d' != ELF_DATA_2LSB\n", elf_header->ident[ELF_DATA_IDX]);
        return false;
    }

    if(elf_header->machine != ELF_MACHINE_EXPECTED) {
        LOG_STRC("elf: invalid machine '%d' != %s\n", elf_header->machine, ELF_MACHINE_EXPECTED_NAME);
        return false;
    }

    if(elf_header->type != ELF_TYPE_EXEC && elf_header->type != ELF_TYPE_DYN) {
        LOG_STRC("elf: invalid machine '%d' != ELF_TYPE_EXEC or ELF_TYPE_DYN\n", elf_header->type);
        return false;
    }

    return true;
}

typedef struct {
    uintptr_t image_start_address;
    uintptr_t image_end_address;
    uintptr_t image_offset;
} elf_image_allocation_t;

static bool internal_allocate_for_image(vm_address_space_t* address_space, const elf64_elf_header_t* elf_header, const elf64_program_header_t* phdr_cache, elf_image_allocation_t* out_allocation) {
    elf_image_allocation_t allocation = { 0 };
    allocation.image_start_address = UINTPTR_MAX;
    allocation.image_end_address = 0;

    for(size_t i = 0; i < elf_header->program_header_count; i++) {
        if(phdr_cache[i].type == ELF_PROG_TYPE_LOAD) {
            if(phdr_cache[i].vaddr < allocation.image_start_address) {
                allocation.image_start_address = ALIGN_DOWN(phdr_cache[i].vaddr, PAGE_SIZE_DEFAULT);
            }
            if(phdr_cache[i].vaddr + phdr_cache[i].mem_size > allocation.image_end_address) {
                allocation.image_end_address = ALIGN_UP(phdr_cache[i].vaddr + phdr_cache[i].mem_size, PAGE_SIZE_DEFAULT);
            }
        }
    }

    LOG_STRC("lowest_address = 0x%lx, highest_address = 0x%lx\n", allocation.image_start_address, allocation.image_end_address);

    uintptr_t target_allocation = allocation.image_start_address + allocation.image_offset;
    size_t image_size = ALIGN_UP((allocation.image_end_address - allocation.image_start_address), PAGE_SIZE_DEFAULT);
    LOG_STRC("target_allocation = 0x%lx, image_size = 0x%lx\n", target_allocation, image_size);

    if(elf_header->type == ELF_TYPE_DYN) {
        uintptr_t allocated = (virt_addr_t) vm_map_anon(address_space, VM_NO_HINT, image_size, VM_PROT_RW, VM_CACHE_NORMAL, VM_FLAG_ZERO);
        assert(allocated != 0 && "Failed to allocate memory for elf image");
        allocation.image_offset = allocated;
    } else {
        LOG_STRC("target_allocation = 0x%lx\n", target_allocation);
        virt_addr_t result_vaddr = (virt_addr_t) vm_map_anon(address_space, (void*) target_allocation, image_size, VM_PROT_RW, VM_CACHE_NORMAL, VM_FLAG_ZERO | VM_FLAG_FIXED);
        assert(result_vaddr != 0 && "Failed to allocate memory for elf image");
    }
    LOG_STRC("image_slide = 0x%lx, image_size = 0x%lx\n", allocation.image_offset, image_size);

    out_allocation->image_start_address = allocation.image_start_address;
    out_allocation->image_end_address = allocation.image_end_address;
    out_allocation->image_offset = allocation.image_offset;
    return true;
}

static bool internal_elf_handle_pt_load(vm_address_space_t* address_space, vfs_path_t* path, size_t phdr_index, elf64_program_header_t* phdr, elf_image_allocation_t* allocation) {
    vm_protection_t flags = VM_PROT_NO_ACCESS;
    flags.read = (phdr->flags & ELF_PROG_FLAGS_READ) != 0;
    flags.write = (phdr->flags & ELF_PROG_FLAGS_WRITE) != 0;
    flags.execute = (phdr->flags & ELF_PROG_FLAGS_EXECUTE) != 0;

    uintptr_t start_vaddr = ALIGN_DOWN(allocation->image_offset + phdr->vaddr, PAGE_SIZE_DEFAULT);
    uintptr_t end_vaddr = ALIGN_DOWN(allocation->image_offset + phdr->vaddr, PAGE_SIZE_DEFAULT);
    vm_rewrite_prot(address_space, (void*) start_vaddr, end_vaddr - start_vaddr, flags);

    // @todo: we must bounce buffer user io...
    void* phdr_data = heap_alloc(phdr->file_size);
    io_request_t io_req;
    io_req.type = IO_REQUEST_READ;
    io_req.read.buffer = phdr_data;
    io_req.read.count = phdr->file_size;
    io_req.read.offset = phdr->offset;
    io_req.read.bytes_read = 0;

    vfs_result_t result = vfs_perform_io(path, &io_req);
    if(result != VFS_RESULT_OK) {
        LOG_FAIL("elf: failed to read phdr data %zu, (%d)\n", phdr_index, result);
        heap_free(phdr_data, phdr->file_size);
        return false;
    }

    vm_copy_to(address_space, allocation->image_offset + phdr->vaddr, phdr_data, phdr->file_size);
    heap_free(phdr_data, phdr->file_size);
    return true;
}

static bool internal_elf_handle_pt_interp(vm_address_space_t* address_space, vfs_path_t* path, elf64_program_header_t* phdr, elf_loader_info_t* out_loader_info) {
    void* phdr_data = heap_alloc(phdr->file_size);
    io_request_t io_req;
    io_req.type = IO_REQUEST_READ;
    io_req.read.buffer = phdr_data;
    io_req.read.count = phdr->file_size;
    io_req.read.offset = phdr->offset;
    io_req.read.bytes_read = 0;

    vfs_result_t result = vfs_perform_io(path, &io_req);
    if(result != VFS_RESULT_OK) {
        LOG_FAIL("elf: failed to read interp, (%d)\n", result);
        heap_free(phdr_data, phdr->file_size);
        return false;
    }

    LOG_STRC("interpreter: %s\n", (const char*) phdr_data);
    elf_loader_info_t interp_loader_info;
    if(!elf_load_file(address_space, &VFS_MAKE_ABS_PATH((const char*) phdr_data), &interp_loader_info)) {
        heap_free((void*) phdr_data, phdr->file_size);
        return false;
    }

    out_loader_info->executable_entry_point = interp_loader_info.executable_entry_point;
    out_loader_info->interp_base = interp_loader_info.image_offset;

    heap_free((void*) phdr_data, phdr->file_size);
    return true;
}


static bool internal_elf_load_image(vm_address_space_t* address_space, elf64_elf_header_t* elf_header, vfs_path_t* path, elf_loader_info_t* out_loader_info) {
    elf_image_allocation_t allocation = {};

    elf64_program_header_t* phdr_cache = heap_alloc(sizeof(elf64_program_header_t) * elf_header->program_header_count);
    for(size_t i = 0; i < elf_header->program_header_count; i++) {
        io_request_t io_req;
        io_req.type = IO_REQUEST_READ;
        io_req.read.buffer = &phdr_cache[i];
        io_req.read.count = sizeof(elf64_program_header_t);
        io_req.read.offset = elf_header->program_header_offset + i * elf_header->program_header_entry_size;
        io_req.read.bytes_read = 0;

        vfs_result_t result = vfs_perform_io(path, &io_req);
        if(result != VFS_RESULT_OK) {
            LOG_FAIL("elf: failed to load phdr %zu, (%d)\n", i, result);
            heap_free(phdr_cache, sizeof(elf64_program_header_t) * elf_header->program_header_count);
            return false;
        }

        LOG_STRC("phdr[%zu].type = 0x%x\n", i, phdr_cache[i].type);
        LOG_STRC("phdr[%zu].vaddr = 0x%lx, mem_size = 0x%lx, file_size = 0x%lx\n", i, phdr_cache[i].vaddr, phdr_cache[i].mem_size, phdr_cache[i].file_size);
    }

    // Allocate memory for image
    bool res = internal_allocate_for_image(address_space, elf_header, phdr_cache, &allocation);
    if(!res) {
        LOG_FAIL("elf: failed to allocate for image");
        heap_free(phdr_cache, sizeof(elf64_program_header_t) * elf_header->program_header_count);
        return false;
    }

    // Fill out loader info
    out_loader_info->image_offset = allocation.image_offset;
    out_loader_info->executable_entry_point = allocation.image_offset + elf_header->entry;
    out_loader_info->image_entry_point = allocation.image_offset + elf_header->entry;

    out_loader_info->program_header_num = elf_header->program_header_count;
    out_loader_info->program_header_entry_size = elf_header->program_header_entry_size;

    for(size_t i = 0; i < elf_header->program_header_count; i++) {
        if(phdr_cache[i].type == ELF_PROG_TYPE_PHDR) {
            out_loader_info->program_header_table = allocation.image_offset + phdr_cache[i].vaddr;
            break;
        }
    }

    if(out_loader_info->program_header_table == 0) {
        out_loader_info->program_header_table = allocation.image_offset + elf_header->program_header_offset;
    }

    for(size_t i = 0; i < elf_header->program_header_count; i++) {
        if(phdr_cache[i].type != ELF_PROG_TYPE_LOAD && phdr_cache[i].type != ELF_PROG_TYPE_INTERP) {
            continue;
        }

        LOG_STRC("elf: loading segment %zu: vaddr=0x%lx, size=%zu\n", i, allocation.image_offset + phdr_cache[i].vaddr, allocation.image_offset + phdr_cache[i].file_size);

        bool success;

        if(phdr_cache[i].type == ELF_PROG_TYPE_LOAD) {
            success = internal_elf_handle_pt_load(address_space, path, i, &phdr_cache[i], &allocation);
        } else if(phdr_cache[i].type == ELF_PROG_TYPE_INTERP) {
            success = internal_elf_handle_pt_interp(address_space, path, &phdr_cache[i], out_loader_info);
        } else {
            ASSERT_UNREACHABLE();
        }

        if(!success) {
            // @todo: print path
            LOG_FAIL("elf: failed to load interp\n");
            heap_free(phdr_cache, sizeof(elf64_program_header_t) * elf_header->program_header_count);
            return false;
        }
    }

    heap_free(phdr_cache, sizeof(elf64_program_header_t) * elf_header->program_header_count);
    return true;
}

bool elf_load_file(vm_address_space_t* address_space, vfs_path_t* path, elf_loader_info_t* out_elf_loader_info) {
    vfs_node_attr_t attributes;
    if(vfs_get_attributes(path, &attributes) != VFS_RESULT_OK) {
        LOG_FAIL("elf: failed to get attributes of elf file");
        return false;
    }

    assert(attributes.type == VFS_NODE_TYPE_FILE && attributes.size >= sizeof(elf64_elf_header_t));

    elf64_elf_header_t* elf_header = heap_alloc(sizeof(elf64_elf_header_t));

    io_request_t io_req;
    io_req.type = IO_REQUEST_READ;
    io_req.read.buffer = elf_header;
    io_req.read.count = sizeof(elf64_elf_header_t);
    io_req.read.offset = 0;
    io_req.read.bytes_read = 0;

    vfs_result_t result = vfs_perform_io(path, &io_req);
    if(result != VFS_RESULT_OK) {
        // @todo: print path
        LOG_FAIL("elf: failed to load elf file %d\n", result);
        heap_free(elf_header, sizeof(elf64_elf_header_t));
        return false;
    }

    assert(io_req.read.count == io_req.read.bytes_read);

    if(!elf_file_supported(elf_header)) {
        LOG_FAIL("elf: unsupported elf file\n");
        heap_free(elf_header, sizeof(elf64_elf_header_t));
        return false;
    }

    if(!internal_elf_load_image(address_space, elf_header, path, out_elf_loader_info)) {
        LOG_FAIL("elf: failed to load elf image\n");
        heap_free(elf_header, sizeof(elf64_elf_header_t));
        return false;
    }

    return true;
}
