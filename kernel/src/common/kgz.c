#include <common/log.h>
#include <lib/string.h>
#include <memory/heap.h>

#define KGZ_USE_OWN_MACROS
#define KGZ_MALLOC(size) heap_alloc(size)
#define KGZ_FREE(ptr, size) heap_free(ptr, size)
#define KGZ_MEMCPY(dst, src, n) memory_copy((dst), (src), (n))
#define KGZ_MEMSET(ptr, val, size) memory_set((ptr), (val), (size))
#define KGZ_PRINTF(...) LOG_FAIL(__VA_ARGS__)

#define KGZ_GRANULE 8

// NOLINTBEGIN
#define KGZ_IMPLEMENTATION
#include <common/kgz.h>
// NOLINTEND
