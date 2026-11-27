#pragma once
#include <stdint.h>

typedef struct {
    uint16_t ws_row;
    uint16_t ws_col;
    uint16_t ws_xpixel;
    uint16_t ws_ypixel;
} uapi_ioctls_winsize_t;

#define UAPI_IOCTLS_TIOCGWINSZ 0x5413

#define UAPI_IOCTLS_TCGETS 0x5401
#define UAPI_IOCTLS_TCSETS 0x5402
#define UAPI_IOCTLS_TCSETSW 0x5403
#define UAPI_IOCTLS_TCSETSF 0x5404
