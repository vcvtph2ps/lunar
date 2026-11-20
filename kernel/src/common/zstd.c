// NOLINTBEGIN
#include <common/zstd.h>
#include <lib/types.h>
#include <memory/heap.h>
#include <stdint.h>

#define ZSTD_STATIC_LINKING_ONLY
#include <zstd.h>

void* ZSTD_malloc(size_t size);
void* ZSTD_calloc(size_t num, size_t size);
void ZSTD_free(void* ptr);

#define ZSTD_ALLOC_ALIGN ((size_t) 16)

typedef struct {
    void* raw;
    size_t size;
} zstd_alloc_header_t;

void* ZSTD_malloc(size_t size) {
    if(size == 0) { return nullptr; }
    if(size > SIZE_MAX - sizeof(zstd_alloc_header_t) - ZSTD_ALLOC_ALIGN) { return nullptr; }

    size_t total = size + sizeof(zstd_alloc_header_t) + ZSTD_ALLOC_ALIGN;
    void* raw = heap_alloc(total);
    if(raw == nullptr) { return nullptr; }

    uintptr_t user = ALIGN_UP((uintptr_t) raw + sizeof(zstd_alloc_header_t), ZSTD_ALLOC_ALIGN);
    zstd_alloc_header_t* header = (zstd_alloc_header_t*) (user - sizeof(zstd_alloc_header_t));
    header->raw = raw;
    header->size = total;
    return (void*) user;
}

void* ZSTD_calloc(size_t num, size_t size) {
    if(num != 0 && size > SIZE_MAX / num) { return nullptr; }

    size_t total = num * size;
    void* ptr = ZSTD_malloc(total);
    if(ptr != nullptr) { __builtin_memset(ptr, 0, total); }
    return ptr;
}

void ZSTD_free(void* ptr) {
    if(ptr == nullptr) { return; }

    zstd_alloc_header_t* header = (zstd_alloc_header_t*) ((uintptr_t) ptr - sizeof(zstd_alloc_header_t));
    heap_free(header->raw, header->size);
}

void* zstd_decompress(const void* src, size_t src_size, size_t* buffer_size) {
    unsigned long long frame_size = ZSTD_getFrameContentSize(src, src_size);
    if(frame_size == ZSTD_CONTENTSIZE_ERROR) { return nullptr; }
    if(frame_size == ZSTD_CONTENTSIZE_UNKNOWN) {
        frame_size = ZSTD_decompressBound(src, src_size);
        if(frame_size == ZSTD_CONTENTSIZE_ERROR) { return nullptr; }
    }
    if(frame_size > SIZE_MAX) { return nullptr; }

    size_t capacity = ALIGN_UP((size_t) frame_size, PAGE_SIZE_DEFAULT);
    if(capacity == 0) { capacity = PAGE_SIZE_DEFAULT; }

    void* dst = heap_alloc(capacity);
    if(dst == nullptr) { return nullptr; }

    size_t workspace_size = ZSTD_estimateDCtxSize();
    size_t workspace_total = workspace_size + ZSTD_ALLOC_ALIGN;
    void* workspace_raw = heap_alloc(workspace_total);
    if(workspace_raw == nullptr) {
        heap_free(dst, capacity);
        return nullptr;
    }
    void* workspace = (void*) ALIGN_UP((uintptr_t) workspace_raw, ZSTD_ALLOC_ALIGN);

    ZSTD_DCtx* dctx = ZSTD_initStaticDCtx(workspace, workspace_size);
    if(dctx == nullptr) {
        heap_free(workspace_raw, workspace_total);
        heap_free(dst, capacity);
        return nullptr;
    }

    size_t got = ZSTD_decompressDCtx(dctx, dst, capacity, src, src_size);
    heap_free(workspace_raw, workspace_total);
    if(ZSTD_isError(got)) {
        heap_free(dst, capacity);
        return nullptr;
    }

    if(buffer_size != nullptr) { *buffer_size = capacity; }
    return dst;
}
// NOLINTEND