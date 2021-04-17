/* @file main.c 
 * @brief custom BLE project main file.
 *
 * This file contains demo application which uses custom BLE service and characteristic.
 * custom service has functionality of read/write characteristic value and notification
   send at interval of 1 second.
 */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "ble.h"
#include "ble_hci.h"
#include "ble_srv_common.h"
#include "ble_advdata.h"
#include "ble_advertising.h"
#include "ble_conn_params.h"
#include "nrf_sdh.h"
#include "nrf_sdh_soc.h"
#include "nrf_sdh_ble.h"
#include "app_timer.h"
#include "peer_manager.h"
#include "peer_manager_handler.h"
#include "bsp_btn_ble.h"
#include "ble_conn_state.h"
#include "nrf_ble_gatt.h"
#include "nrf_pwr_mgmt.h"
#include "nrf_drv_saadc.h"
#include "nrf_log.h"
#include "nrf_power.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"
#include "flash.h"
#include "ble_dfu.h"
#include "nrf_bootloader_info.h"
#include "app.h"

BLE_CUS_DEF(m_cus_1);
BLE_CUS_DEF(m_cus_2);

NRF_BLE_GATT_DEF(m_gatt);                                                       /**< GATT module instance. */
BLE_ADVERTISING_DEF(m_advertising);                                             /**< Advertising module instance. */
ble_advdata_manuf_data_t manufacturer_data;
ble_cus_evt_t *receivedBLEevent;
extern ble_cus_evt_t evt;
extern taskConfig taskObj[MAX_TASKS];
extern unScheduledEvent unSchdEvent[MAX_UNSCHEDULED_EVENTS];
unScheduledEvent tempUnSchdEvent;
extern deviceConfig deviceConfigObj;
extern time_t     doseTimeStamp[MAX_DOSES_PER_TASK];
volatile extern bool buzzerTimerExpired;
uint32_t doseCounter=0;
uint8_t taskIndex = 0;
extern uint32_t totalUnscheduledTasksSaved;
bool startAdvertising=false;
bool unscheduledEvent = false;
bool deviceConnected = false;
bool blinkLedOnCommand = false;
static uint16_t m_conn_handle = BLE_CONN_HANDLE_INVALID;                        /**< Handle of the current connection. */
bool notifications_service1_enable=false;
bool notifications_service2_enable=false;
bool receivedFrame = false;
uint8_t saveCompletedTaskIndex = 0;
static nrf_saadc_value_t     m_buffer_pool[2][SAMPLES_IN_BUFFER];
uint8_t percentage_batt_lvl=0;
uint16_t batt_lvl_in_milli_volts=0;


// UUID for custom services.
static ble_uuid_t m_adv_uuids[] =                                               /**< Universally unique service identifiers. */
{
    {CUSTOM_SERVICE_1_UUID, BLE_UUID_TYPE_BLE},
    {CUSTOM_SERVICE_2_UUID, BLE_UUID_TYPE_BLE}
};

ble_cus_t *getServiceDefinition(uint8_t id);

app_timer_t *getAppTimerInstance();

/**@brief Callback function for asserts in the SoftDevice.
 *
 * @details This function will be called in case of an assert in the SoftDevice.
 *
 * @warning This handler is an example only and does not fit a final product. You need to analyze
 *          how your product is supposed to react in case of Assert.
 * @warning On assert from the SoftDevice, the system can only recover on reset.
 *
 * @param[in] line_num   Line number of the failing ASSERT call.
 * @param[in] file_name  File name of the failing ASSERT call.
 */
void assert_nrf_callback(uint16_t line_num, const uint8_t * p_file_name)
{
    app_error_handler(DEAD_BEEF, line_num, p_file_name);
}


/**@brief Function for handling Peer Manager events.
 *
 * @param[in] p_evt  Peer Manager event.
 */
static void pm_evt_handler(pm_evt_t const * p_evt)
{
    pm_handler_on_pm_evt(p_evt);
    pm_handler_flash_clean(p_evt);

    switch (p_evt->evt_id)
    {
        case PM_EVT_PEERS_DELETE_SUCCEEDED:
            NRF_LOG_INFO("deleted bonds");
            advertising_start(false);
            break;

        default:
            break;
    }
}

/**@brief Function for handling the periodic interval notification
 *
 * @details This function will be called each time the timer expires.
 *
 * @param[in] p_context  Pointer used for passing some arbitrary information (context) from the
 *                       app_start_timer() call to the timeout handler.
 */
void notification_timeout_handler(void* p_context)
{
   if(!buzzerTimerExpired)
   {
     NRF_LOG_INFO("Buzzer switch duty cycle at %d sec", BUZZER_BEEP_TIME);
     buzzerTimerExpired = true;
   }
    
    if(blinkLedOnCommand)
    {
      blinkLedOnCommand = false;
      nrf_gpio_pin_toggle(LED_2);
    }
}


///**@brief Function for the Timer initialization.
// *
// * @details Initializes the timer module. This creates and starts application timers.
// */
static void timers_init(void)
{
    // Initialize timer module.
    ret_code_t err_code = app_timer_init();
    APP_ERROR_CHECK(err_code);

    // Create timers.
    err_code = app_timer_create(&m_notification_timer_id, APP_TIMER_MODE_SINGLE_SHOT, notification_timeout_handler);
    APP_ERROR_CHECK(err_code); 
}

void saadc_callback(nrf_drv_saadc_evt_t const * p_event)
{
    if (p_event->type == NRF_DRV_SAADC_EVT_DONE)
    {
        nrf_saadc_value_t adc_result;
        uint32_t          err_code;

        adc_result = p_event->data.done.p_buffer[0];

        err_code = nrf_drv_saadc_buffer_convert(p_event->data.done.p_buffer, 1);
        APP_ERROR_CHECK(err_code);

        batt_lvl_in_milli_volts = ADC_RESULT_IN_MILLI_VOLTS(adc_result) +
                                  DIODE_FWD_VOLT_DROP_MILLIVOLTS;
        percentage_batt_lvl = battery_level_in_percent(batt_lvl_in_milli_volts);       

        NRF_LOG_INFO("Battery Percentage %d, %d", percentage_batt_lvl,batt_lvl_in_milli_volts);        
    }
}

void saadc_init(void)
{
    ret_code_t err_code;
    nrf_saadc_channel_config_t channel_config =
    NRF_DRV_SAADC_DEFAULT_CHANNEL_CONFIG_SE(NRF_SAADC_INPUT_AIN0);

    err_code = nrf_drv_saadc_init(NULL, saadc_callback);
    APP_ERROR_CHECK(err_code);

    err_code = nrf_drv_saadc_channel_init(0, &channel_config);
    APP_ERROR_CHECK(err_code);

    err_code = nrf_drv_saadc_buffer_convert(m_buffer_pool[0], SAMPLES_IN_BUFFER);
    APP_ERROR_CHECK(err_code);

    err_code = nrf_drv_saadc_buffer_convert(m_buffer_pool[1], SAMPLES_IN_BUFFER);
    APP_ERROR_CHECK(err_code);

}

/**@brief Function for the GAP initialization.
 *
 * @details This function sets up all the necessary GAP (Generic Access Profile) parameters of the
 *          device including the device name, appearance, and the preferred connection parameters.
 */
static void gap_params_init(void)
{
    ret_code_t              err_code;
    ble_gap_conn_params_t   gap_conn_params;
    ble_gap_conn_sec_mode_t sec_mode;

    BLE_GAP_CONN_SEC_MODE_SET_OPEN(&sec_mode);

    err_code = sd_ble_gap_device_name_set(&sec_mode,
                                          (const uint8_t *)DEVICE_NAME,
                                          strlen(DEVICE_NAME));
    APP_ERROR_CHECK(err_code);
    memset(&gap_conn_params, 0, sizeof(gap_conn_params));

    gap_conn_params.min_conn_interval = MIN_CONN_INTERVAL;
    gap_conn_params.max_conn_interval = MAX_CONN_INTERVAL;
    gap_conn_params.slave_latency     = SLAVE_LATENCY;
    gap_conn_params.conn_sup_timeout  = CONN_SUP_TIMEOUT;

    err_code = sd_ble_gap_ppcp_set(&gap_conn_params);
    APP_ERROR_CHECK(err_code);
}

static void buttonless_dfu_sdh_state_observer(nrf_sdh_state_evt_t state, void * p_context)
{
    if (state == NRF_SDH_EVT_STATE_DISABLED)
    {
        // Softdevice was disabled before going into reset. Inform bootloader to skip CRC on next boot.
        nrf_power_gpregret2_set(BOOTLOADER_DFU_SKIP_CRC);

        //Go to system off.
        nrf_pwr_mgmt_shutdown(NRF_PWR_MGMT_SHUTDOWN_GOTO_SYSOFF);
    }
}

/* nrf_sdh state observer. */
NRF_SDH_STATE_OBSERVER(m_buttonless_dfu_state_obs, 0) =
{
    .handler = buttonless_dfu_sdh_state_observer,
};

static void disconnect(uint16_t conn_handle, void * p_context)
{
    UNUSED_PARAMETER(p_context);

    ret_code_t err_code = sd_ble_gap_disconnect(conn_handle, BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION);
    if (err_code != NRF_SUCCESS)
    {
        NRF_LOG_WARNING("Failed to disconnect connection. Connection handle: %d Error: %d", conn_handle, err_code);
    }
    else
    {
        NRF_LOG_DEBUG("Disconnected connection handle %d", conn_handle);
    }
}

static void advertising_config_get(ble_adv_modes_config_t * p_config)
{
    memset(p_config, 0, sizeof(ble_adv_modes_config_t));

    p_config->ble_adv_fast_enabled  = true;
    p_config->ble_adv_fast_interval = APP_ADV_INTERVAL;
    p_config->ble_adv_fast_timeout  = APP_ADV_DURATION;
}

static void ble_dfu_evt_handler(ble_dfu_buttonless_evt_type_t event)
{
    switch (event)
    {
        case BLE_DFU_EVT_BOOTLOADER_ENTER_PREPARE:
        {
            NRF_LOG_INFO("Device is preparing to enter bootloader mode.");

            // Prevent device from advertising on disconnect.
            ble_adv_modes_config_t config;
            advertising_config_get(&config);
            config.ble_adv_on_disconnect_disabled = true;
            ble_advertising_modes_config_set(&m_advertising, &config);

            // Disconnect all other bonded devices that currently are connected.
            // This is required to receive a service changed indication
            // on bootup after a successful (or aborted) Device Firmware Update.
            uint32_t conn_count = ble_conn_state_for_each_connected(disconnect, NULL);
            NRF_LOG_INFO("Disconnected %d links.", conn_count);
            break;
        }

        case BLE_DFU_EVT_BOOTLOADER_ENTER:
            // YOUR_JOB: Write app-specific unwritten data to FLASH, control finalization of this
            //           by delaying reset by reporting false in app_shutdown_handler
            NRF_LOG_INFO("Device will enter bootloader mode.");
            break;

        case BLE_DFU_EVT_BOOTLOADER_ENTER_FAILED:
            NRF_LOG_ERROR("Request to enter bootloader mode failed asynchroneously.");
            // YOUR_JOB: Take corrective measures to resolve the issue
            //           like calling APP_ERROR_CHECK to reset the device.
            break;

        case BLE_DFU_EVT_RESPONSE_SEND_ERROR:
            NRF_LOG_ERROR("Request to send a response to client failed.");
            // YOUR_JOB: Take corrective measures to resolve the issue
            //           like calling APP_ERROR_CHECK to reset the device.
            APP_ERROR_CHECK(false);
            break;

        default:
            NRF_LOG_ERROR("Unknown event from ble_dfu_buttonless.");
            break;
    }
}


///**@brief Function for initializing the GATT module.
// */
static void gatt_init(void)
{
    ret_code_t err_code = nrf_ble_gatt_init(&m_gatt, NULL);
    APP_ERROR_CHECK(err_code);
}

/**@brief Function for handling the Custom Service 1 Service events.
 *
 * @details This function will be called for all Custom Service events which are passed to
 *          the application.
 *
 * @param[in]   p_cus_service  Custom Service structure.
 * @param[in]   p_evt          Event received from the Custom Service.
 *
 */
static void on_cus_1_evt(ble_cus_t     * p_cus_service,
                       ble_cus_evt_t * p_evt)
{
    ret_code_t err_code;
    
    switch(p_evt->evt_type)
    {
        case BLE_CUS_EVT_NOTIFICATION_ENABLED:
              NRF_LOG_INFO("Device notifications on service 1 enabled");
              notifications_service1_enable = true;
             break;

        case BLE_CUS_EVT_NOTIFICATION_DISABLED:
              NRF_LOG_INFO("Device notifications on service 1 disabled");
              notifications_service1_enable = false;
             break;

        case BLE_CUS_EVT_CONNECTED:
              startAdvertising = false;
              deviceConnected = true;
              NRF_LOG_INFO("Device connected with App");
              // turn on LED if device is connected
              nrf_gpio_pin_write(LED_2, 1);
//              setDutyCycleLED(0);
              break;

        case BLE_CUS_EVT_DISCONNECTED:
              deviceConnected = false;
              NRF_LOG_INFO("Device disconnected with App");
              nrf_gpio_cfg_output(LED_1);
              nrf_gpio_pin_write(LED_1, 0);
              // turn off LED if device is disconnected
              nrf_gpio_pin_write(LED_2, 0);
//             setDutyCycleLED(100);
             break;

        case BLE_CUS_DATA_RECEIVED:
             NRF_LOG_INFO("Received data : %s", p_evt->evtData);
             receivedBLEevent = p_evt;
             break;

        default:
              break;
    }
}

/**@brief Function for handling the Custom Service2  Service events.
 *
 * @details This function will be called for all Custom Service events which are passed to
 *          the application.
 *
 * @param[in]   p_cus_service  Custom Service structure.
 * @param[in]   p_evt          Event received from the Custom Service.
 *
 */
static void on_cus_2_evt(ble_cus_t     * p_cus_service,
                       ble_cus_evt_t * p_evt)
{
    ret_code_t err_code;
    
    switch(p_evt->evt_type)
    {
        case BLE_CUS_EVT_NOTIFICATION_ENABLED:
             NRF_LOG_INFO("Device notifications on service 2 enabled");
             notifications_service2_enable = true;
             triggerADCSampling();
             sendDeviceConfigToApp(&deviceConfigObj);             
             break;

        case BLE_CUS_EVT_NOTIFICATION_DISABLED:
             NRF_LOG_INFO("Device notifications on service 2 disabled");
             notifications_service2_enable = false;       
            break;

        case BLE_CUS_EVT_CONNECTED:
            break;

        case BLE_CUS_EVT_DISCONNECTED:
            break;
        
        case BLE_CUS_DATA_RECEIVED:
              break;

        default:
              break;
    }
}

/**@brief Function for initializing services that will be used by the application.
 */
static void services_init(void)
{
    ret_code_t         err_code;
    ble_cus_init_t     cus_1_init,cus_2_init;
    ble_dfu_buttonless_init_t dfus_init = {0};

    dfus_init.evt_handler = ble_dfu_evt_handler;

    err_code = ble_dfu_buttonless_init(&dfus_init);
    APP_ERROR_CHECK(err_code);


    cus_1_init.evt_handler                = on_cus_1_evt;

    BLE_GAP_CONN_SEC_MODE_SET_OPEN(&cus_1_init.custom_value_char_attr_md.cccd_write_perm);
    BLE_GAP_CONN_SEC_MODE_SET_OPEN(&cus_1_init.custom_value_char_attr_md.read_perm);
    BLE_GAP_CONN_SEC_MODE_SET_OPEN(&cus_1_init.custom_value_char_attr_md.write_perm);

    m_cus_1.service_uuid = CUSTOM_SERVICE_1_UUID;
    m_cus_1.char_uuid = CUSTOM_VALUE_CHAR_1_UUID;

    err_code = ble_cus_init(&m_cus_1, &cus_1_init);
    APP_ERROR_CHECK(err_code);

    cus_2_init.evt_handler                = on_cus_2_evt;

    BLE_GAP_CONN_SEC_MODE_SET_OPEN(&cus_2_init.custom_value_char_attr_md.cccd_write_perm);
    BLE_GAP_CONN_SEC_MODE_SET_OPEN(&cus_2_init.custom_value_char_attr_md.read_perm);
    BLE_GAP_CONN_SEC_MODE_SET_OPEN(&cus_2_init.custom_value_char_attr_md.write_perm);

    m_cus_2.service_uuid = CUSTOM_SERVICE_2_UUID;
    m_cus_2.char_uuid = CUSTOM_VALUE_CHAR_2_UUID;

    err_code = ble_cus_init(&m_cus_2, &cus_2_init);
    APP_ERROR_CHECK(err_code);
}


/**@brief Function for handling the Connection Parameters Module.
 *
 * @details This function will be called for all events in the Connection Parameters Module which
 *          are passed to the application.
 *          @note All this function does is to disconnect. This could have been done by simply
 *                setting the disconnect_on_fail config parameter, but instead we use the event
 *                handler mechanism to demonstrate its use.
 *
 * @param[in] p_evt  Event received from the Connection Parameters Module.
 */
static void on_conn_params_evt(ble_conn_params_evt_t * p_evt)
{
    ret_code_t err_code;

    if (p_evt->evt_type == BLE_CONN_PARAMS_EVT_FAILED)
    {
        err_code = sd_ble_gap_disconnect(m_conn_handle, BLE_HCI_CONN_INTERVAL_UNACCEPTABLE);
        APP_ERROR_CHECK(err_code);
    }
}


/**@brief Function for handling a Connection Parameters error.
 *
 * @param[in] nrf_error  Error code containing information about what went wrong.
 */
static void conn_params_error_handler(uint32_t nrf_error)
{
    APP_ERROR_HANDLER(nrf_error);
}


/**@brief Function for initializing the Connection Parameters module.
 */
static void conn_params_init(void)
{
    ret_code_t             err_code;
    ble_conn_params_init_t cp_init;

    memset(&cp_init, 0, sizeof(cp_init));

    cp_init.p_conn_params                  = NULL;
    cp_init.first_conn_params_update_delay = FIRST_CONN_PARAMS_UPDATE_DELAY;
    cp_init.next_conn_params_update_delay  = NEXT_CONN_PARAMS_UPDATE_DELAY;
    cp_init.max_conn_params_update_count   = MAX_CONN_PARAMS_UPDATE_COUNT;
    cp_init.start_on_notify_cccd_handle    = BLE_GATT_HANDLE_INVALID;
    cp_init.disconnect_on_fail             = false;
    cp_init.evt_handler                    = on_conn_params_evt;
    cp_init.error_handler                  = conn_params_error_handler;

    err_code = ble_conn_params_init(&cp_init);
    APP_ERROR_CHECK(err_code);
}

/**@brief Function for handling the idle state (main loop).
 *
 * @details If there is no pending log operation, then sleep until next the next event occurs.
 */
static void idle_state_handle(void)
{
    if (NRF_LOG_PROCESS() == false)
    {
        nrf_pwr_mgmt_run();
    }
}

/**@brief Function for putting the chip into sleep mode.
 *
 * @note This function will not return.
 */
static void sleep_mode_enter(void)
{
    NRF_LOG_INFO("Advertising idle and going to sleep mode");
    nrf_gpio_pin_write(LED_2, 0);
    idle_state_handle();
}


/**@brief Function for handling advertising events.
 *
 * @details This function will be called for advertising events which are passed to the application.
 *
 * @param[in] ble_adv_evt  Advertising event.
 */
static void on_adv_evt(ble_adv_evt_t ble_adv_evt)
{
    ret_code_t err_code;

    switch (ble_adv_evt)
    {
        case BLE_ADV_EVT_FAST:
            startAdvertising = true;
//            setDutyCycleLED(25);
            NRF_LOG_INFO("Fast advertising event");
            break;

        case BLE_ADV_EVT_IDLE:
            if(deviceConfigObj.continuousAdvertisement == 1)
            {
                startAdvertising = true;
                advertising_start(false);
            }
            else
            {
              startAdvertising = false;
              sleep_mode_enter();
            }
            break;

        default:
            break;
    }
}


/**@brief Function for initializing the BLE stack.
 *
 * @details Initializes the SoftDevice and the BLE event interrupt.
 */
static void ble_stack_init(void)
{
    ret_code_t err_code;

    err_code = nrf_sdh_enable_request();
    APP_ERROR_CHECK(err_code);

    // Configure the BLE stack using the default settings.
    // Fetch the start address of the application RAM.
    uint32_t ram_start = 0;
    err_code = nrf_sdh_ble_default_cfg_set(APP_BLE_CONN_CFG_TAG, &ram_start);
    APP_ERROR_CHECK(err_code);

    // Enable BLE stack.
    err_code = nrf_sdh_ble_enable(&ram_start);
    APP_ERROR_CHECK(err_code);
}


/**@brief Function for the Peer Manager initialization.
 */
static void peer_manager_init(void)
{
    ble_gap_sec_params_t sec_param;
    ret_code_t           err_code;

    err_code = pm_init();
    APP_ERROR_CHECK(err_code);

    memset(&sec_param, 0, sizeof(ble_gap_sec_params_t));

    // Security parameters to be used for all security procedures.
    sec_param.bond           = SEC_PARAM_BOND;
    sec_param.mitm           = SEC_PARAM_MITM;
    sec_param.lesc           = SEC_PARAM_LESC;
    sec_param.keypress       = SEC_PARAM_KEYPRESS;
    sec_param.io_caps        = SEC_PARAM_IO_CAPABILITIES;
    sec_param.oob            = SEC_PARAM_OOB;
    sec_param.min_key_size   = SEC_PARAM_MIN_KEY_SIZE;
    sec_param.max_key_size   = SEC_PARAM_MAX_KEY_SIZE;
    sec_param.kdist_own.enc  = 1;
    sec_param.kdist_own.id   = 1;
    sec_param.kdist_peer.enc = 1;
    sec_param.kdist_peer.id  = 1;

    err_code = pm_sec_params_set(&sec_param);
    APP_ERROR_CHECK(err_code);

    err_code = pm_register(pm_evt_handler);
    APP_ERROR_CHECK(err_code);
}


/**@brief Clear bond information from persistent storage.
 */
static void delete_bonds(void)
{
    ret_code_t err_code;

    NRF_LOG_INFO("Erase bonds!");

    err_code = pm_peers_delete();
    APP_ERROR_CHECK(err_code);
}


/**@brief Function for handling events from the BSP module.
 *
 * @param[in]   event   Event generated when button is pressed.
 */
static void bsp_event_handler(bsp_event_t event)
{ 
    switch (event)
    {
        case BSP_EVENT_KEY_SHORT_PRESS:
          NRF_LOG_INFO("\r\n Short button press - %s\r\n", nrf_cal_get_time_string(false));
          if(!startAdvertising && !deviceConnected)
          {
            NRF_LOG_INFO("Device started advertising due to short button press event");
            startAdvertising = true;
            advertising_start(false);
          }
          app_timer_start(getAppTimerInstance(), LED_BLINK_INTERVAL, NULL);
          blinkLedOnCommand = true;
          nrf_gpio_pin_toggle(LED_2);
        break;

        case BSP_EVENT_KEY_LONG_PRESS:
          app_timer_start(getAppTimerInstance(), LED_BLINK_INTERVAL, NULL);
          blinkLedOnCommand = true;
          nrf_gpio_pin_toggle(LED_2);
              NRF_LOG_INFO("\r\n Button pressed time for dose - %s\r\n", nrf_cal_get_time_string(false));
              unscheduledEvent = true;
              doseCounter++;
              for(taskIndex = 0; taskIndex < MAX_TASKS; taskIndex++)
              {
                  if(taskObj[taskIndex].taskState == IN_PROGRESS)
                  {
                      unscheduledEvent = false;
                      doseTimeStamp[taskObj[taskIndex].doseCount] = getCurrentTime_t();
                      taskObj[taskIndex].doseCount++;
                      if(taskObj[taskIndex].doseCount == taskObj[taskIndex].noOfDoses)
                      {
                          taskObj[taskIndex].taskState = COMPLETED;
                          NRF_LOG_INFO("Stop Buzzer due to task completed \r\n");
                          stopBuzzer();
                          taskObj[taskIndex].taskCompletionTime = getCurrentTime_t();
                          saveCompletedTaskIndex = taskIndex + 1;
                      }
                      break;
                  }
              }

              if(unscheduledEvent)
              {
                if(!startAdvertising && !deviceConnected)
                {
                  NRF_LOG_INFO("Device started advertising due to unscheduled event");
                  startAdvertising = true;
                  advertising_start(false);
                }
                if(totalUnscheduledTasksSaved >= MAX_UNSCHEDULED_EVENTS)
                {
                  totalUnscheduledTasksSaved = 0;
                }
                totalUnscheduledTasksSaved++;
                unSchdEvent[totalUnscheduledTasksSaved - 1].id = totalUnscheduledTasksSaved;
                unSchdEvent[totalUnscheduledTasksSaved - 1].unScheduledEventTimeStamp = getCurrentTime_t();
                memcpy(&tempUnSchdEvent, &unSchdEvent[totalUnscheduledTasksSaved - 1], sizeof(unScheduledEvent));                
              }
            break;

        default:
            break;
    }
}


/**@brief Function for initializing the Advertising functionality.
 */
static void advertising_init(void)
{
    ret_code_t             err_code;
    ble_advertising_init_t init;

    memset(&init, 0, sizeof(init));

    init.advdata.name_type               = BLE_ADVDATA_FULL_NAME;
    init.advdata.include_appearance      = true;
    init.advdata.flags                   = BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE;
    init.advdata.uuids_complete.uuid_cnt = sizeof(m_adv_uuids) / sizeof(m_adv_uuids[0]);
    init.advdata.uuids_complete.p_uuids  = m_adv_uuids;

    init.config.ble_adv_fast_enabled  = true;
    init.config.ble_adv_fast_interval = APP_ADV_INTERVAL;
    init.config.ble_adv_fast_timeout  = APP_ADV_DURATION;

    manufacturer_data.company_identifier = COMPANY_ID;
    memcpy(manufacturer_data.data.p_data,&deviceConfigObj,sizeof(deviceConfig));
    init.advdata.p_manuf_specific_data = &manufacturer_data;

    init.evt_handler = on_adv_evt;

    err_code = ble_advertising_init(&m_advertising, &init);
    APP_ERROR_CHECK(err_code);

    ble_advertising_conn_cfg_tag_set(&m_advertising, APP_BLE_CONN_CFG_TAG);
}


/**@brief Function for initializing buttons and leds.
 *
 * @param[out] p_erase_bonds  Will be true if the clear bonding button was pressed to wake the application up.
 */
static void buttons_leds_init()
{
    ret_code_t err_code;
    bsp_event_t startup_event;

    //err_code = bsp_init(BSP_INIT_LEDS | BSP_INIT_BUTTONS, bsp_event_handler);
    err_code = bsp_init(BSP_INIT_BUTTONS, bsp_event_handler);
    APP_ERROR_CHECK(err_code);

    nrf_gpio_cfg_output(LED_1);
}


/**@brief Function for initializing the nrf log module.
 */
static void log_init(void)
{
    ret_code_t err_code = NRF_LOG_INIT(NULL);
    APP_ERROR_CHECK(err_code);

    NRF_LOG_DEFAULT_BACKENDS_INIT();
}


/**@brief Function for initializing power management.
 */
static void power_management_init(void)
{
    ret_code_t err_code;
    err_code = nrf_pwr_mgmt_init();
    APP_ERROR_CHECK(err_code);
}


/**@brief Function for starting advertising.
 */
void advertising_start(bool erase_bonds)
{
    if (erase_bonds == true)
    {
        delete_bonds();
        // Advertising is started by PM_EVT_PEERS_DELETED_SUCEEDED event
    }
    else
    {
        startAdvertising = true;
        ret_code_t err_code = ble_advertising_start(&m_advertising, BLE_ADV_MODE_FAST);
        APP_ERROR_CHECK(err_code);
    }
}
static void calendar_updated()
{
//  NRF_LOG_INFO("Current time:\t%s\r\n", nrf_cal_get_time_string(false));
}


ble_cus_t *getServiceDefinition(uint8_t id)
{
  if(id == 1)
  {
    return &m_cus_1;
  }
  else
  {
    return &m_cus_2;
  }
}

app_timer_t *getAppTimerInstance()
{
  return m_notification_timer_id;
}

/**@brief Function for application main entry.
 */
int main(void)
{
    nrf_gpio_cfg_output(LED_2);
    nrf_gpio_pin_write(LED_2,0);
    log_init();    
    NRF_LOG_INFO("\r\n Alveo demo started \r\n");
    buttons_leds_init();
    saadc_init();
    timers_init(); 
    pwm_init();
    nrf_cal_init();
    nrf_cal_set_callback(calendar_updated, 1);
    power_management_init();
    ble_stack_init();
    flash_page_init();
    readDeviceConfig(&deviceConfigObj);  
    gap_params_init();
    gatt_init();
    advertising_init();
    services_init();
    conn_params_init();
    peer_manager_init();
    

//    flash_page_init();
//---------------------------------Test Flash data store-----------------------------------------//
//    taskConfig tempTaskObj,readTaskObj;
//    uint32_t totalTasks;
//    char data[100];
//    timeStruct timeDate;
//    strcpy(data,"Task[1,1,1,1,1234,26,4,2020,10,21,30,2,1,30,Test");
//
//              char* token = strtok(data, "[");
//          token = strtok(NULL,",");
//
//        char totalSavedTasks = atoi(token);
//          token = strtok(NULL,",");
//
//          tempTaskObj.taskId = atoi(token);
//          token = strtok(NULL,",");
//      
//          tempTaskObj.taskDetailsId = atoi(token);
//          token = strtok(NULL,",");
//
//          tempTaskObj.taskType = atoi(token);
//          token = strtok(NULL,",");
//
//          tempTaskObj.orderId = atoi(token);
//          token = strtok(NULL,",");
//
//          timeDate.dateTimeConfig.day = atoi(token);
//          token = strtok(NULL,",");
//
//          timeDate.dateTimeConfig.month = atoi(token);
//          token = strtok(NULL,",");
//
//          timeDate.dateTimeConfig.year = atoi(token);
//          token = strtok(NULL,",");
//
//          timeDate.dateTimeConfig.hour = atoi(token);
//          token = strtok(NULL,",");
//
//          timeDate.dateTimeConfig.minute = atoi(token);
//          token = strtok(NULL,",");
//
//          timeDate.dateTimeConfig.second = atoi(token);
//          token = strtok(NULL,",");
//
//          tempTaskObj.noOfDoses = atoi(token);
//          token = strtok(NULL,",");
//
//          tempTaskObj.taskState = atoi(token);
//          token = strtok(NULL,",");
//
//          tempTaskObj.durationBetweenDoses = atoi(token);
//          token = strtok(NULL,",");
//
//          strcpy(&tempTaskObj.medicationName[0],token);
// 
//    saveTask(tempTaskObj.taskId, &tempTaskObj);
//    totalTasks = 1;
//    saveTotalTasks(&totalTasks);
//    readTask(1, &readTaskObj);
//    readAllTasks();
//---------------------------------Test Flash data store-----------------------------------------//

//-------------------------Test buzzer-----------------------------------------------------------//

//app_timer_start(getAppTimerInstance(), BUZZER_BEEP_INTERVAL, NULL);
//buzzerTimerExpired = true;
//
//while(1)
//{
//  setDutyCycleBuzzer(60);
//  nrf_delay_ms(90);
//  setDutyCycleBuzzer(100);
//  nrf_delay_ms(200);
//  setDutyCycleBuzzer(60);
//  nrf_delay_ms(90);
//  setDutyCycleBuzzer(100);
//  nrf_delay_ms(700);
//}
//-------------------------Close buzzer----------------------------------------------------------//
    readAllUnscheduledTasks();

    readAllTasks();    

    triggerADCSampling();

    //NRF_LOG_INFO("Device config [%s,%c,%d,%d,%d,%d,%d,%s]", deviceConfigObj.serialNumber,deviceConfigObj.deviceType,deviceConfigObj.buzzerEnable,deviceConfigObj.continuousAdvertisement, deviceConfigObj.taskReminderInterval, deviceConfigObj.doseCountLimit, percentage_batt_lvl, FIRMWARE_VERSION);

    NRF_LOG_INFO("\r\n Device config %s, %c, %d, %d, %d, %d\r\n",deviceConfigObj.serialNumber,deviceConfigObj.deviceType,deviceConfigObj.buzzerEnable,deviceConfigObj.continuousAdvertisement,deviceConfigObj.taskReminderInterval,deviceConfigObj.doseCountLimit);
  
    if(deviceConfigObj.continuousAdvertisement == 1)
    {
      startAdvertising = true;
      advertising_start(false);
    }
    NRF_LOG_INFO("\r\n Initiliazation done \r\n");

//    time_t currentTime;
//    currentTime = getCurrentTime_t();
//
//    triggerADCSampling();
//    time_t lastTime=0;
//    double diff;
//    NRF_LOG_INFO("Battery %ld, %ld", percentage_batt_lvl, batt_lvl_in_milli_volts);
//    while(1)
//    {
//       currentTime = getCurrentTime_t();
//       diff = difftime(lastTime,currentTime);
//       if(diff >= 5)
//       {
//          triggerADCSampling();  
//          lastTime = currentTime;
//          NRF_LOG_INFO("Battery %ld, %ld", percentage_batt_lvl, batt_lvl_in_milli_volts);
//       }
//    }
    // Enter main loop.
    enablePWMForBuzzer();
     
      beepBuzzer();
      nrf_delay_ms(500);
    //  beepBuzzer();
      

    for (;;)
    {
        if(receivedFrame)
        {
          receivedFrame = false;
          processReceivedData(receivedBLEevent->evtData,receivedBLEevent->evtDataLen);
          receivedBLEevent->evtDataLen = 0;
          memset(receivedBLEevent->evtData, 0, MEX_EVENT_DATA_LEN);
        }

        sendSyncResponseForAllTasks();
        
        checkForAlert();

        if(saveCompletedTaskIndex != 0)
        {
           saveTask(saveCompletedTaskIndex, &taskObj[saveCompletedTaskIndex-1]);
           saveCompletedTaskIndex = 0;
        }

        if(unscheduledEvent)
        {
          unscheduledEvent = false;
//          if(deviceConnected)
//          {
//            sendStatusToApp(0, UNSCHEDULED_EVENT);
//          }
//          else
//          {
            // store unscheduled event in flash (maximum upto 20 events)
            NRF_LOG_INFO("\r\n Saving unscheduled event %d\r\n",tempUnSchdEvent.id);
            saveUnscheduledTask(&tempUnSchdEvent);
            totalUnscheduledTasksSaved = tempUnSchdEvent.id;
            saveTotalUnscheduledTasks(&tempUnSchdEvent.id);
//          }
        }
        
        checkForLowBattery();

        checkForEmptyDose();

        idle_state_handle();
    }
}

void triggerADCSampling(void)
{
  nrf_drv_saadc_sample();
}

void checkForLowBattery()
{
  static bool beepBuzzerOnce=false;
  static time_t lastTime=0;
  static bool startBuzzerLoop=false;
  double diff;
  time_t currentTime;
  currentTime = getCurrentTime_t();

  diff = difftime(lastTime,currentTime);

  if(startBuzzerLoop)
  {
    if(diff >= BATTERY_LOW_BEEP_INTERVAL)
    {
      // stop buzzer
      NRF_LOG_INFO("\r\n Stopped buzzer after 10 seconds due to low battery \r\n");
      startBuzzerLoop = false;
      beepBuzzerOnce = true;      
      setDutyCycleBuzzer(0);
      disablePWMForBuzzer();
    }
    else
    {
      // activity to be done during this 10 seconds
    }
  }

  if(percentage_batt_lvl <= 5)
  {
    if(!beepBuzzerOnce && !startBuzzerLoop)
    {
      startBuzzerLoop = true;
      lastTime = currentTime;
      NRF_LOG_INFO("\r\n Started buzzer for 10 seconds as low bettery %d \r\n", percentage_batt_lvl);
      enablePWMForBuzzer();
      beepBuzzer();
      nrf_delay_ms(3000);
      beepBuzzer();
      nrf_delay_ms(3000);
      beepBuzzer();  
    }
  }
  else
  {
    beepBuzzerOnce = false;
  }
}

void checkForEmptyDose()
{
  static bool beepBuzzerOnce=false;
  static time_t lastTime=0;
  static bool startBuzzerLoop=false;
  double diff;
  time_t currentTime;
  currentTime = getCurrentTime_t();

  diff = difftime(lastTime,currentTime);

  if(startBuzzerLoop)
  {
    if(diff >= DOSELIMIT_REMINDER_INTERVAL)
    {
      // stop buzzer
      NRF_LOG_INFO("\r\n Stopped buzzer after 10 seconds due to dose limit reached \r\n");
      startBuzzerLoop = false;
      beepBuzzerOnce = true;
      setDutyCycleBuzzer(0);
      disablePWMForBuzzer();
    }
    else
    {
      // activity to be done during this 10 seconds
    }
  }

  if(doseCounter >= (deviceConfigObj.doseCountLimit - 10))
  {
    if(!beepBuzzerOnce && !startBuzzerLoop)
    {
      NRF_LOG_INFO("\r\n Started buzzer for 10 seconds as limit reached %d \r\n", doseCounter);
      startBuzzerLoop = true;
      lastTime = currentTime;
      enablePWMForBuzzer();
      beepBuzzer();
      nrf_delay_ms(3000);
      beepBuzzer();
      nrf_delay_ms(3000);
      beepBuzzer();
    }
  }
  else
  {
    beepBuzzerOnce = false;
  }
}
