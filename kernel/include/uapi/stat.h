#pragma once

#include <uapi/types.h>

#define UAPI_STAT_IFMT 0x0F000
#define UAPI_STAT_IFBLK 0x06000
#define UAPI_STAT_IFCHR 0x02000
#define UAPI_STAT_IFIFO 0x01000
#define UAPI_STAT_IFREG 0x08000
#define UAPI_STAT_IFDIR 0x04000
#define UAPI_STAT_IFLNK 0x0A000
#define UAPI_STAT_IFSOCK 0x0C000

#define UAPI_STAT_IRWXU 0700
#define UAPI_STAT_IRUSR 0400
#define UAPI_STAT_IWUSR 0200
#define UAPI_STAT_IXUSR 0100
#define UAPI_STAT_IRWXG 070
#define UAPI_STAT_IRGRP 040
#define UAPI_STAT_IWGRP 020
#define UAPI_STAT_IXGRP 010
#define UAPI_STAT_IRWXO 07
#define UAPI_STAT_IROTH 04
#define UAPI_STAT_IWOTH 02
#define UAPI_STAT_IXOTH 01
#define UAPI_STAT_ISUID 04000
#define UAPI_STAT_ISGID 02000
#define UAPI_STAT_ISVTX 01000

typedef struct {
    uint64_t st_dev;
    uint64_t st_ino;
    uint32_t st_mode;
    uint64_t st_nlink;
    uapi_uid_t st_uid;
    uapi_gid_t st_gid;
    uint64_t st_rdev;
    uint64_t st_size;
    uint64_t st_blksize;
    int64_t st_blocks;
    uapi_timespec_t st_atim;
    uapi_timespec_t st_mtim;
    uapi_timespec_t st_ctim;
} uapi_stat_t;
