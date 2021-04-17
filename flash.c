#include "flash.h"


//flashwrite_data m_dataflash;
static void fstorage_evt_handler(nrf_fstorage_evt_t * p_evt);

static bool volatile m_fds_initialized,m_write_completed,m_update_completed,m_gc_completed;
extern taskConfig taskObj[MAX_TASKS];
extern unScheduledEvent unSchdEvent[MAX_UNSCHEDULED_EVENTS];
extern uint32_t totalUnscheduledTasksSaved,totalScheduledTasksSaved;

/* Keep track of the progress of a delete_all operation. */
static struct
{
    bool delete_next;   //!< Delete next record.
    bool pending;       //!< Waiting for an fds FDS_EVT_DEL_RECORD event, to delete the next record.
} m_delete_all;

/**@brief   Wait for fds to initialize. */
static void wait_for_fds_ready(void)
{
    while (!m_fds_initialized)
    {
         (void) sd_app_evt_wait();
    }
}

static void fds_evt_handler(fds_evt_t const * p_evt)
{
    if (p_evt->result == NRF_SUCCESS)
    {
        NRF_LOG_INFO("Event: %d received (NRF_SUCCESS)",p_evt->id);
    }
    else
    {
        NRF_LOG_INFO("Event: %d received (NRF_SUCCESS)",p_evt->id,p_evt->result);
    }

    switch (p_evt->id)
    { 
        case FDS_EVT_INIT:
            if (p_evt->result == NRF_SUCCESS)
            {
                m_fds_initialized = true;
            }
            break;

        case FDS_EVT_WRITE:
        {
            if (p_evt->result == NRF_SUCCESS)
            {
                NRF_LOG_INFO("Record ID:\t0x%04x",  p_evt->write.record_id);
                NRF_LOG_INFO("File ID:\t0x%04x",    p_evt->write.file_id);
                NRF_LOG_INFO("Record key:\t0x%04x", p_evt->write.record_key);
                m_write_completed = true;
            }
        } break;

        case FDS_EVT_DEL_RECORD:
        {
            if (p_evt->result == NRF_SUCCESS)
            {
                NRF_LOG_INFO("Record ID:\t0x%04x",  p_evt->del.record_id);
                NRF_LOG_INFO("File ID:\t0x%04x",    p_evt->del.file_id);
                NRF_LOG_INFO("Record key:\t0x%04x", p_evt->del.record_key);
            }
            m_delete_all.pending = false;
        } break;

        case FDS_EVT_UPDATE:
        {
            if (p_evt->result == NRF_SUCCESS)
            {
                NRF_LOG_INFO("Record ID:\t0x%04x",  p_evt->write.record_id);
                NRF_LOG_INFO("File ID:\t0x%04x",    p_evt->write.file_id);
                NRF_LOG_INFO("Record key:\t0x%04x", p_evt->write.record_key);
                m_update_completed = true;
            }
        } break;

        case FDS_EVT_GC:
        {
            if (p_evt->result == NRF_SUCCESS)
            {
                NRF_LOG_INFO("Garbage records flash reclaimed");
                m_gc_completed = true;
            }
        } break;

        default:
            break;
    }
}

void flash_page_init(void)
{
   nrf_fstorage_api_t * p_fs_api;
    
   (void) fds_register(fds_evt_handler);

    ret_code_t rc;
    rc = fds_init();
    APP_ERROR_CHECK(rc);

    wait_for_fds_ready();

    fds_stat_t stat = {0};
    rc = fds_stat(&stat);
    APP_ERROR_CHECK(rc);
}


uint32_t nrf5_flash_end_addr_get()
{
    uint32_t const bootloader_addr = BOOTLOADER_ADDRESS;
    uint32_t const page_sz         = NRF_FICR->CODEPAGESIZE;
    uint32_t const code_sz         = NRF_FICR->CODESIZE;

    return (bootloader_addr != 0xFFFFFFFF ?
            bootloader_addr : (code_sz * page_sz));
}


void saveTask(uint8_t taskId, taskConfig *taskCfg)
{
  ret_code_t rc;
  fds_record_t record;
  fds_record_desc_t desc = {0};
  fds_find_token_t  tok  = {0};
  record.file_id = CONFIG_FILE;
  record.key = taskId;
  record.data.p_data = taskCfg;
  record.data.length_words = sizeof(taskConfig) / sizeof(uint32_t);
  m_update_completed = false;
  m_write_completed = false;

  rc = fds_record_find(CONFIG_FILE, taskId, &desc, &tok);

  if(rc == NRF_SUCCESS)
  {
    rc = fds_record_update(&desc, &record);
    APP_ERROR_CHECK(rc);
    while(!m_update_completed);
    m_update_completed = false;
    m_gc_completed = false;
    rc = fds_gc();
    APP_ERROR_CHECK(rc);
    while(!m_gc_completed);
  }
  else
  {
    rc = fds_record_write(&desc, &record);

    if ((rc != NRF_SUCCESS) && (rc == FDS_ERR_NO_SPACE_IN_FLASH))
    {
      NRF_LOG_INFO("No space in flash, delete some records to update the config file.");
    }
    else
    {
      APP_ERROR_CHECK(rc);
      while(!m_write_completed);
      m_write_completed = false;
    }
  }
}

void readTask(uint8_t taskId, taskConfig *taskCfg)
{
  ret_code_t rc;
  fds_record_t record;
  fds_record_desc_t desc = {0};
  fds_find_token_t  tok  = {0};
  fds_flash_record_t config = {0};

  rc = fds_record_find(CONFIG_FILE, taskId, &desc, &tok);

  if(rc == NRF_SUCCESS)
  {
    rc = fds_record_open(&desc, &config);
    APP_ERROR_CHECK(rc);

    memcpy(taskCfg, config.p_data, sizeof(taskConfig));

    rc = fds_record_close(&desc);
    APP_ERROR_CHECK(rc);
  }
}

void readAllTasks()
{
  ret_code_t rc;
  fds_record_t record;
  fds_record_desc_t desc = {0};
  fds_find_token_t  tok  = {0};
  fds_flash_record_t config = {0};

  rc = fds_record_find(CONFIG_FILE, TOTAL_TASKS_RECORD_KEY, &desc, &tok);
  
  if(rc == NRF_SUCCESS)
  {  
    rc = fds_record_open(&desc, &config);
    APP_ERROR_CHECK(rc);

    memcpy(&totalScheduledTasksSaved, config.p_data, sizeof(uint32_t));

    NRF_LOG_INFO("\r\n Total Scheduled Tasks %ld \r\n",totalScheduledTasksSaved);

    rc = fds_record_close(&desc);
    APP_ERROR_CHECK(rc);

    for(uint8_t i = 0; i < totalScheduledTasksSaved; i++)
    {
      readTask(i+1, &taskObj[i]);
      NRF_LOG_INFO("\r\n Scheduled task id: %d \r\n",taskObj[i].taskId);
    }
  }
}

void saveTotalTasks(uint32_t *totalTasks)
{
  ret_code_t rc;
  fds_record_t record;
  fds_record_desc_t desc = {0};
  fds_find_token_t  tok  = {0};

  record.file_id = CONFIG_FILE;
  record.key = TOTAL_TASKS_RECORD_KEY;
  record.data.p_data = totalTasks;
  record.data.length_words = sizeof(uint32_t);
  m_write_completed = false;
  m_update_completed = false;

  rc = fds_record_find(CONFIG_FILE, TOTAL_TASKS_RECORD_KEY, &desc, &tok);

  if(rc == NRF_SUCCESS)
  {
    NRF_LOG_INFO("Scheduled Record update %ld \r\n",*totalTasks);
    rc = fds_record_update(&desc, &record);
    APP_ERROR_CHECK(rc);
    while(!m_update_completed);
    m_update_completed = false;
    m_gc_completed = false;
    rc = fds_gc();
    APP_ERROR_CHECK(rc);
    while(!m_gc_completed);
  }
  else
  {
    rc = fds_record_write(&desc, &record);
    NRF_LOG_INFO("Scheduled Record write%ld \r\n",*totalTasks);
    if ((rc != NRF_SUCCESS) && (rc == FDS_ERR_NO_SPACE_IN_FLASH))
    {
      NRF_LOG_INFO("No space in flash, delete some records to update the config file.");
    }
    else
    {
      APP_ERROR_CHECK(rc);
      while(!m_write_completed);
      m_write_completed = false;
    }
  }
}

void saveDeviceConfig(deviceConfig *deviceCfg)
{
  ret_code_t rc;
  fds_record_t record;
  fds_record_desc_t desc = {0};
  fds_find_token_t  tok  = {0};

  record.file_id = CONFIG_FILE;
  record.key = DEVICE_CONFIG_RECORD_KEY;
  record.data.p_data = deviceCfg;
  record.data.length_words = ((sizeof(deviceConfig)+2)/sizeof(uint32_t));
  m_write_completed = false;
  m_update_completed = false;

  rc = fds_record_find(CONFIG_FILE, DEVICE_CONFIG_RECORD_KEY, &desc, &tok);

  if(rc == NRF_SUCCESS)
  {
    rc = fds_record_update(&desc, &record);
    APP_ERROR_CHECK(rc);
    while(!m_update_completed);
    m_update_completed = false;
    m_gc_completed = false;
    rc = fds_gc();
    APP_ERROR_CHECK(rc);
    while(!m_gc_completed);
  }
  else
  {
    rc = fds_record_write(&desc, &record);
    if ((rc != NRF_SUCCESS) && (rc == FDS_ERR_NO_SPACE_IN_FLASH))
    {
      NRF_LOG_INFO("No space in flash, delete some records to update the config file.");
    }
    else
    {
      APP_ERROR_CHECK(rc);
      while(!m_write_completed);
      m_write_completed = false;
    }
  }
}

void readDeviceConfig(deviceConfig *deviceCfg)
{
  ret_code_t rc;
  fds_record_t record;
  fds_record_desc_t desc = {0};
  fds_find_token_t  tok  = {0};
  fds_flash_record_t config = {0};

  rc = fds_record_find(CONFIG_FILE, DEVICE_CONFIG_RECORD_KEY, &desc, &tok);

  if(rc == NRF_SUCCESS)
  {
    rc = fds_record_open(&desc, &config);
    APP_ERROR_CHECK(rc);

    memcpy(deviceCfg, config.p_data, sizeof(deviceConfig));

    rc = fds_record_close(&desc);
    APP_ERROR_CHECK(rc);
  }
  else if(rc == FDS_ERR_NOT_FOUND)
  {
    // set default device config and save it
    strncpy(&deviceCfg->serialNumber[0],DEFAULT_SERIALNO,14);
    deviceCfg->serialNumber[14] = NULL;
    deviceCfg->deviceType = DEFAULT_DEVICE_TYPE;
    deviceCfg->buzzerEnable = 1;
    deviceCfg->continuousAdvertisement = 0;
    deviceCfg->taskReminderInterval = DEFAULT_TASK_REMINDER_INTERVAL;
    deviceCfg->doseCountLimit = DEFAULT_DOSECOUNT_LIMIT;
    saveDeviceConfig(deviceCfg);
  }
}

void saveUnscheduledTask(unScheduledEvent *unSchdEvt)
{
  ret_code_t rc;
  fds_record_t record,record1;
  fds_record_desc_t desc = {0};
  fds_find_token_t  tok  = {0};

  record.file_id = CONFIG_FILE;
  record.key = unSchdEvt->id + UNSCHEDULED_TASK_KEY_OFFSET;
  record.data.p_data = unSchdEvt;
  record.data.length_words = sizeof(unScheduledEvent)/sizeof(uint32_t);

  m_write_completed = false;
  m_update_completed = false;

  rc = fds_record_find(CONFIG_FILE, record.key, &desc, &tok);

  if(rc == NRF_SUCCESS)
  {
    rc = fds_record_update(&desc, &record);
    APP_ERROR_CHECK(rc);
    while(!m_update_completed);
    m_update_completed = false;
    m_gc_completed = false;
    rc = fds_gc();
    APP_ERROR_CHECK(rc);
    while(!m_gc_completed);
  }
  else
  {
    rc = fds_record_write(&desc, &record);
    if ((rc != NRF_SUCCESS) && (rc == FDS_ERR_NO_SPACE_IN_FLASH))
    {
      NRF_LOG_INFO("No space in flash, delete some records to update the config file.");
    }
    else
    {
      APP_ERROR_CHECK(rc);
      while(!m_write_completed);
      m_write_completed = false;
    }
  }
}

void saveTotalUnscheduledTasks(uint32_t *total)
{
  ret_code_t rc;
  fds_record_t record;
  fds_record_desc_t desc = {0};
  fds_find_token_t  tok  = {0};

  record.file_id = CONFIG_FILE;
  record.key = TOTAL_UNSCHEDULED_EVENTS_KEY;
  record.data.p_data = total;
  record.data.length_words = sizeof(uint32_t);

  m_write_completed = false;
  m_update_completed = false;
  rc = fds_record_find(CONFIG_FILE, record.key, &desc, &tok);

  if(rc == NRF_SUCCESS)
  {
    rc = fds_record_update(&desc, &record);
    NRF_LOG_INFO("Unscheduled Record update \r\n");
    APP_ERROR_CHECK(rc);
    while(!m_update_completed);
    m_update_completed = false;
    m_gc_completed = false;
    rc = fds_gc();
    APP_ERROR_CHECK(rc);
    while(!m_gc_completed);
  }
  else
  {
    rc = fds_record_write(&desc, &record);
    NRF_LOG_INFO("Unscheduled Record write \r\n");
    if ((rc != NRF_SUCCESS) && (rc == FDS_ERR_NO_SPACE_IN_FLASH))
    {
      NRF_LOG_INFO("No space in flash, delete some records to update the config file.");
    }
    else
    {
      APP_ERROR_CHECK(rc);
      while(!m_write_completed);
      m_write_completed = false;
    }
  }
}


void readUnscheduledTask(uint32_t taskId, unScheduledEvent *unSchdEvt)
{
  ret_code_t rc;
  fds_record_t record;
  fds_record_desc_t desc = {0};
  fds_find_token_t  tok  = {0};
  fds_flash_record_t config = {0};

  rc = fds_record_find(CONFIG_FILE, taskId + UNSCHEDULED_TASK_KEY_OFFSET, &desc, &tok);

  if(rc == NRF_SUCCESS)
  {
    rc = fds_record_open(&desc, &config);
    APP_ERROR_CHECK(rc);

    memcpy(unSchdEvt, config.p_data, sizeof(unScheduledEvent));

    rc = fds_record_close(&desc);
    APP_ERROR_CHECK(rc);
  }
}

void readAllUnscheduledTasks()
{
  ret_code_t rc;
  fds_record_t record;
  fds_record_desc_t desc = {0};
  fds_find_token_t  tok  = {0};
  fds_flash_record_t config = {0};
  uint32_t tmpId;

  rc = fds_record_find(CONFIG_FILE, TOTAL_UNSCHEDULED_EVENTS_KEY, &desc, &tok);
  
  if(rc == NRF_SUCCESS)
  {  
    rc = fds_record_open(&desc, &config);
    APP_ERROR_CHECK(rc);

    memcpy(&totalUnscheduledTasksSaved, config.p_data, sizeof(uint32_t));

    NRF_LOG_INFO("\r\n Total unscheduled Tasks %ld \r\n",totalUnscheduledTasksSaved);
    rc = fds_record_close(&desc);
    APP_ERROR_CHECK(rc);

    for(uint8_t i = 0; i < totalUnscheduledTasksSaved; i++)
    {
      tmpId = i + 1;
      readUnscheduledTask(tmpId, &unSchdEvent[i]);
      NRF_LOG_INFO("\r\n unscheduled task id: %d \r\n",unSchdEvent[i].id);
    }
  }
}

void eraseAllRecordsinFlash()
{
    fds_file_delete(CONFIG_FILE);
    fds_gc();
}