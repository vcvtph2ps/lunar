#pragma once

/**
 * @brief Disable deferred work. (increments the status counter).
 */
void dw_status_disable();

/**
 * @brief Enable deferred work. (decrements the status counter).
 * @note Deferred work will only be processed when the status reaches zero
 */
void dw_status_enable();
