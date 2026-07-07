#pragma once

#include <lib/helpers.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    IPI_TLB_FLUSH = 0,
    IPI_HALT,
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
    ATOMIC struct ipi_request* next;
} ipi_request_t;

/**
 * @brief Broadcasts an IPI message to all cores except the sender.
 * @param message The IPI message to broadcast.
 */
void ipi_broadcast(ipi_message_t message);

/**
 * @brief Sends an IPI message to a specific core.
 * @param core_id The ID of the core to send the message to.
 * @param message The IPI message to send.
 */
void ipi_send(uint32_t core_id, ipi_message_t message);

/**
 * @brief Pops an IPI message from the queue.
 * @param message Pointer to an ipi_message_t structure to fill with the popped message.
 * @return if there is a message, returns true and fills the message, otherwise returns false
 */
bool ipi_pop(ipi_message_t* message);

/**
 * @brief Handles an incoming IPI message.
 * @param message The IPI message to handle.
 */
void ipi_handle(ipi_message_t message);

/**
 * @brief Initializes the IPI system for the current core.
 */
void ipi_init(uint32_t core_id);
