#ifndef FLASH_H__
#define FLASH_H__


#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "nrf_nvmc.h"
#include "nrf_log.h"
#include "nrf_fstorage_sd.h"
#include "fds.h"
#include "app.h"

#define CONFIG_FILE                               (0x8010)
#define TOTAL_TASKS_RECORD_KEY                    (0x7010)
#define DEVICE_CONFIG_RECORD_KEY                  (0x6010)
#define UNSCHEDULED_EVENT_KEY                     (0x5010)
#define TOTAL_UNSCHEDULED_EVENTS_KEY              (0x4010)

//#define TOTAL_NO_UNSCHEDULED                      (0x1010)
//#define TOTAL_NO_SCHEDULED                        (0x2010)

#define UNSCHEDULED_TASK_KEY_OFFSET               1000

void wait_for_flash_ready(nrf_fstorage_t const * p_fstorage);
void flash_page_init();
uint32_t nrf5_flash_end_addr_get();
void readTask(uint8_t taskId, taskConfig *taskCfg);
void saveTask(uint8_t taskId, taskConfig *taskCfg);
void saveTotalTasks(uint32_t *totalTasks);
void saveDeviceConfig(deviceConfig *deviceCfg);
void readDeviceConfig(deviceConfig *deviceCfg);
void readAllTasks();
void saveUnscheduledTask(unScheduledEvent *unSchdEvt);
void readAllUnscheduledTasks();
void readUnscheduledTask(uint32_t taskId, unScheduledEvent *unSchdEvt);
void saveTotalUnscheduledTasks(uint32_t *total);
void eraseAllRecordsinFlash();

#endif // FLASH_H__