#include <lib/buffer.h>
#include <lib/string.h>
#include <memory/heap.h>

buffer_t* buffer_create(size_t capacity) {
    buffer_t* buffer = heap_alloc(sizeof(buffer_t));
    buffer->data = heap_alloc(capacity);
    buffer->size = 0;
    buffer->capacity = capacity;
    return buffer;
}

void buffer_free(buffer_t* buffer) {
    heap_free(buffer->data, buffer->capacity);
    buffer->data = nullptr;
    buffer->size = 0;
    buffer->capacity = 0;
}

void buffer_append(buffer_t* buffer, const void* data, size_t size) {
    if(buffer->size + size > buffer->capacity) {
        buffer->data = heap_realloc(buffer->data, buffer->capacity, buffer->capacity * 2);
        buffer->capacity *= 2;
    }

    memory_copy(buffer->data + buffer->size, data, size);
    buffer->size += size;
}

void buffer_slice(buffer_t* buffer, size_t offset, size_t size) {
    if(offset + size > buffer->size) {
        return;
    }

    memory_move(buffer->data, buffer->data + offset, size);
    buffer->size = size;
}

void buffer_clear(buffer_t* buffer) {
    buffer->size = 0;
}
