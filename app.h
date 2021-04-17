#ifndef APP_H__
#define APP_H__

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "boards.h"
#include "nrf_log.h"
#include "time.h"
#include "calender.h"
#include "app_pwm.h"
#include "ble_custom.h"
#include "app_timer.h"
#include "stdlib.h"
#include "nrf_drv_rtc.h"
#include "nrf_delay.h"


APP_TIMER_DEF(m_notification_timer_id);

#define FIRMWARE_VERSION              "1.0"

#define MAX_WORDS_IN_FLASH            2000

#define MAX_MEDICATION_NAME_LEN       10

#define MAX_TASKS                     50

#define BUZZER_ON_INTERVAL            30

#define MAX_DOSES_PER_TASK            10

#define BUZZER_BEEP_TIME              1

#define BUZZER_BEEP_INTERVAL          APP_TIMER_TICKS(BUZZER_BEEP_TIME*700)

#define LED_BLINK_INTERVAL            APP_TIMER_TICKS(250)

#define COMPANY_ID                    1

#define SAMPLES_IN_BUFFER             1

#define BATTERY_LOW_BEEP_INTERVAL     10

#define DOSELIMIT_REMINDER_INTERVAL    10

#define MAX_UNSCHEDULED_EVENTS        100

#define DEFAULT_TASK_REMINDER_INTERVAL        120

#define DEFAULT_DOSECOUNT_LIMIT        200


#define ADC_REF_VOLTAGE_IN_MILLIVOLTS   600                                     /**< Reference voltage (in milli volts) used by ADC while doing conversion. */
#define ADC_PRE_SCALING_COMPENSATION    6                                       /**< The ADC is configured to use VDD with 1/3 prescaling as input. And hence the result of conversion is to be multiplied by 3 to get the actual value of the battery voltage.*/
#define DIODE_FWD_VOLT_DROP_MILLIVOLTS  270                                     /**< Typical forward voltage drop of the diode . */
#define ADC_RES_10BIT                   1024                                    /**< Maximum digital value for 10-bit ADC conversion. */


#define ADC_RESULT_IN_MILLI_VOLTS(ADC_VALUE)\
        ((((ADC_VALUE) * ADC_REF_VOLTAGE_IN_MILLIVOLTS) / ADC_RES_10BIT) * ADC_PRE_SCALING_COMPENSATION)


typedef enum 
{
  NONE = 0,
  PENDING,
  COMPLETED,
  SKIPPED_REMINDER_PENDING,
  INCOMPLETE_REMINDER_PENDING,
  IN_PROGRESS,
  SKIPPED,
  INCOMPLETE,
}taskStatus;


typedef struct
{
  uint16_t            taskId;
  uint16_t            taskDetailsId;
  uint8_t             taskType;
  uint16_t            orderId;
  uint8_t             medicationName[MAX_MEDICATION_NAME_LEN];
  time_t              medicationScheduledTime;
  taskStatus          taskState;
  uint8_t             noOfDoses;
  uint8_t             durationBetweenDoses;
  uint8_t             doseCount;
  double              diffBetweenDoses[MAX_DOSES_PER_TASK];
  time_t              taskCompletionTime;  
}taskConfig;

typedef struct
{
  uint32_t            id;
  time_t             unScheduledEventTimeStamp;
}unScheduledEvent;

typedef struct
{
 char serialNumber[15];
 char deviceType;
 uint8_t buzzerEnable;
 uint8_t continuousAdvertisement;
 uint16_t taskReminderInterval;
 uint16_t doseCountLimit;
}deviceConfig;

enum eventReply
{
  TASK_ACK=1,
  TASK_SYNC_SCHEDULED,
  TASK_SYNC_UNSCHEDULED,
  UNSCHEDULED_EVENT,
  BATTERY_LEVEL,
  DOSE_COUNT
};

void processReceivedData(uint8_t *data, uint8_t len);

void checkForAlert(void);

void pwm_init();

void setDutyCycleBuzzer(uint8_t dutyCycle);

void setDutyCycleLED(uint8_t dutyCycle);

ret_code_t sendStatusToApp(uint8_t taskIndex, uint8_t sendStatus);

extern ble_cus_t *getServiceDefinition(uint8_t id);

void setBeepForBuzzer();

void notification_timeout_handler(void * p_context);

void triggerADCSampling(void);

extern app_timer_t *getAppTimerInstance();

void readTask(uint8_t taskId, taskConfig *taskCfg);

void saveTask(uint8_t taskId, taskConfig *taskCfg);

void sendDeviceConfigToApp(deviceConfig *deviceCfg);

void sendSyncResponseForAllTasks();

void stopBuzzer();

void advertising_start(bool erase_bonds);

void checkForLowBattery();

void checkForEmptyDose();

void enablePWMForBuzzer();

void disablePWMForBuzzer();

void eraseAllFlashMemory();

void beepBuzzer();

#endif