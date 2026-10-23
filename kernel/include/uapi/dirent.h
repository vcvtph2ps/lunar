#pragma once

#include <stdint.h>

#define UAPI_DIRENT_UNKNOWN 0
#define UAPI_DIRENT_FIFO 1
#define UAPI_DIRENT_CHR 2
#define UAPI_DIRENT_DIR 4
#define UAPI_DIRENT_BLK 6
#define UAPI_DIRENT_REG 8
#define UAPI_DIRENT_LNK 10
#define UAPI_DIRENT_SOCK 12
#define UAPI_DIRENT_WHT 14

#define UAPI_DIRENT_NAME_MAX 255

typedef struct {
    uint64_t d_ino;
    int64_t d_off;
    uint16_t d_reclen;
    uint8_t d_type;
    char d_name[UAPI_DIRENT_NAME_MAX + 1];
} uapi_dirent_t;
