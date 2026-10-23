#pragma once

// NOLINTBEGIN
#define UAPI_O_RDONLY (1 << 0)
#define UAPI_O_WRONLY (1 << 1)
#define UAPI_O_RDWR (UAPI_O_RDONLY | UAPI_O_WRONLY)

#define UAPI_O_CREATE (1 << 6)
#define UAPI_O_EXCL (1 << 7)
#define UAPI_O_NOCTTY (1 << 8)
#define UAPI_O_TRUNC (1 << 9)
#define UAPI_O_APPEND (1 << 10)
#define UAPI_O_NONBLOCK (1 << 11)
#define UAPI_O_DSYNC (1 << 12)
#define UAPI_O_ASYNC (1 << 13)
#define UAPI_O_DIRECT (1 << 14)
#define UAPI_O_LARGEFILE (1 << 15)
#define UAPI_O_DIRECTORY (1 << 16)
#define UAPI_O_NOFOLLOW (1 << 17)
#define UAPI_O_NOATIME (1 << 18)
#define UAPI_O_CLOEXEC (1 << 19)

#define UAPI_O_PATH (1 << 21)
#define UAPI_O_TMPFILE (1 << 22)
#define UAPI_O_SYNC (1 << 23)

#define UAPI_O_RSYNC UAPI_O_SYNC
#define UAPI_O_ACCMODE (UAPI_O_RDWR | UAPI_O_PATH)

#define UAPI_AT_FDCWD (-100)

// NOLINTEND
