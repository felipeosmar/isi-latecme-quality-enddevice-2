/**
 * @file alarm_sync_task.h
 * @brief Applies platform alarm limits received on fPort 11.
 */
#ifndef ALARM_SYNC_TASK_H
#define ALARM_SYNC_TASK_H

#ifdef __cplusplus
extern "C" {
#endif

/** Create the "alarm_sync" task (stack 4096, priority 3, core 0). */
void alarm_sync_task_start(void);

#ifdef __cplusplus
}
#endif

#endif
