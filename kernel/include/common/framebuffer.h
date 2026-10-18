#pragma once

/**
 * @brief Initializes the framebuffer for logging output.
 */
void framebuffer_init();

/**
 * @brief prints @p to every framebuffer
 */
void framebuffer_put_char(char c);

/**
 * @brief Drains pending write-combining stores out to the framebuffer memory.
 * @note this should be called once per batch of output (a whole log message, a tty write, etc) rather than per character.
 */
void framebuffer_flush(void);

/**
 * @brief Enables or disables logging to the framebuffer.
 * @param enable true to enable logging to the framebuffer, false to disable
 */
void framebuffer_enable(bool enable);
