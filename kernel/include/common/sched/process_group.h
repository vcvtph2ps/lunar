#pragma once
#include <common/sched/process.h>
#include <lib/list.h>
#include <lib/rb.h>
#include <stddef.h>
#include <stdint.h>

struct process_group {
    /// Process group ID. Equal to the process ID of the group's leader.
    uint32_t pgid;

    ATOMIC uint32_t ref_count;
    rb_node_t rb_node;
    list_t members;
};

/**
 * @brief Looks up a process group by its ID.
 * @return A referenced group, or nullptr
 * @note The caller owns the returned reference and must release it with process_group_deref()
 */
process_group_t* process_group_lookup(uint32_t pgid);

/**
 * @brief Looks up a process group by ID, creating it if it does not exist.
 * @return A referenced group.
 * @note The caller owns the returned reference and must release it with process_group_deref()
 */
process_group_t* process_group_get_or_create(uint32_t pgid);

/**
 * @brief Releases a reference to a process group, destroying it once the last reference is dropped.
 * @param group The group to dereference
 */
void process_group_deref(process_group_t* group);

/**
 * @brief Moves a process into a process group, creating the group if necessary.
 * @return true on success, false if the process or group could not be created
 * @note A process already in a group is first removed from it
 */
bool process_group_join(process_t* process, uint32_t pgid);

/**
 * @brief Removes a process from its current process group, if any.
 * @param process The process to remove
 * @note The group is destroyed when its last member leaves
 */
void process_group_leave(process_t* process);

/**
 * @brief Returns the process group ID of a process.
 * @param process The process to query
 * @return The process' pgid, or 0 if it does not belong to a group
 */
uint32_t process_group_id(process_t* process);

/**
 * @brief Finds every process that belongs to a process group.
 * @param out_count Set to the number of processes returned
 * @return A heap allocated array holding a reference to each process in the group, or nullptr if the
 *         group does not exist. The caller owns a reference to every returned process and must release
 *         them with process_deref(), then release the array with heap_free(array, count * sizeof(process_t*))
 */
process_t** process_group_find_processes(uint32_t pgid, size_t* out_count);
