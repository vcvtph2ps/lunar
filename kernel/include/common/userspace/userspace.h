#pragma once
#include <common/assert.h>
#include <common/cpu_local.h>
#include <common/log.h>

// NOLINTBEGIN
#define user_assert(expr)                                                                                                                                                        \
    do {                                                                                                                                                                         \
        if(EXPECT_UNLIKELY(!(expr))) {                                                                                                                                           \
            LOG_FAIL("Userspace Assertion failed in process %d: %s, file %s, line %d\n", CPU_LOCAL_GET_CURRENT_THREAD()->common.process->process_id, #expr, __FILE__, __LINE__); \
            process_kill(CPU_LOCAL_GET_CURRENT_THREAD()->common.process);                                                                                                        \
        }                                                                                                                                                                        \
    } while(0)
// NOLINTEND
