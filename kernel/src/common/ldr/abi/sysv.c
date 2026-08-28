#include <common/arch.h>
#include <common/ldr/abi/sysv.h>
#include <common/ldr/bin/elf.h>
#include <common/log.h>
#include <lib/buffer.h>
#include <lib/string.h>
#include <memory/heap.h>
#include <memory/vm.h>

typedef enum : uint64_t {
    AUXV_NULL = 0,
    AUXV_IGNORE = 1,
    AUXV_EXECFD = 2,
    AUXV_PHDR = 3,
    AUXV_PHENT = 4,
    AUXV_PHNUM = 5,
    AUXV_PAGESZ = 6,
    AUXV_BASE = 7,
    AUXV_FLAGS = 8,
    AUXV_ENTRY = 9,
    AUXV_NOTELF = 10,
    AUXV_UID = 11,
    AUXV_EUID = 12,
    AUXV_GID = 13,
    AUXV_EGID = 14,
    AUXV_SECURE = 23
} auxv_entry_t;

// @todo: should we move this into buffer.c?
static void insert_u64(buffer_t* data, uint64_t value) {
    buffer_append(data, (uint8_t*) &value, sizeof(value));
}

static void insert_auxv(buffer_t* data, auxv_entry_t entry, uint64_t value) {
    buffer_append(data, (uint8_t*) &entry, sizeof(entry));
    buffer_append(data, (uint8_t*) &value, sizeof(value));
}


typedef struct {
    uintptr_t* argv_p;
    uintptr_t* envp_p;
} sysv_info_block_out_t;

static size_t info_block_size(const ldr_process_load_info_t* load_info) {
    size_t size = 0;
    for(int i = 0; i < load_info->argc; i++) {
        size += string_length(load_info->argv[i]) + 1;
    }
    for(int i = 0; i < load_info->envc; i++) {
        size += string_length(load_info->envp[i]) + 1;
    }
    return size;
}

static uintptr_t create_info_block(vm_address_space_t* address_space, const ldr_process_load_info_t* load_info, sysv_info_block_out_t* info_block) {
    size_t size_of_info_block = info_block_size(load_info);

    // Allocate one extra slot on each array so the terminator can be written safely.
    info_block->argv_p = heap_alloc(sizeof(uintptr_t) * (load_info->argc + 1));
    info_block->envp_p = heap_alloc(sizeof(uintptr_t) * (load_info->envc + 1));
    if(info_block->argv_p == nullptr || info_block->envp_p == nullptr) {
        return 0;
    }

    if(size_of_info_block == 0) {
        return 0;
    }

    uintptr_t arg_block = (uintptr_t) vm_map_anon(address_space, VM_NO_HINT, ALIGN_UP(size_of_info_block, PAGE_SIZE_DEFAULT), VM_PROT_RW, VM_CACHE_NORMAL, VM_FLAG_ZERO);
    if(arg_block == 0) {
        return 0;
    }

    uintptr_t offset = 0;
    for(int i = 0; i < load_info->argc; i++) {
        size_t len = string_length(load_info->argv[i]) + 1;
        vm_copy_to(address_space, arg_block + offset, (void*) load_info->argv[i], len);
        info_block->argv_p[i] = arg_block + offset;
        offset += len;
    }
    info_block->argv_p[load_info->argc] = 0;

    for(int i = 0; i < load_info->envc; i++) {
        size_t len = string_length(load_info->envp[i]) + 1;
        vm_copy_to(address_space, arg_block + offset, (void*) load_info->envp[i], len);
        info_block->envp_p[i] = arg_block + offset;
        offset += len;
    }
    info_block->envp_p[load_info->envc] = 0;

    return arg_block;
}


bool sysv_load_abi(vm_address_space_t* address_space, uintptr_t* inout_user_stack, const ldr_process_load_info_t* load_info, const void* format_info) {
    sysv_info_block_out_t info_block;
    uintptr_t arg_block = create_info_block(address_space, load_info, &info_block);
    if(arg_block == 0 && (load_info->argc != 0 || load_info->envc != 0)) {
        arch_panic("sysv: failed to allocate arg block");
    }

    buffer_t* stack_buf = buffer_create(128);

    insert_u64(stack_buf, load_info->argc);
    for(int i = 0; i < load_info->argc; i++) {
        insert_u64(stack_buf, info_block.argv_p[i]);
    }
    insert_u64(stack_buf, 0); // argv null terminator

    for(int i = 0; i < load_info->envc; i++) {
        insert_u64(stack_buf, info_block.envp_p[i]);
    }
    insert_u64(stack_buf, 0); // envp null terminator

    heap_free(info_block.argv_p, sizeof(uintptr_t) * (load_info->argc + 1));
    heap_free(info_block.envp_p, sizeof(uintptr_t) * (load_info->envc + 1));

    elf_loader_info_t* elf_loader_info = (elf_loader_info_t*) format_info;

    insert_auxv(stack_buf, AUXV_PHDR, elf_loader_info->program_header_table);
    insert_auxv(stack_buf, AUXV_PHENT, elf_loader_info->program_header_entry_size);
    insert_auxv(stack_buf, AUXV_PHNUM, elf_loader_info->program_header_num);
    insert_auxv(stack_buf, AUXV_PAGESZ, PAGE_SIZE_DEFAULT);
    if(elf_loader_info->interp_base != 0) {
        insert_auxv(stack_buf, AUXV_BASE, elf_loader_info->interp_base);
    }
    insert_auxv(stack_buf, AUXV_ENTRY, elf_loader_info->image_entry_point);
    insert_u64(stack_buf, AUXV_NULL);

    uintptr_t stack_pointer = ALIGN_DOWN(*inout_user_stack - stack_buf->size, 16);
    LOG_STRC("stack_pointer=%p\n", (void*) stack_pointer);
    vm_copy_to(address_space, stack_pointer, (void*) stack_buf->data, stack_buf->size);

    buffer_free(stack_buf);

    *inout_user_stack = stack_pointer;
    return true;
}
