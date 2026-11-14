#pragma once

#include <lib/helpers.h>
#include <lib/list.h>
#include <stddef.h>
#include <stdint.h>

#include "lib/atomic_stack.h"

typedef enum {
    IPI_TLB_FLUSH = 0,
    IPI_TLB_FLUSH_ALL,
} ipi_type_t;

typedef struct {
    ipi_type_t type;

    union {
        struct {
            uintptr_t vaddr;
            size_t length;
        } tlb_flush;
    };
} ipi_message_t;

typedef struct ipi_request {
    ipi_message_t message;

    // Either in the ipi stack, or the pending free stack
    atomic_stack_node_t node;
} ipi_request_t;

/**
 * @brief Broadcasts a control IPI message to all cores except the sender.
 * @param message The IPI message to broadcast.
 */
void ipi_broadcast(ipi_message_t message);

/**
 * @brief Sends a control IPI message to a specific core.
 * @param core_id The ID of the core to send the message to.
 * @param message The IPI message to send.
 */
void ipi_send(uint32_t core_id, ipi_message_t message);

/**
 * @brief Sends the dedicated panic IPI to a specific core.
 * @param core_id The ID of the core to stop.
 * @note Unlike the control IPI path this does not allocate or take any locks.
 */
void ipi_send_panic(uint32_t core_id);

/**
 * @brief Sends the dedicated panic IPI to every other online core.
 */
void ipi_broadcast_panic(void);

/**
 * @brief Handles all incoming control IPI messages.
 */
void ipi_handle();

/**
 * @brief Handles a panic IPI by shutting the current core down.
 * @note This never returns, the core must be brought up again
 */
[[noreturn]] void ipi_handle_panic(void);

/**
 * @brief Reclaims any ipis sitting in the free ipi list
 */
void ipi_reclaim();


/**
 * @brief Initializes the IPI system for the current core.
 */
void ipi_init(uint32_t core_id);
