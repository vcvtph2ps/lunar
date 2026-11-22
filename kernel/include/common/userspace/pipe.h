#pragma once
#include <common/userspace/fd_store.h>
#include <uapi/limits.h>

#define PIPE_CAPACITY (UAPI_LIMITS_PIPE_BUF * 16)

typedef struct pipe pipe_t;

/**
 * @brief Creates a new pipe
 * @return The new pipe, with one reader and one writer end open, or nullptr on allocation failure
 */
pipe_t* pipe_create();

/// @brief The backend operations of a pipe file descriptor
extern const fd_store_file_ops_t g_pipe_file_ops;
