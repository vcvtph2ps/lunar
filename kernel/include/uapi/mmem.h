#pragma once

// NOLINTBEGIN
#define UAPI_PROT_NONE (0)
#define UAPI_PROT_READ (1 << 0)
#define UAPI_PROT_WRITE (1 << 1)
#define UAPI_PROT_EXEC (1 << 2)

#define UAPI_MAP_FILE (0)
#define UAPI_MAP_SHARED (1 << 0)
#define UAPI_MAP_PRIVATE (1 << 1)
#define UAPI_MAP_FIXED (1 << 4)
#define UAPI_MAP_ANON (1 << 5)
// NOLINTEND
