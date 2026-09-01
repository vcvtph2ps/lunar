#pragma once
#include <common/fs/vfs.h>
#include <common/sync/spinlock.h>
#include <lib/bitmap.h>
#include <stdint.h>

typedef struct {
    vfs_node_t* node;
    size_t offset;
} fd_store_entry_t;

typedef struct {
    spinlock_t lock;

    bitmap_t* bitmap;
    fd_store_entry_t* fds;
    size_t fds_elements_count;
} fd_store_t;

fd_store_t* fd_store_create();
void fd_store_free(fd_store_t* store);

uint32_t fd_store_create_fd(fd_store_t* store, vfs_node_t* node);
fd_store_entry_t* fd_store_get_fd(fd_store_t* store, uint32_t fd);
bool fd_store_free_fd(fd_store_t* store, uint32_t fd);
