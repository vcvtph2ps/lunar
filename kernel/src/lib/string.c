#include <lib/string.h>
#include <stdint.h>

[[gnu::weak]] void memset(void* dest, int ch, size_t count) {
    for(size_t i = 0; i < count; i++) ((uint8_t*) dest)[i] = (unsigned char) ch;
}

[[gnu::weak]] void memcpy(void* dest, const void* src, size_t count) {
    for(size_t i = 0; i < count; i++) ((uint8_t*) dest)[i] = ((uint8_t*) src)[i];
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
