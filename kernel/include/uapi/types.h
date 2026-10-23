#pragma once
#include <stdint.h>

// NOLINTBEGIN
typedef int32_t uapi_pid_t;
typedef uint32_t uapi_uid_t;
typedef uint32_t uapi_gid_t;

typedef struct {
    int64_t tv_sec;
    uint32_t tv_nsec;
} uapi_timespec_t;
// NOLINTEND
