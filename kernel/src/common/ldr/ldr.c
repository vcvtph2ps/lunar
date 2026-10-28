#include <common/assert.h>
#include <common/ldr/bin/elf.h>
#include <common/ldr/ldr.h>
#include <common/log.h>
#include <lib/math.h>
#include <lib/string.h>
#include <memory/heap.h>

#include "common/fs/vfs.h"

#define LDR_MAGIC_SNIFF_SIZE 64
#define LDR_SHEBANG_MAX_SIZE 256

ldr_loader_t* g_loaders[] = { &g_elf_loader };

static bool ldr_read(vfs_node_t* node, size_t offset, void* buffer, size_t size, size_t* out_read) {
    io_request_t io_req;
    io_req.type = IO_REQUEST_READ;
    io_req.read.buffer = buffer;
    io_req.read.count = size;
    io_req.read.offset = offset;
    io_req.read.bytes_read = 0;

    vfs_result_t result = vfs_perform_io_node(node, &io_req);
    if(result != VFS_RESULT_OK) {
        return false;
    }

    if(out_read != nullptr) {
        *out_read = io_req.read.bytes_read;
    }
    return true;
}

static bool ldr_setup_process_shebang(vm_address_space_t* address_space, vfs_node_t* node, const ldr_process_load_info_t* load_info, uintptr_t* inout_user_stack, uintptr_t* entry_point) {
    vfs_node_attr_t attributes;
    if(vfs_get_attributes_node(node, &attributes) != VFS_RESULT_OK) {
        LOG_FAIL("ldr: failed to get attributes of file\n");
        return false;
    }

    assert(attributes.type == VFS_NODE_TYPE_FILE);

    size_t read_size = attributes.size < (size_t) LDR_SHEBANG_MAX_SIZE ? attributes.size : (size_t) LDR_SHEBANG_MAX_SIZE;
    if(read_size <= 2) {
        LOG_FAIL("ldr: shebang file is too small\n");
        return false;
    }

    size_t shebang_buffer_size = read_size + 1;
    char* shebang_buffer = heap_alloc(shebang_buffer_size);
    if(shebang_buffer == nullptr) {
        LOG_FAIL("ldr: failed to allocate shebang buffer\n");
        return false;
    }

    size_t bytes_read = 0;
    if(!ldr_read(node, 0, shebang_buffer, read_size, &bytes_read) || bytes_read <= 2) {
        LOG_FAIL("ldr: failed to read shebang\n");
        heap_free(shebang_buffer, shebang_buffer_size);
        return false;
    }
    shebang_buffer[bytes_read] = '\0';

    /// We only need to care about the first line
    int newline = string_find_first_instance(shebang_buffer, '\n', bytes_read);
    size_t line_size = newline >= 0 ? (size_t) newline : bytes_read;
    if(line_size > 0 && shebang_buffer[line_size - 1] == '\r') {
        line_size--;
    }

    char* cursor = shebang_buffer + 2;
    char* end = shebang_buffer + line_size;

    /// Skip any whitespace separating the '#!' from the interpreter path
    while(cursor < end && (*cursor == ' ' || *cursor == '\t')) {
        cursor++;
    }

    char* interpreter_start = cursor;
    while(cursor < end && *cursor != ' ' && *cursor != '\t') {
        cursor++;
    }
    size_t interpreter_length = (size_t) (cursor - interpreter_start);
    if(interpreter_length == 0) {
        LOG_FAIL("ldr: shebang is missing an interpreter\n");
        heap_free(shebang_buffer, shebang_buffer_size);
        return false;
    }

    /// Skip whitespace separating the interpreter from the optional argument
    while(cursor < end && (*cursor == ' ' || *cursor == '\t')) {
        cursor++;
    }

    /// everything remaining on the line is the optional argument
    char* optional_arg_start = cursor;
    char* optional_arg_end = end;
    while(optional_arg_end > optional_arg_start && (optional_arg_end[-1] == ' ' || optional_arg_end[-1] == '\t')) {
        optional_arg_end--;
    }
    size_t optional_arg_length = (size_t) (optional_arg_end - optional_arg_start);

    char* interpreter_path = heap_alloc(interpreter_length + 1);
    if(interpreter_path == nullptr) {
        heap_free(shebang_buffer, shebang_buffer_size);
        return false;
    }
    memory_copy(interpreter_path, interpreter_start, interpreter_length);
    interpreter_path[interpreter_length] = '\0';

    char* optional_arg = nullptr;
    if(optional_arg_length > 0) {
        optional_arg = heap_alloc(optional_arg_length + 1);
        if(optional_arg == nullptr) {
            heap_free(interpreter_path, interpreter_length + 1);
            heap_free(shebang_buffer, shebang_buffer_size);
            return false;
        }
        memory_copy(optional_arg, optional_arg_start, optional_arg_length);
        optional_arg[optional_arg_length] = '\0';
    }

    heap_free(shebang_buffer, shebang_buffer_size);

    char* script_path = nullptr;
    size_t script_path_size = 0;
    if(vfs_path_to(node, &script_path, &script_path_size) != VFS_RESULT_OK) {
        LOG_FAIL("ldr: failed to resolve script path\n");
        if(optional_arg != nullptr) {
            heap_free(optional_arg, optional_arg_length + 1);
        }
        heap_free(interpreter_path, interpreter_length + 1);
        return false;
    }

    /// argv becomes: [interpreter] [optional-arg] [script-path] [original argv[1]..argv[argc-1]]
    /// Drop the original argv[0] as it is replaced by the interpreter path.
    int tail_argc = load_info->argc > 1 ? load_info->argc - 1 : 0;
    int new_argc = 2 + (optional_arg != nullptr ? 1 : 0) + tail_argc;

    const char** new_argv = heap_alloc(sizeof(const char*) * new_argc);
    if(new_argv == nullptr) {
        heap_free(script_path, script_path_size);
        if(optional_arg != nullptr) {
            heap_free(optional_arg, optional_arg_length + 1);
        }
        heap_free(interpreter_path, interpreter_length + 1);
        return false;
    }

    int new_argv_index = 0;
    new_argv[new_argv_index++] = interpreter_path;
    if(optional_arg != nullptr) {
        new_argv[new_argv_index++] = optional_arg;
    }
    new_argv[new_argv_index++] = script_path;
    for(int i = 1; i < load_info->argc; i++) {
        new_argv[new_argv_index++] = load_info->argv[i];
    }
    assert(new_argv_index == new_argc);

    vfs_node_t* interpreter_node = nullptr;
    vfs_result_t lookup_result = vfs_lookup(&VFS_MAKE_ABS_PATH(interpreter_path), true, &interpreter_node);
    if(lookup_result != VFS_RESULT_OK) {
        LOG_FAIL("ldr: failed to look up interpreter \"%s\" (%d)\n", interpreter_path, lookup_result);
        heap_free((void*) new_argv, sizeof(const char*) * new_argc);
        heap_free(script_path, script_path_size);
        if(optional_arg != nullptr) {
            heap_free(optional_arg, optional_arg_length + 1);
        }
        heap_free(interpreter_path, interpreter_length + 1);
        return false;
    }

    ldr_process_load_info_t new_load_info;
    new_load_info.argv = new_argv;
    new_load_info.envp = load_info->envp;
    new_load_info.argc = new_argc;
    new_load_info.envc = load_info->envc;

    bool result = ldr_setup_process(address_space, interpreter_node, &new_load_info, inout_user_stack, entry_point);

    vfs_node_put(interpreter_node);
    heap_free((void*) new_argv, sizeof(const char*) * new_argc);
    heap_free(script_path, script_path_size);
    if(optional_arg != nullptr) {
        heap_free(optional_arg, optional_arg_length + 1);
    }
    heap_free(interpreter_path, interpreter_length + 1);

    return result;
}

bool ldr_setup_process(vm_address_space_t* address_space, vfs_node_t* node, const ldr_process_load_info_t* load_info, uintptr_t* inout_user_stack, uintptr_t* entry_point) {
    vfs_node_attr_t attributes;
    if(vfs_get_attributes_node(node, &attributes) != VFS_RESULT_OK) {
        LOG_FAIL("ldr: failed to get attributes of file\n");
        return false;
    }

    size_t sniff_size = math_min(attributes.size, (size_t) LDR_MAGIC_SNIFF_SIZE);
    if(sniff_size == 0) {
        LOG_FAIL("ldr: file is empty\n");
        return false;
    }

    uint8_t* buffer = heap_alloc(sniff_size);

    size_t read_size = 0;
    if(!ldr_read(node, 0, buffer, sniff_size, &read_size)) {
        LOG_FAIL("ldr: failed to read file beginning\n");
        heap_free(buffer, sniff_size);
        return false;
    }

    if(buffer[0] == '#' && buffer[1] == '!') {
        heap_free(buffer, sniff_size);
        return ldr_setup_process_shebang(address_space, node, load_info, inout_user_stack, entry_point);
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
        LOG_FAIL("ldr: failed to find a loader for the file\n");
        return false;
    }

    ldr_image_info_t image;
    void* format_info = nullptr;
    if(!loader->load(address_space, node, &image, &format_info)) {
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
