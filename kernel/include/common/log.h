#pragma once
#include <common/interrupts/interrupt.h>
#include <stdarg.h>
#include <stddef.h>

typedef enum {
    LOG_LEVEL_STRC = 0,
    LOG_LEVEL_DBGL,
    LOG_LEVEL_INFO,
    LOG_LEVEL_OKAY,
    LOG_LEVEL_WARN,
    LOG_LEVEL_FAIL,
} log_level_t;

typedef void (*log_sink_func_t)(int c, void* ctx);

typedef struct log_sink {
    log_level_t min_level;
    log_sink_func_t write;
    void* ctx;
    bool is_framebuffer;
    size_t framebuffer_index;
} log_sink_t;

/**
 * @brief Registers a new log sink dynamically.
 * @return true if the sink was added successfully, false otherwise
 */
[[nodiscard]] bool log_add_sink(const log_sink_t* sink);

/**
 * @brief Initializes the logging system.
 */
void log_init();

/**
 * @brief Initializes the framebuffer for logging output.
 */
void log_framebuffer_init();

/**
 * @brief Enables or disables logging to the framebuffer.
 * @param enable true to enable logging to the framebuffer, false to disable
 */
void log_framebuffer_enable(bool enable);

/**
 * @brief Reinitializes the framebuffer for logging output.
 */
void log_framebuffer_reinit();

void log_vprint_lockless(log_level_t log, const char* fmt, va_list val);
[[gnu::format(printf, 2, 3)]] void log_print_lockless(log_level_t log, const char* fmt, ...);

void log_vprint(log_level_t log, const char* fmt, va_list val);
[[gnu::format(printf, 2, 3)]] void log_print(log_level_t log, const char* fmt, ...);

/**
 * @brief Acquire/release the global log lock manually.
 * @return The previous interrupt state before acquiring the lock
 */
arch_interrupt_state_t log_lock_acquire(void);

/**
 * @brief Release the global log lock and restore the previous interrupt state.
 * @param state The previous interrupt state to restore
 */
void log_lock_release(arch_interrupt_state_t state);

#define LOG_COLORIZE(text, color) "\x1b[1m\x1b[38;5;" color "m" text "\x1b[0m"

#define LOG_FAIL(fmt, ...)                                                                           \
    do {                                                                                             \
        log_print(LOG_LEVEL_FAIL, LOG_COLORIZE("fail | ", "9") "%s: " fmt, __func__, ##__VA_ARGS__); \
    } while(0)

#define LOG_WARN(fmt, ...)                                                                            \
    do {                                                                                              \
        log_print(LOG_LEVEL_WARN, LOG_COLORIZE("warn | ", "11") "%s: " fmt, __func__, ##__VA_ARGS__); \
    } while(0)

#define LOG_OKAY(fmt, ...)                                                                            \
    do {                                                                                              \
        log_print(LOG_LEVEL_OKAY, LOG_COLORIZE("okay | ", "10") "%s: " fmt, __func__, ##__VA_ARGS__); \
    } while(0)

#define LOG_INFO(fmt, ...)                                                                            \
    do {                                                                                              \
        log_print(LOG_LEVEL_INFO, LOG_COLORIZE("info | ", "14") "%s: " fmt, __func__, ##__VA_ARGS__); \
    } while(0)

#if defined(__DEBUG__)

#define LOG_DBGL(fmt, ...)                                                                           \
    do {                                                                                             \
        log_print(LOG_LEVEL_DBGL, LOG_COLORIZE("dbgl | ", "4") "%s: " fmt, __func__, ##__VA_ARGS__); \
    } while(0)

// Kernel TRaCe
#define LOG_KTRC(fmt, ...)                                                                            \
    do {                                                                                              \
        log_print(LOG_LEVEL_STRC, LOG_COLORIZE("ktrc | ", "13") "%s: " fmt, __func__, ##__VA_ARGS__); \
    } while(0)

// User TRaCe
#define LOG_UTRC(fmt, ...)                                                                             \
    do {                                                                                               \
        log_print(LOG_LEVEL_STRC, LOG_COLORIZE("utrc | ", "208") "%s: " fmt, __func__, ##__VA_ARGS__); \
    } while(0)

#else

#define LOG_DBGL(fmt, ...)
#define LOG_KTRC(fmt, ...)

#endif
