#include <lib/string.h>
#include <stddef.h>
#include <stdint.h>

#define IS_ALIGNED(x, align) (((uint64_t) (x) & ((align) - 1)) == 0)
#define MISALIGNMENT(x, align) (((uint64_t) (x) & ((align) - 1)))

// taken from https://git.evalyngoemer.com/evalynOS/evalynOS/src/commit/54fc9102b75f853f05f3326c2a3ad84f082ff31e/kernel/src/libc/string.c
// thanks evalyn :3
[[gnu::weak]] void* memcpy(void* restrict dest, const void* restrict src, size_t n) {
// on x86 do a rep movsb if copying 2kb+
#if defined(__ARCH_X86_64__)
    if(n >= 2048) {
        void* tmp = dest;
        asm volatile("rep movsb" : "+D"(dest), "+S"(src), "+c"(n) : : "memory");
        return tmp;
    }
#endif

    uint8_t* restrict pdest = (uint8_t* restrict) dest;
    const uint8_t* restrict psrc = (const uint8_t* restrict) src;

    // if copying a small ammount do a byte loop
    if(n <= sizeof(size_t) * 2) {
        for(size_t i = 0; i < n; i++) { pdest[i] = psrc[i]; }
        return dest;
    }

    // if dest and src can be aligned do a word copy
    if(MISALIGNMENT(pdest, sizeof(size_t)) == MISALIGNMENT(psrc, sizeof(size_t))) {
        // align dest and src
        while(!IS_ALIGNED(pdest, sizeof(size_t))) {
            *pdest++ = *psrc++;
            n--;
        }

        size_t* restrict wdest = (size_t* restrict) pdest;
        const size_t* restrict wsrc = (const size_t* restrict) psrc;

        // do bulk word copies if possible
        while(n >= (8 * sizeof(size_t))) {
            wdest[0] = wsrc[0];
            wdest[1] = wsrc[1];
            wdest[2] = wsrc[2];
            wdest[3] = wsrc[3];
            wdest[4] = wsrc[4];
            wdest[5] = wsrc[5];
            wdest[6] = wsrc[6];
            wdest[7] = wsrc[7];
            wdest += 8;
            wsrc += 8;
            n -= (8 * sizeof(size_t));
        }

        // finish remaining words
        while(n >= sizeof(size_t)) {
            *wdest++ = *wsrc++;
            n -= sizeof(size_t);
        }

        pdest = (uint8_t* restrict) wdest;
        psrc = (const uint8_t* restrict) wsrc;
    }

    // final per byte copy
    for(size_t i = 0; i < n; i++) { pdest[i] = psrc[i]; }

    return dest;
}

[[gnu::weak]] void* memset(void* s, int c, size_t n) {
// on x86 do a rep stosb if setting more than 2kb
#if defined(__ARCH_X86_64__)
    if(n >= 2048) {
        void* tmp = s;
        asm volatile("rep stosb" : "+D"(s), "+c"(n) : "a"(c) : "memory");
        return tmp;
    }
#endif

    uint8_t* pdest = (uint8_t*) s;

    // if copying a small ammount do a byte loop
    if(n <= sizeof(size_t) * 2) {
        for(size_t i = 0; i < n; i++) { pdest[i] = (uint8_t) c; }
        return s;
    }

    // align pdest
    while(!IS_ALIGNED(pdest, sizeof(size_t))) {
        *pdest++ = (uint8_t) c;
        n--;
    }

    size_t* wdest = (size_t*) pdest;

    size_t pattern = (unsigned char) c;
    pattern *= (~(size_t) 0) / 0xFF;

    // do bulk word writes if possible
    while(n >= (8 * sizeof(size_t))) {
        wdest[0] = pattern;
        wdest[1] = pattern;
        wdest[2] = pattern;
        wdest[3] = pattern;
        wdest[4] = pattern;
        wdest[5] = pattern;
        wdest[6] = pattern;
        wdest[7] = pattern;
        wdest += 8;
        n -= (8 * sizeof(size_t));
    }

    // finish remaining words
    while(n >= sizeof(size_t)) {
        wdest[0] = pattern;
        wdest++;
        n -= sizeof(size_t);
    }

    pdest = (uint8_t*) wdest;

    // finish remaining bytes
    for(size_t i = 0; i < n; i++) { pdest[i] = (uint8_t) c; }

    return s;
}

[[gnu::weak]] void memmove(void* dest, const void* src, size_t count) {
    if(src == dest) return;
    if(src > dest) {
        for(size_t i = 0; i < count; i++) ((uint8_t*) dest)[i] = ((uint8_t*) src)[i];
    } else {
        for(size_t i = count; i > 0; i--) ((uint8_t*) dest)[i - 1] = ((uint8_t*) src)[i - 1];
    }
}

int memcmp(const void* lhs, const void* rhs, size_t count) {
    for(size_t i = 0; i < count; i++) {
        if(*((uint8_t*) lhs + i) > *((uint8_t*) rhs + i)) return -1;
        if(*((uint8_t*) lhs + i) < *((uint8_t*) rhs + i)) return 1;
    }
    return 0;
}

int strlen(const char* str) {
    int length = 0;
    while(str[length] != '\0') length++;
    return length;
}

int strcmp(const char* s1, const char* s2) {
    while(*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(const unsigned char*) s1 - *(const unsigned char*) s2;
}

int strcasecmp(const char* s1, const char* s2) {
    while(*s1 && *s2) {
        unsigned char c1 = (unsigned char) *s1;
        unsigned char c2 = (unsigned char) *s2;
        if(c1 >= 'A' && c1 <= 'Z') c1 += 32;
        if(c2 >= 'A' && c2 <= 'Z') c2 += 32;
        if(c1 != c2) return c1 - c2;
        s1++;
        s2++;
    }
    return *(const unsigned char*) s1 - *(const unsigned char*) s2;
}
