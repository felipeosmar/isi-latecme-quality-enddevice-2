/**
 * @file boot_status.h
 * @brief Boot step list: logs each step and draws it on the OLED
 *
 * Steps are shown as "ok"/".."/"ER" lines with an optional detail line.
 * Only the last 7 lines are drawn. After boot_status_finish() steps are
 * only logged, so other screens can take over the display.
 */

#ifndef BOOT_STATUS_H
#define BOOT_STATUS_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BOOT_STEP_RUNNING = 0,  // ".."
    BOOT_STEP_OK,           // "ok"
    BOOT_STEP_ERROR,        // "ER"
} boot_step_state_t;

/** @brief Create the mutex and set the header (version string). */
void boot_status_init(void);

/**
 * @brief Start a step (state RUNNING).
 * @param name   Step name (truncated to 18 chars)
 * @param detail Optional detail (truncated to 18 chars), may be NULL
 * @return Step id (>= 0), or -1 if the list is full / not initialized
 */
int boot_status_begin(const char *name, const char *detail);

/**
 * @brief Finish a step with a final state.
 * @param detail New detail text, or NULL to keep the current one
 */
void boot_status_end(int step, boot_step_state_t state, const char *detail);

/** @brief Stop drawing; later begin/end calls only log. */
void boot_status_finish(void);

bool boot_status_is_finished(void);

#ifdef __cplusplus
}
#endif

#endif /* BOOT_STATUS_H */
