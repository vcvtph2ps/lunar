#include <lib/bitmap.h>
#include <memory/heap.h>

#include "lib/string.h"

bitmap_t* bitmap_create(size_t bit_count) {
    bitmap_t* bitmap = heap_alloc(sizeof(bitmap_t));

    size_t word_count = (bit_count + 63) / 64;

    bitmap->words = heap_zalloc(word_count * sizeof(uint64_t));
    bitmap->word_count = word_count;

    return bitmap;
}

void bitmap_resize(bitmap_t* bitmap, size_t new_bit_count) {
    size_t new_word_count = (new_bit_count + 63) / 64;

    uint64_t* words = heap_zalloc(new_word_count * sizeof(uint64_t));
    size_t copy_count = bitmap->word_count < new_word_count ? bitmap->word_count : new_word_count;
    memory_copy(words, bitmap->words, copy_count * sizeof(uint64_t));
    heap_free(bitmap->words, bitmap->word_count * sizeof(uint64_t));

    bitmap->words = words;
    bitmap->word_count = new_word_count;
}

void bitmap_free(bitmap_t* bitmap) {
    heap_free(bitmap->words, bitmap->word_count * sizeof(uint64_t));
    heap_free(bitmap, sizeof(bitmap_t));
}

void bitmap_set(bitmap_t* bitmap, bool value, size_t bit_index) {
    size_t word_index = bit_index / 64;
    size_t bit_offset = bit_index % 64;

    uint64_t mask = UINT64_C(1) << bit_offset;

    if(value)
        bitmap->words[word_index] |= mask;
    else
        bitmap->words[word_index] &= ~mask;
}

bool bitmap_get(bitmap_t* bitmap, size_t bit_index) {
    size_t word_index = bit_index / 64;
    size_t bit_offset = bit_index % 64;

    return (bitmap->words[word_index] & (UINT64_C(1) << bit_offset)) != 0;
}

size_t bitmap_find_free(bitmap_t* bitmap) {
    for(size_t word_index = 0; word_index < bitmap->word_count; word_index++) {
        uint64_t word = bitmap->words[word_index];

        if(word == UINT64_MAX) continue;

        uint64_t free_bits = ~word;
        size_t bit_offset = __builtin_ctzll(free_bits);

        return word_index * 64 + bit_offset;
    }

    return SIZE_MAX;
}
