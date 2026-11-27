#pragma once
#include <stdint.h>

typedef struct {
    uint32_t c_iflag; // Input flags
    uint32_t c_oflag; // Output flags
    uint32_t c_cflag; // Control / communication flags
    uint32_t c_lflag; // Line flags
    uint8_t c_line; // Line discipline
    uint8_t c_cc[32]; // Control characters
    uint32_t c_ibaud; // Input baud rate
    uint32_t c_obaud; // Output baud rate
} uapi_termios_t;

/* bitwise constants for c_lflag in struct termios */
#define UAPI_TERMIOS_ISIG 0x0001
#define UAPI_TERMIOS_ICANON 0x0002
#define UAPI_TERMIOS_ECHO 0x0008
#define UAPI_TERMIOS_ECHOCTL 0x0200

/* indices into c_cc */
#define UAPI_TERMIOS_VINTR 0
#define UAPI_TERMIOS_VERASE 2
#define UAPI_TERMIOS_VKILL 3
#define UAPI_TERMIOS_VEOF 4
#define UAPI_TERMIOS_VWERASE 14
