#include "app.h"
#include "fds.h"

taskConfig taskObj[MAX_TASKS];
unScheduledEvent unSchdEvent[MAX_UNSCHEDULED_EVENTS];
deviceConfig deviceConfigObj;
uint32_t totalScheduledTasksSaved,totalUnscheduledTasksSaved;
time_t     doseTimeStamp[MAX_DOSES_PER_TASK];
extern bool notifications_service1_enable;
extern bool notifications_service2_enable;
extern bool startAdvertising;
extern bool deviceConnected;
extern bool blinkLedOnCommand;
extern uint32_t doseCounter;
extern uint8_t percentage_batt_lvl;
extern uint16_t batt_lvl_in_milli_volts;
volatile bool buzzerTimerExpired = true;
bool sendSyncResponse = false;
bool sendScheduledTasks = false;
bool sendUnscheduledTasks = false;
uint8_t sendTaskIndex = 0;

extern void saveTask(uint8_t taskId, taskConfig *taskCfg);
extern void readTask(uint8_t taskId, taskConfig *taskCfg);
extern void saveTotalTasks(uint32_t *totalTasks);
extern void saveDeviceConfig(deviceConfig *deviceCfg);
extern void eraseAllRecordsinFlash();
extern void readDeviceConfig(deviceConfig *deviceCfg);
APP_PWM_INSTANCE(PWM1,3);
APP_PWM_INSTANCE(PWM2,4);


void processReceivedData(uint8_t *data, uint8_t len)
{
  taskConfig tempTaskObj;
  timeStruct timeDate;
  uint32_t taskIndex = 0;
  char* token;
  
  NRF_LOG_INFO("ProcessData : %s with len %d \r\n", data, len);

  if(strncmp(data,"Time",4) == 0)
  {
    sscanf(data, "Time[%d-%d-%d,%d:%d:%d",&timeDate.dateTimeConfig.day, &timeDate.dateTimeConfig.month, &timeDate.dateTimeConfig.year, &timeDate.dateTimeConfig.hour, &timeDate.dateTimeConfig.minute, &timeDate.dateTimeConfig.second);
    app_timer_start(getAppTimerInstance(), LED_BLINK_INTERVAL, NULL);
    blinkLedOnCommand = true;
    nrf_gpio_pin_toggle(LED_2);
    nrf_cal_set_time(&timeDate);
    ble_cus_custom_value_update(getServiceDefinition(2), "Ack", 3);
  }
  else if(strncmp(data,"Task",4) == 0)
  {
    app_timer_start(getAppTimerInstance(), LED_BLINK_INTERVAL, NULL);
    blinkLedOnCommand = true;
    nrf_gpio_pin_toggle(LED_2);

     if(totalScheduledTasksSaved < MAX_TASKS)
      {
          token = strtok(data, "[");
          token = strtok(NULL,",");

          taskIndex = atoi(token);
          token = strtok(NULL,",");

          tempTaskObj.taskId = atoi(token);
          token = strtok(NULL,",");
      
          tempTaskObj.taskDetailsId = atoi(token);
          token = strtok(NULL,",");

          tempTaskObj.taskType = atoi(token);
          token = strtok(NULL,",");

          tempTaskObj.orderId = atoi(token);
          token = strtok(NULL,",");

          timeDate.dateTimeConfig.day = atoi(token);
          token = strtok(NULL,",");

          timeDate.dateTimeConfig.month = atoi(token);
          token = strtok(NULL,",");

          timeDate.dateTimeConfig.year = atoi(token);
          token = strtok(NULL,",");

          timeDate.dateTimeConfig.hour = atoi(token);
          token = strtok(NULL,",");

          timeDate.dateTimeConfig.minute = atoi(token);
          token = strtok(NULL,",");

          timeDate.dateTimeConfig.second = atoi(token);
          token = strtok(NULL,",");

          tempTaskObj.noOfDoses = atoi(token);
          token = strtok(NULL,",");

          tempTaskObj.taskState = atoi(token);
          token = strtok(NULL,",");

          tempTaskObj.durationBetweenDoses = atoi(token);
          token = strtok(NULL,",");

          strcpy(&tempTaskObj.medicationName[0],token);

          tempTaskObj.medicationScheduledTime = convertToTime_t(&timeDate);
          tempTaskObj.doseCount = 0;
          totalScheduledTasksSaved++;
          if(taskIndex <= MAX_TASKS)
          {
            memcpy(&taskObj[taskIndex - 1],&tempTaskObj,sizeof(tempTaskObj));
            sendStatusToApp(taskIndex - 1, TASK_ACK);
            saveTask(taskIndex, &tempTaskObj);
            saveTotalTasks(&totalScheduledTasksSaved);
          }
      }
      else
      {
        totalScheduledTasksSaved = 0;
      }
  }
  else if(strncmp(data,"Sync",4) == 0)
  {
    app_timer_start(getAppTimerInstance(), LED_BLINK_INTERVAL, NULL);
    blinkLedOnCommand = true;
    nrf_gpio_pin_toggle(LED_2);
    sendSyncResponse = true;
    if(totalScheduledTasksSaved > 0)
    {
      sendScheduledTasks = true;
    }
    else if(totalUnscheduledTasksSaved > 0)
    {
      sendUnscheduledTasks = true;
    }
    sendTaskIndex = 0;
  }
  else if(strncmp(data,"Config",6) == 0)
  {
    app_timer_start(getAppTimerInstance(), LED_BLINK_INTERVAL, NULL);
    blinkLedOnCommand = true;
    nrf_gpio_pin_toggle(LED_2);
    token = strtok(data, "[");
    token = strtok(NULL,",");

    strncpy(deviceConfigObj.serialNumber, token, 14);

    deviceConfigObj.serialNumber[14] = NULL;

    token = strtok(NULL,",");
    deviceConfigObj.deviceType = *token;

    token = strtok(NULL,",");
    if(*token == '1')
    {
      deviceConfigObj.buzzerEnable = 1;
    }
    else
    {
      deviceConfigObj.buzzerEnable = 0;
    }
    
    token = strtok(NULL,",");
    if(*token == '1')
    {
      deviceConfigObj.continuousAdvertisement = 1;
    }
    else
    {
      deviceConfigObj.continuousAdvertisement = 0;
    }

    token = strtok(NULL,",");
    deviceConfigObj.taskReminderInterval = atoi(token);

    if(deviceConfigObj.taskReminderInterval == 0)
    {
      deviceConfigObj.taskReminderInterval = DEFAULT_TASK_REMINDER_INTERVAL;
    }

    token = strtok(NULL,",");
    deviceConfigObj.doseCountLimit = atoi(token);

    if(deviceConfigObj.doseCountLimit == 0)
    {
      deviceConfigObj.doseCountLimit = DEFAULT_DOSECOUNT_LIMIT;
    }

    saveDeviceConfig(&deviceConfigObj);
    sendDeviceConfigToApp(&deviceConfigObj);
    NRF_LOG_INFO("\r\n Device config %s, %c, %d, %d, %d, %d\r\n",deviceConfigObj.serialNumber,deviceConfigObj.deviceType,deviceConfigObj.buzzerEnable,deviceConfigObj.continuousAdvertisement,deviceConfigObj.taskReminderInterval,deviceConfigObj.doseCountLimit);
  }
  else if(strncmp(data,"Add",3) == 0)
  {
    app_timer_start(getAppTimerInstance(), LED_BLINK_INTERVAL, NULL);
    blinkLedOnCommand = true;
    nrf_gpio_pin_toggle(LED_2);
    doseCounter = 0;
    sendStatusToApp(0,DOSE_COUNT);
  }
  else if(strncmp(data,"Dinfo",5) == 0)
  {
    app_timer_start(getAppTimerInstance(), LED_BLINK_INTERVAL, NULL);
    blinkLedOnCommand = true;
    nrf_gpio_pin_toggle(LED_2);
    sendDeviceConfigToApp(&deviceConfigObj);
  }
  else if(strncmp(data,"Erase",5) == 0)
  {
    app_timer_start(getAppTimerInstance(), LED_BLINK_INTERVAL, NULL);
    blinkLedOnCommand = true;
    nrf_gpio_pin_toggle(LED_2);
    eraseAllRecordsinFlash();
    memset(&taskObj[0],0,sizeof(taskObj));
    memset(&unSchdEvent[0],0,sizeof(unSchdEvent)); 
    totalScheduledTasksSaved = 0;
    totalUnscheduledTasksSaved = 0;
    readDeviceConfig(&deviceConfigObj);
    ble_cus_custom_value_update(getServiceDefinition(2), "Ack", 3);
  }
}


void checkForAlert(void)
{
  static time_t lastTime;
  double diff;
  static bool startBuzzerLoop=false;
  static uint8_t beforeReminderDoseCount=0;
  static bool reminding = false;

  time_t currentTime;
  currentTime = getCurrentTime_t();

  for(uint8_t taskIndex = 0; taskIndex < MAX_TASKS; taskIndex++)
  {
    if(taskObj[taskIndex].taskState == PENDING)
    {
      if(currentTime == taskObj[taskIndex].medicationScheduledTime)
      {
        taskObj[taskIndex].taskState = IN_PROGRESS;
        lastTime = getCurrentTime_t();
        // start buzzer
        if(deviceConfigObj.buzzerEnable == 1)
        {
          NRF_LOG_INFO("Start Buzzer currentTime %ld lastTime %ld\r\n", lastTime, currentTime);
          app_pwm_enable(&PWM1);
          app_timer_start(getAppTimerInstance(), BUZZER_BEEP_INTERVAL, NULL);
          buzzerTimerExpired = true;
          startBuzzerLoop = true;
          setDutyCycleBuzzer(90);
          beforeReminderDoseCount = 0;
        }
        if(!startAdvertising && !deviceConnected)
        {
          NRF_LOG_INFO("Device started advertising due to task scheduled");
          startAdvertising = true;
          advertising_start(false);
        }
      }
    }

    if(taskObj[taskIndex].taskState == IN_PROGRESS)
    {
      if(startBuzzerLoop)
      {
        diff = difftime(lastTime,currentTime);
    
        setBeepForBuzzer();
        // stop buzzer based on timeout of 30 seconds or any dose taken whatever happens first
        if(diff >= BUZZER_ON_INTERVAL)
        {
            startBuzzerLoop = false;          
            // stop buzzer and notify that task is skipped
            NRF_LOG_INFO("Stop Buzzer due to Timeout at %ld \r\n", diff);
            stopBuzzer();
            if(taskObj[taskIndex].doseCount < 1)
            {
              if(!reminding)
              {
                NRF_LOG_INFO("Task skipped because of no dose \r\n");
                taskObj[taskIndex].taskState = SKIPPED_REMINDER_PENDING;
                lastTime = getCurrentTime_t();
                saveTask(taskIndex + 1, &taskObj[taskIndex]);
              }
              else
              {
                reminding = false;
                NRF_LOG_INFO("Task skipped even tough reminding once \r\n");
                taskObj[taskIndex].taskState = SKIPPED;
                saveTask(taskIndex + 1, &taskObj[taskIndex]);
              }
            }
            else
            {
              if(reminding)
              {
                reminding = false;
                NRF_LOG_INFO("Task not completed even after reminding once \r\n");
                taskObj[taskIndex].taskState = INCOMPLETE;
                beforeReminderDoseCount = 0;
                saveTask(taskIndex + 1, &taskObj[taskIndex]);
              }
            }
        }

        // stop buzzer if dose has been taken
        if(taskObj[taskIndex].noOfDoses > 1)
        {
           if(taskObj[taskIndex].doseCount >= 1)
           {
              // stop buzzer
              if(beforeReminderDoseCount != taskObj[taskIndex].doseCount)
              {
                NRF_LOG_INFO("Stop Buzzer due to dose taken in progress \r\n");
                startBuzzerLoop = false;
                stopBuzzer();
              }
           }
        }         
      }
     
      if(taskObj[taskIndex].noOfDoses > 1)
      {
         if(taskObj[taskIndex].doseCount >= 1)
         {
           if(beforeReminderDoseCount != taskObj[taskIndex].doseCount)
           {
             diff = difftime(doseTimeStamp[taskObj[taskIndex].doseCount-1],getCurrentTime_t());
             if(diff >= taskObj[taskIndex].durationBetweenDoses)
             {
                if(reminding)
                {
                  reminding = false;
                  taskObj[taskIndex].taskState = INCOMPLETE;
                }
                else
                {
                  lastTime = getCurrentTime_t();
                  taskObj[taskIndex].taskState = INCOMPLETE_REMINDER_PENDING;
                }                
                saveTask(taskIndex + 1, &taskObj[taskIndex]);
                NRF_LOG_INFO("Task not completed %d \r\n",taskObj[taskIndex].taskState);              
             }
           }
         }
      } 
      break;
    }

    if(taskObj[taskIndex].taskState == SKIPPED_REMINDER_PENDING || taskObj[taskIndex].taskState == INCOMPLETE_REMINDER_PENDING)
    {
      // check for 10 minutes reminder
      if(!reminding)
      {
        diff = difftime(lastTime,currentTime);
        if(diff >= deviceConfigObj.taskReminderInterval)
        {
          if(deviceConfigObj.buzzerEnable == 1)
          {
            NRF_LOG_INFO("Task reminder started \r\n");
            reminding = true;
            lastTime = getCurrentTime_t();
            app_pwm_enable(&PWM1);
            app_timer_start(getAppTimerInstance(), BUZZER_BEEP_INTERVAL, NULL);
            buzzerTimerExpired = true;
            startBuzzerLoop = true;
            setDutyCycleBuzzer(90);
            beforeReminderDoseCount = taskObj[taskIndex].doseCount;
          }
          taskObj[taskIndex].taskState = IN_PROGRESS;
        }
      }
    }

    if(taskObj[taskIndex].taskState == COMPLETED)
    {
       if(reminding)
       {
         reminding = false;
       }
    }
  }
}


void pwm_init()
{
    ret_code_t err_code;

    /* 1-channel PWM, 4000Hz, output on LED1 pin number 17 */
    app_pwm_config_t pwm1_cfg = APP_PWM_DEFAULT_CONFIG_1CH(250L,  LED_1);

    err_code = app_pwm_init(&PWM1,&pwm1_cfg,NULL);
    APP_ERROR_CHECK(err_code);
}

void setDutyCycleBuzzer(uint8_t dutyCycle)
{
  while (app_pwm_channel_duty_set(&PWM1, 0, dutyCycle) == NRF_ERROR_BUSY);
}

void enablePWMForBuzzer()
{
  app_pwm_enable(&PWM1);
}

void disablePWMForBuzzer()
{
  app_pwm_disable(&PWM1);
}

void setDutyCycleLED(uint8_t dutyCycle)
{
  while (app_pwm_channel_duty_set(&PWM2, 0, dutyCycle) == NRF_ERROR_BUSY);
}

ret_code_t sendStatusToApp(uint8_t taskIndex, uint8_t sendStatus)
{
    uint8_t statusToApp[60];
    ret_code_t errorCode;
    struct tm *time;
    
    if(notifications_service1_enable && notifications_service2_enable)
    {
      if(sendStatus == TASK_SYNC_SCHEDULED)
      {
        if(taskObj[taskIndex].taskState == COMPLETED)
        {
          time = localtime(&taskObj[taskIndex].taskCompletionTime);
        }
        else
        {
          time = nrf_cal_get_time();
        }

        sprintf(statusToApp,"RTask[%d,%d,%d,%d,%d-%d-%d,%d:%d:%d,%d,%d,%d]",taskObj[taskIndex].taskId,taskObj[taskIndex].taskDetailsId, \
          taskObj[taskIndex].taskType,taskObj[taskIndex].orderId,time->tm_mday,time->tm_mon + 1,time->tm_year + 1900,\
          time->tm_hour,time->tm_min,time->tm_sec,taskObj[taskIndex].taskState,taskObj[taskIndex].noOfDoses,taskObj[taskIndex].durationBetweenDoses);
      }
      else if(sendStatus == TASK_ACK)
      {
        sprintf(statusToApp, "Ack[%d]", taskObj[taskIndex].taskId);
      }
      else if(sendStatus == UNSCHEDULED_EVENT)
      {
        time = nrf_cal_get_time();
        sprintf(statusToApp, "Unschd[%d-%d-%d,%d:%d:%d]", time->tm_mday,time->tm_mon + 1,time->tm_year + 1900,time->tm_hour,time->tm_min,time->tm_sec);
      }
      else if(sendStatus == BATTERY_LEVEL)
      {
        sprintf(statusToApp, "Battery[%d]", percentage_batt_lvl);
      }
      else if(sendStatus == TASK_SYNC_UNSCHEDULED)
      {
        time = localtime(&unSchdEvent[taskIndex].unScheduledEventTimeStamp);
        sprintf(statusToApp, "Unschd[%ld,%d-%d-%d,%d:%d:%d]", unSchdEvent[taskIndex].id, time->tm_mday,time->tm_mon + 1,time->tm_year + 1900,time->tm_hour,time->tm_min,time->tm_sec);
      }
      else if(sendStatus == DOSE_COUNT)
      {
        sprintf(statusToApp, "DoseCount[%ld]", doseCounter);
      }
      else
      {}
      NRF_LOG_INFO("Reply: %s",statusToApp);
      uint8_t len = strlen(statusToApp);
      if(len >= 30)
      {
        errorCode = ble_cus_custom_value_update(getServiceDefinition(2), &statusToApp[0], 30);
        errorCode = ble_cus_custom_value_update(getServiceDefinition(2), &statusToApp[30], strlen(statusToApp) - 30);
      }
      else
      {
        errorCode = ble_cus_custom_value_update(getServiceDefinition(2), &statusToApp[0], strlen(statusToApp));
      }
    }
    return errorCode;
}

void sendDeviceConfigToApp(deviceConfig *deviceCfg)
{
  uint16_t tempDoseCount;
  fds_stat_t stat = {0};
  if(notifications_service2_enable)
  {
    uint8_t configToApp[60];
    fds_stat(&stat);
    tempDoseCount = deviceCfg->doseCountLimit - totalUnscheduledTasksSaved - totalScheduledTasksSaved - doseCounter;
    sprintf(configToApp,"Rcfg[%s,%c,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%s]", deviceCfg->serialNumber,deviceCfg->deviceType,deviceCfg->buzzerEnable,deviceCfg->continuousAdvertisement, deviceCfg->taskReminderInterval, tempDoseCount, deviceCfg->doseCountLimit, percentage_batt_lvl, batt_lvl_in_milli_volts,stat.words_used, stat.dirty_records ,MAX_WORDS_IN_FLASH,FIRMWARE_VERSION);
    NRF_LOG_INFO("Config sent: %s",configToApp);
    uint8_t len = strlen(configToApp);
    if(len >= 30)
    {
      ble_cus_custom_value_update(getServiceDefinition(2), &configToApp[0], 30);
      ble_cus_custom_value_update(getServiceDefinition(2), &configToApp[30], strlen(configToApp) - 30);
    }
    else
    {
      ble_cus_custom_value_update(getServiceDefinition(2), &configToApp[0], strlen(configToApp));
    }
  }
}

void setBeepForBuzzer()
{ 
  if(buzzerTimerExpired)
  {
    buzzerTimerExpired = false;
    app_timer_stop(getAppTimerInstance());
    app_timer_start(getAppTimerInstance(), BUZZER_BEEP_INTERVAL, NULL);
    beepBuzzer();
  }
}

void sendSyncResponseForAllTasks()
{
  if(sendSyncResponse)
  {
    if(sendScheduledTasks)
    {
      if(sendTaskIndex < totalScheduledTasksSaved)
      {
          if(taskObj[sendTaskIndex].taskState != NONE && taskObj[sendTaskIndex].taskId != 0)
          {
            if(sendStatusToApp(sendTaskIndex, TASK_SYNC_SCHEDULED) == NRF_SUCCESS)
            {
              sendTaskIndex++;
            }
          }
      }
      else
      {
        sendTaskIndex = 0;
        sendScheduledTasks = false;
        sendUnscheduledTasks = true;
        NRF_LOG_INFO("Sync response scheduled tasks complete \r\n");
      }
    }
    else if(sendUnscheduledTasks)
    {
      if(sendTaskIndex < totalUnscheduledTasksSaved)
      {
        if(sendStatusToApp(sendTaskIndex, TASK_SYNC_UNSCHEDULED) == NRF_SUCCESS)
        {
          sendTaskIndex++;
        }
      }
      else
      {
        sendTaskIndex = 0;
        sendScheduledTasks = false;
        sendUnscheduledTasks = false;
        sendSyncResponse = false;
        NRF_LOG_INFO("Sync response unScheduled tasks complete \r\n");
        // send dose counter value
        //sendStatusToApp(0, DOSE_COUNT);
      }
    }
  }
}

void stopBuzzer()
{
    app_timer_stop(getAppTimerInstance());
    buzzerTimerExpired = true;
    setDutyCycleBuzzer(0);
    app_pwm_disable(&PWM1);
    nrf_gpio_pin_write(LED_1, 0);
}

void beepBuzzer()
{
    setDutyCycleBuzzer(60);
    nrf_delay_ms(90);
    setDutyCycleBuzzer(100);
    nrf_delay_ms(200);
    setDutyCycleBuzzer(60);
    nrf_delay_ms(90);
    setDutyCycleBuzzer(100);
}