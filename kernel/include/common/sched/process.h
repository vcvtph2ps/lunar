#pragma once
#include <common/ldr/ldr.h>
#include <common/sync/spinlock.h>
#include <lib/list.h>
#include <stdint.h>

typedef struct process process_t;
typedef struct thread thread_t; // NOLINT

struct process {
    uint32_t process_id;

    spinlock_no_dw_t thread_list_lock;
    list_t thread_list;
};

/**
 * @brief Allocates a new process ID.
 * @return A unique process ID.
 */
uint32_t process_allocate_id();

/**
 * @brief Frees a previously allocated process ID.
 * @param id The process ID to free.
 */
void process_free_id(uint32_t id);

/**
 * @brief Creates a process from the file and arguments
 */
process_t* process_create_from_file(const vfs_path_t* path, const ldr_process_load_info_t* load_info, thread_t** out_thread);
