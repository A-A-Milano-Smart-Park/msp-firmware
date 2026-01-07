/********************************************************************
 * @file display_tasks.cpp
 * @author AB-Engineering - https://ab-engineering.it
 * @brief   Display task for managing display events and rendering
 * @details This task handles various display events, manages the state machine for display updates,
 * @version 0.1
 * @date 2025-07-20
 *
 * @copyright Copyright (c) 2025
 *
 *********************************************************************/
// -- Includes --
#include "config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "display_task.h"
#include "display.h"

//--------------------------------------------------------------------------------------------------
//------------------ DISPLAY TASK SECTION ----------------------------------------------------------
//--------------------------------------------------------------------------------------------------

// -- defines --
// Task configuration - public values defined in display_task.h
#define DISP_QUEUE_LENGTH 10 // Internal queue configuration
#define DISP_QUEUE_ITEM_SIZE sizeof(displayData_t)
#define DISP_QUEUE_SIZE (DISP_QUEUE_LENGTH * DISP_QUEUE_ITEM_SIZE)

// Static task variables
StackType_t displayTaskStack[DISPLAY_TASK_STACK_SIZE];
StaticTask_t displayTaskBuffer;
TaskHandle_t displayTaskHandle = NULL;

// -- queue handle --
static QueueHandle_t displayTaskQueue; /*!< DISPLAY Task Events queue */

// -- finite state machine --
state_machine_t dispFSM;

// -- display data instance --
static displayData_t data{};

#define EVENT_WAIT_TIMEOUT 1000
#define RESET_TIMEOUT 10

#define ONE_SEC_TIMEOUT 1
#define TWO_SEC_TIMEOUT 2
#define THREE_SEC_TIMEOUT 3
#define FIVE_SEC_TIMEOUT 5

#define FIRST_ROW_LEN 17
#define SECOND_ROW_LEN 22
#define SECONDS_IN_MIN 60

/*********************************************************
 * @brief function to initialize the display task queue.
 *
 *********************************************************/
void vTaskDisplay_initDataQueue(void)
{
  if (displayTaskQueue == NULL)
  {
    displayTaskQueue = xQueueCreate(DISP_QUEUE_LENGTH, DISP_QUEUE_ITEM_SIZE);
  }
}

/******************************************************************
 * @brief function to send display events and data to the queue.
 *
 * @param data
 * @return BaseType_t
 ******************************************************************/
BaseType_t tTaskDisplay_sendEvent(displayData_t *data)
{
  return (BaseType_t)xQueueSend(displayTaskQueue, data, 0);
}

/**********************************************************************
 * @brief function to receive display events and data from the queue.
 *
 * @param data
 * @param xTicksToWait
 * @return BaseType_t
 **********************************************************************/
BaseType_t tTaskDisplay_receiveEvent(displayData_t *data, TickType_t xTicksToWait)
{
  return xQueueReceive(displayTaskQueue, data, xTicksToWait);
}

/*********************************************************************
 * @brief display task function that handles
 * display events and updates the display accordingly.
 *
 * @param pvParameters
 *********************************************************************/
void displayTask(void *pvParameters)
{
  // init finite state machine
  dispFSM.current_state = DISP_EVENT_WAIT_FOR_EVENT;
  dispFSM.next_state = DISP_EVENT_WAIT_FOR_EVENT;
  dispFSM.return_state = DISP_EVENT_WAIT_FOR_EVENT;
  dispFSM.isFirstTransition = true;

  TickType_t eventWaitTimeout = pdMS_TO_TICKS(EVENT_WAIT_TIMEOUT); // 1 second timeout for waiting events
  displayEvents_t displayEvents = DISP_EVENT_WAIT_FOR_EVENT;

  while (1)
  {
    switch (dispFSM.current_state)
    {
    case DISP_EVENT_WAIT_FOR_EVENT:
    {
      if (pdTRUE != tTaskDisplay_receiveEvent(&data, eventWaitTimeout))
      {
        // check if we have received the first data
        if (false == dispFSM.isFirstTransition)
        {
          dispFSM.next_state = DISP_EVENT_SHOW_MEAS_DATA;
        }
        else
        {
          dispFSM.next_state = dispFSM.return_state;
        }
      }
      else
      {
        displayEvents = data.currentEvent;
        dispFSM.next_state = displayEvents;
        if (displayEvents == DISP_EVENT_SHOW_MEAS_DATA)
        {
          dispFSM.isFirstTransition = false;
        }
      }
      break;
    }
    // set up cases
    case DISP_EVENT_DEVICE_BOOT:
    {
      vHalDisplay_DrawBoot(&data.sysData.ver, FIVE_SEC_TIMEOUT);
      dispFSM.next_state = dispFSM.return_state;
      break;
    }
    case DISP_EVENT_WIFI_MAC_ADDR:
    {
      vHalDisplay_drawTwoLines("WIFI MAC ADDRESS:", data.devInfo.baseMacChr, ONE_SEC_TIMEOUT, &data.sysStat, &data.devInfo);
      dispFSM.next_state = dispFSM.return_state;
      break;
    }
    case DISP_EVENT_SD_CARD_INIT:
    {
      vHalDisplay_drawTwoLines("Initializing", "SD Card...", ONE_SEC_TIMEOUT, &data.sysStat, &data.devInfo);
      dispFSM.next_state = dispFSM.return_state;
      break;
    }
    case DISP_EVENT_CONFIG_READ:
    {
      vHalDisplay_drawTwoLines("SD Card ok!", "Reading config...", ONE_SEC_TIMEOUT, &data.sysStat, &data.devInfo);
      dispFSM.next_state = dispFSM.return_state;
      break;
    }
    case DISP_EVENT_URL_UPLOAD_STAT:
    {
      vHalDisplay_drawTwoLines("No URL defined!", "No upload!", ONE_SEC_TIMEOUT, &data.sysStat, &data.devInfo);
      dispFSM.next_state = dispFSM.return_state;
      break;
    }
    case DISP_EVENT_SD_CARD_NOT_PRESENT:
    {
      vHalDisplay_drawTwoLines("No SD Card!", "No web!", ONE_SEC_TIMEOUT, &data.sysStat, &data.devInfo);
      dispFSM.next_state = dispFSM.return_state;
      break;
    }
    case DISP_EVENT_SD_CARD_FORMAT:
    {
      vHalDisplay_drawTwoLines("SD Card format!", "No web!", ONE_SEC_TIMEOUT, &data.sysStat, &data.devInfo);
      dispFSM.next_state = dispFSM.return_state;
      break;
    }
    case DISP_EVENT_SD_CARD_LOG_ERROR:
    {
      vHalDisplay_drawTwoLines("SD Card log", "error!", ONE_SEC_TIMEOUT, &data.sysStat, &data.devInfo);
      dispFSM.next_state = dispFSM.return_state;
      break;
    }
    case DISP_EVENT_SD_CARD_CONFIG_CREATE:
    {
      vHalDisplay_drawTwoLines("No cfg found!", "Creating...", ONE_SEC_TIMEOUT, &data.sysStat, &data.devInfo);
      dispFSM.next_state = dispFSM.return_state;
      break;
    }
    case DISP_EVENT_SD_CARD_CONFIG_ERROR:
    {
      vHalDisplay_drawTwoLines("Cfg error!", "No web!", ONE_SEC_TIMEOUT, &data.sysStat, &data.devInfo);
      dispFSM.next_state = dispFSM.return_state;
      break;
    }
    case DISP_EVENT_SD_CARD_CONFIG_INS_DATA:
    {
      vHalDisplay_drawTwoLines("Done! Please", "insert data!", ONE_SEC_TIMEOUT, &data.sysStat, &data.devInfo);
      dispFSM.next_state = dispFSM.return_state;
      break;
    }
    case DISP_EVENT_SD_CARD_WRITE_DATA:
    {
      vHalDisplay_drawTwoLines("Error while", "writing SD Card!", ONE_SEC_TIMEOUT, &data.sysStat, &data.devInfo);
      dispFSM.next_state = dispFSM.return_state;
      break;
    }
    case DISP_EVENT_BME680_SENSOR_INIT:
    {
      vHalDisplay_drawTwoLines("Detecting BME680...", "", ONE_SEC_TIMEOUT, &data.sysStat, &data.devInfo);
      dispFSM.next_state = dispFSM.return_state;
      break;
    }
    case DISP_EVENT_BME680_SENSOR_OKAY:
    {
      vHalDisplay_drawTwoLines("Detecting BME680...", "BME680 -> Ok!", ONE_SEC_TIMEOUT, &data.sysStat, &data.devInfo);
      dispFSM.next_state = dispFSM.return_state;
      break;
    }
    case DISP_EVENT_BME680_SENSOR_ERR:
    {
      vHalDisplay_drawTwoLines("Detecting BME680...", "BME680 -> Err!", ONE_SEC_TIMEOUT, &data.sysStat, &data.devInfo);
      dispFSM.next_state = dispFSM.return_state;
      break;
    }
    case DISP_EVENT_PMS5003_SENSOR_INIT:
    {
      vHalDisplay_drawTwoLines("Detecting PMS5003...", "", ONE_SEC_TIMEOUT, &data.sysStat, &data.devInfo);
      dispFSM.next_state = dispFSM.return_state;
      break;
    }
    case DISP_EVENT_PMS5003_SENSOR_OKAY:
    {
      vHalDisplay_drawTwoLines("Detecting PMS5003...", "PMS5003 -> Ok!", ONE_SEC_TIMEOUT, &data.sysStat, &data.devInfo);
      dispFSM.next_state = dispFSM.return_state;
      break;
    }
    case DISP_EVENT_PMS5003_SENSOR_ERR:
    {
      vHalDisplay_drawTwoLines("Detecting PMS5003...", "PMS5003 -> Err!", ONE_SEC_TIMEOUT, &data.sysStat, &data.devInfo);
      dispFSM.next_state = dispFSM.return_state;
      break;
    }
    case DISP_EVENT_MICSxx14_SENSOR_INIT:
    {
      vHalDisplay_drawTwoLines("Detecting MICSxx14...", "", ONE_SEC_TIMEOUT, &data.sysStat, &data.devInfo);
      dispFSM.next_state = dispFSM.return_state;
      break;
    }
    case DISP_EVENT_MICSxx14_SENSOR_OKAY:
    {
      vHalDisplay_drawTwoLines("Detecting MICSxx14...", "MICSxx14 -> Ok!", ONE_SEC_TIMEOUT, &data.sysStat, &data.devInfo);
      dispFSM.next_state = dispFSM.return_state;
      break;
    }
    case DISP_EVENT_MICSxx14_VALUES_OKAY:
    {
      vHalDisplay_drawLine("MICSxx14 values OK!", ONE_SEC_TIMEOUT, &data.sysStat, &data.devInfo);
      dispFSM.next_state = dispFSM.return_state;
      break;
    }
    case DISP_EVENT_MICSxx14_DEF_SETTING:
    {
      vHalDisplay_drawLine("Setting MICSxx14...", ONE_SEC_TIMEOUT, &data.sysStat, &data.devInfo);
      dispFSM.next_state = dispFSM.return_state;
      break;
    }
    case DISP_EVENT_MICSxx14_HEATING_UP:
    {
      vHalDisplay_drawLine("Heating MICSxx14...", ONE_SEC_TIMEOUT, &data.sysStat, &data.devInfo);
      dispFSM.next_state = dispFSM.return_state;
      break;
    }
    case DISP_EVENT_MICSxx14_DONE:
    {
      vHalDisplay_drawLine("Done!", ONE_SEC_TIMEOUT, &data.sysStat, &data.devInfo);
      dispFSM.next_state = dispFSM.return_state;
      break;
    }
    case DISP_EVENT_MICSxx14_SENSOR_ERR:
    {
      vHalDisplay_drawTwoLines("Detecting MICSxx14...", "MICSxx14 -> Err!", ONE_SEC_TIMEOUT, &data.sysStat, &data.devInfo);
      dispFSM.next_state = dispFSM.return_state;
      break;
    }
    case DISP_EVENT_O3_SENSOR_INIT:
    {
      vHalDisplay_drawTwoLines("Detecting O3...", "", ONE_SEC_TIMEOUT, &data.sysStat, &data.devInfo);
      dispFSM.next_state = dispFSM.return_state;
      break;
    }
    case DISP_EVENT_O3_SENSOR_OKAY:
    {
      vHalDisplay_drawTwoLines("Detecting O3...", "O3 -> Ok!", ONE_SEC_TIMEOUT, &data.sysStat, &data.devInfo);
      dispFSM.next_state = dispFSM.return_state;
      break;
    }
    case DISP_EVENT_O3_SENSOR_ERR:
    {
      vHalDisplay_drawTwoLines("Detecting O3...", "O3 -> Err!", ONE_SEC_TIMEOUT, &data.sysStat, &data.devInfo);
      dispFSM.next_state = dispFSM.return_state;
      break;
    }
    // loop cases
    case DISP_EVENT_WAIT_FOR_NETWORK_CONN:
    {
      vHalDisplay_drawTwoLines("Network", "Wait for connection", ONE_SEC_TIMEOUT, &data.sysStat, &data.devInfo);
      dispFSM.next_state = dispFSM.return_state;
      break;
    }
    case DISP_EVENT_NETWORK_CONN_FAIL:
    {
      vHalDisplay_drawTwoLines("Network Error", "Failed to connect", ONE_SEC_TIMEOUT, &data.sysStat, &data.devInfo);
      dispFSM.next_state = dispFSM.return_state;
      break;
    }
    case DISP_EVENT_READING_SENSORS:
    {
      vHalDisplay_drawTwoLines("Timeout Expired", "Reading Sensors", ONE_SEC_TIMEOUT, &data.sysStat, &data.devInfo);
      dispFSM.next_state = dispFSM.return_state;
      break;
    }
    case DISP_EVENT_WAIT_FOR_TIMEOUT:
    {
      vHalDisplay_drawTwoLines("Waiting For", "Timeout...", ONE_SEC_TIMEOUT, &data.sysStat, &data.devInfo);
      dispFSM.next_state = dispFSM.return_state;
      break;
    }
    case DISP_EVENT_PREHEAT_STAT:
    {
      // vHalDisplay_drawCountdown(PMS_PREHEAT_TIME_IN_SEC, "Preheating PMS5003...",&data.sysStat,&data.devInfo);
      dispFSM.next_state = dispFSM.return_state;
      break;
    }
    case DISP_EVENT_MEAS_IN_PROGRESS:
    {
      vHalDisplay_drawTwoLines("Measurements", "in progress...", ONE_SEC_TIMEOUT, &data.sysStat, &data.devInfo);
      dispFSM.next_state = dispFSM.return_state;
      break;
    }
    case DISP_EVENT_SENDING_MEAS:
    {
      vHalDisplay_drawTwoLines("All measurements", "obtained, sending...", ONE_SEC_TIMEOUT, &data.sysStat, &data.devInfo);
      dispFSM.next_state = dispFSM.return_state;
      break;
    }
    case DISP_EVENT_SYSTEM_ERROR:
    {
      vHalDisplay_drawTwoLines("System in error!", "Waiting for reset...", RESET_TIMEOUT, &data.sysStat, &data.devInfo);
      dispFSM.next_state = dispFSM.return_state;
      break;
    }
    case DISP_EVENT_FOTA_IN_PROGRESS:
    {
      vHalDisplay_drawTwoLines("Firmware Update", "In Progress...", ONE_SEC_TIMEOUT, &data.sysStat, &data.devInfo);
      dispFSM.next_state = dispFSM.return_state;
      break;
    }
    // network cases
    case DISP_EVENT_CONN_TO_WIFI:
    {
      vHalDisplay_drawTwoLines("Connecting to", "WiFi...", ONE_SEC_TIMEOUT, &data.sysStat, &data.devInfo);
      dispFSM.next_state = dispFSM.return_state;
      break;
    }
    case DISP_EVENT_CONN_TO_GPRS:
    {
      vHalDisplay_drawTwoLines("Connecting to", "GPRS...", ONE_SEC_TIMEOUT, &data.sysStat, &data.devInfo);
      dispFSM.next_state = dispFSM.return_state;
      break;
    }
    case DISP_EVENT_RETRIEVE_DATETIME:
    {
      vHalDisplay_drawTwoLines("Getting date&time...", "Please wait...", ONE_SEC_TIMEOUT, &data.sysStat, &data.devInfo);
      dispFSM.next_state = dispFSM.return_state;
      break;
    }
    case DISP_EVENT_DATETIME_OK:
    {
      vHalDisplay_drawTwoLines("Getting date&time...", "OK!", ONE_SEC_TIMEOUT, &data.sysStat, &data.devInfo);
      dispFSM.next_state = dispFSM.return_state;
      break;
    }
    case DISP_EVENT_DATETIME:
    {
      vHalDisplay_drawTwoLines("Date & Time:", data.sysData.currentDataTime.c_str(), ONE_SEC_TIMEOUT, &data.sysStat, &data.devInfo);
      dispFSM.next_state = dispFSM.return_state;
      break;
    }
    case DISP_EVENT_DATETIME_ERR:
    {
      vHalDisplay_drawTwoLines("Date & time err!", "Is internet ok?", ONE_SEC_TIMEOUT, &data.sysStat, &data.devInfo);
      dispFSM.next_state = dispFSM.return_state;
      break;
    }
    case DISP_EVENT_WIFI_CONNECTED:
    {
      dispFSM.next_state = dispFSM.return_state;
      break;
    }
    case DISP_EVENT_WIFI_DISCONNECTED:
    {
      vHalDisplay_drawLine("WiFi connect err!", ONE_SEC_TIMEOUT, &data.sysStat, &data.devInfo);
      dispFSM.next_state = dispFSM.return_state;
      break;
    }
    case DISP_EVENT_SSID_NOT_FOUND:
    {
      vHalDisplay_drawLine(data.devInfo.noNet.c_str(), ONE_SEC_TIMEOUT, &data.sysStat, &data.devInfo);
      dispFSM.next_state = dispFSM.return_state;
      break;
    }
    case DISP_EVENT_NO_NETWORKS_FOUND:
    {
      vHalDisplay_drawLine("No networks found!", ONE_SEC_TIMEOUT, &data.sysStat, &data.devInfo);
      dispFSM.next_state = dispFSM.return_state;
      break;
    }
    case DISP_EVENT_CONN_RETRY:
    {
      vHalDisplay_drawTwoLines("Retrying...", data.devInfo.remain.c_str(), ONE_SEC_TIMEOUT, &data.sysStat, &data.devInfo);
      dispFSM.next_state = dispFSM.return_state;
      break;
    }
    case DISP_EVENT_NO_INTERNET:
    {
      vHalDisplay_drawLine("No internet!", ONE_SEC_TIMEOUT, &data.sysStat, &data.devInfo);
      dispFSM.next_state = dispFSM.return_state;
      break;
    }

    // modem
    case DISP_EVENT_SIM_ERROR:
    {
      vHalDisplay_drawTwoLines("ERROR:", "NO SIM!", ONE_SEC_TIMEOUT, &data.sysStat, &data.devInfo);
      dispFSM.next_state = dispFSM.return_state;
      break;
    }
    case DISP_EVENT_NETWORK_ERROR:
    {
      vHalDisplay_drawTwoLines("ERROR:", "NO NETWORK!", ONE_SEC_TIMEOUT, &data.sysStat, &data.devInfo);
      dispFSM.next_state = dispFSM.return_state;
      break;
    }
    case DISP_EVENT_GPRS_ERROR:
    {
      vHalDisplay_drawTwoLines("ERROR:", "NO GPRS!", ONE_SEC_TIMEOUT, &data.sysStat, &data.devInfo);
      dispFSM.next_state = dispFSM.return_state;
      break;
    }
    // measurement data
    case DISP_EVENT_SHOW_MEAS_DATA:
    {
      vHalDisplay_drawBme680GasSensorData(&data.sensorData, &data.sysStat, &data.devInfo, THREE_SEC_TIMEOUT);
      vHalDisplay_drawPMS5003AirQualitySensorData(&data.sensorData, &data.sysStat, &data.devInfo, THREE_SEC_TIMEOUT);
      vHalDisplay_drawMICSxx14PollutionSensorData(&data.sensorData, &data.sysStat, &data.devInfo, THREE_SEC_TIMEOUT);
      vHalDisplay_drawOzoneSensorData(&data.sensorData, &data.sysStat, &data.devInfo, THREE_SEC_TIMEOUT);
      vHalDisplay_drawMspIndexData(&data.sensorData, &data.sysStat, &data.devInfo, THREE_SEC_TIMEOUT);
      vHalDisplay_drawFirmwareVersion(&data.sysStat, &data.devInfo, THREE_SEC_TIMEOUT);
      vHalDisplay_drawConfigurationValues(&data.sensorData, &data.sysStat, &data.devInfo, &data.measStat, THREE_SEC_TIMEOUT);
      dispFSM.next_state = dispFSM.return_state;
      break;
    }
    // default case
    default:
      dispFSM.current_state = dispFSM.next_state = dispFSM.return_state;
    }
    dispFSM.current_state = dispFSM.next_state;
  }
}

/******************************************************
 * @brief   Function to create the display task.
 *
 ******************************************************/
void vTaskDisplay_createTask(void)
{
  displayTaskHandle = xTaskCreateStaticPinnedToCore(
      displayTask,             // Task function
      "displayTask",           // Name
      DISPLAY_TASK_STACK_SIZE, // Stack size
      NULL,                    // Parameters
      DISPLAY_TASK_PRIORITY,   // Priority
      displayTaskStack,        // Stack buffer
      &displayTaskBuffer,      // Task buffer
      1                        // Core 1
  );
}
