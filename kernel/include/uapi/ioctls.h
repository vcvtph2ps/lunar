#pragma once
#include <stdint.h>

typedef struct {
    uint16_t ws_row;
    uint16_t ws_col;
    uint16_t ws_xpixel;
    uint16_t ws_ypixel;
} uapi_ioctls_winsize_t;

#define UAPI_IOCTLS_TIOCGWINSZ 0x5413
