#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint64_t* words;
    size_t word_count;
} bitmap_t;

bitmap_t* bitmap_create(size_t bit_count);
void bitmap_resize(bitmap_t* bitmap, size_t new_bit_count);
void bitmap_free(bitmap_t* bitmap);

void bitmap_set(bitmap_t* bitmap, bool value, size_t bit_index);
bool bitmap_get(bitmap_t* bitmap, size_t bit_index);
size_t bitmap_find_free(bitmap_t* bitmap);
