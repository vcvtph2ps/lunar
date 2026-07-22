#pragma once
#include <common/sched/thread.h>
#include <common/sync/spinlock.h>
#include <lib/list.h>
#include <lib/types.h>

typedef struct {
    spinlock_no_dw_t lock;
    list_t wait_queue;
} sleep_queue_t;

void sleep_queue_insert(sleep_queue_t* queue, thread_t* item);
void sleep_queue_check(sleep_queue_t* queue);
