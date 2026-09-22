#pragma once

#include <common/sync/spinlock.h>
#include <lib/list.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    WAIT_OBJ_NOTIFICATION,
    WAIT_OBJ_SYNCHRONIZATION,
    WAIT_OBJ_SEMAPHORE
} wait_obj_type_t;

typedef enum {
    WAIT_OBJ_WAIT_ANY,
    WAIT_OBJ_WAIT_ALL,
} wait_obj_wait_type_t;

typedef struct {
    thread_t* thread;
    ATOMIC wait_obj_wait_type_t type;

    ATOMIC size_t waiting_on;
    ATOMIC size_t signaled;

    wait_obj_t* timeout_obj;

    spinlock_t lock;
    list_t wait_blocks_list;
} wait_entry_t; // NOLINT

typedef struct {
    wait_obj_t* obj;
    wait_entry_t* entry;

    ATOMIC bool signaled;

    list_node_t wait_object_node;
    list_node_t thread_node;
} wait_block_t; // NOLINT

struct wait_obj {
    spinlock_no_dw_t lock;

    wait_obj_type_t type;
    ATOMIC size_t signal_state;

    list_t wait_blocks;
};

typedef struct wait_obj wait_obj_t;

#define WAIT_OBJ_INIT(TYPE) ((wait_obj_t) { .lock = SPINLOCK_NO_DW_INIT, .type = (TYPE), .signal_state = 0, .wait_blocks = LIST_INIT })

bool wait_obj_signal(wait_obj_t* obj, size_t count);
bool wait_obj_is_signaled(wait_obj_t* obj);
void wait_obj_reset(wait_obj_t* obj);

/// @note: it is assumed the caller will be holding object lock
bool wait_obj_try_consume(wait_obj_t* obj);
