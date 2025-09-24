/*******************************************************************************
 * @file    display_task.h
 * @author  AB-Engineering - https://ab-engineering.it
 * @brief
 * @version 0.1
 * @date    2025-07-25
 *
 * @copyright Copyright (c) 2025
 *
 *******************************************************************************/

#ifndef DISPLAY_TASKS_H
#define DISPLAY_TASKS_H

// -- includes --
#include "shared_values.h"
#include "freertos/portmacro.h"

// -- display events --
typedef enum _DISPLAY_EVENTS_
{
  DISP_EVENT_WAIT_FOR_EVENT,
  // set up cases
  DISP_EVENT_DEVICE_BOOT,
  DISP_EVENT_WIFI_MAC_ADDR,
  DISP_EVENT_SHOW_MEAS_DATA,

  DISP_EVENT_SD_CARD_INIT,
  DISP_EVENT_CONFIG_READ,
  DISP_EVENT_URL_UPLOAD_STAT,
  DISP_EVENT_SD_CARD_NOT_PRESENT,
  DISP_EVENT_SD_CARD_FORMAT,
  DISP_EVENT_SD_CARD_LOG_ERROR,
  DISP_EVENT_SD_CARD_CONFIG_CREATE,
  DISP_EVENT_SD_CARD_CONFIG_ERROR,
  DISP_EVENT_SD_CARD_CONFIG_INS_DATA,
  DISP_EVENT_SD_CARD_WRITE_DATA,

  DISP_EVENT_BME680_SENSOR_INIT,
  DISP_EVENT_BME680_SENSOR_OKAY,
  DISP_EVENT_BME680_SENSOR_ERR,

  DISP_EVENT_PMS5003_SENSOR_INIT,
  DISP_EVENT_PMS5003_SENSOR_OKAY,
  DISP_EVENT_PMS5003_SENSOR_ERR,

  DISP_EVENT_MICS6814_SENSOR_INIT,
  DISP_EVENT_MICS6814_SENSOR_OKAY,
  DISP_EVENT_MICS6814_VALUES_OKAY,
  DISP_EVENT_MICS6814_DEF_SETTING,
  DISP_EVENT_MICS6814_DONE,
  DISP_EVENT_MICS6814_SENSOR_ERR,

  DISP_EVENT_O3_SENSOR_INIT,
  DISP_EVENT_O3_SENSOR_OKAY,
  DISP_EVENT_O3_SENSOR_ERR,

  // loop cases
  DISP_EVENT_WAIT_FOR_NETWORK_CONN,
  DISP_EVENT_NETWORK_CONN_FAIL,
  DISP_EVENT_READING_SENSORS,
  DISP_EVENT_WAIT_FOR_TIMEOUT,
  DISP_EVENT_PREHEAT_STAT,
  DISP_EVENT_MEAS_IN_PROGRESS,
  DISP_EVENT_SENDING_MEAS,
  DISP_EVENT_SYSTEM_ERROR,
  DISP_EVENT_FOTA_IN_PROGRESS,

  // network cases
  DISP_EVENT_CONN_TO_WIFI,
  DISP_EVENT_CONN_TO_GPRS,
  DISP_EVENT_RETREIVE_DATETIME,
  DISP_EVENT_DATETIME_OK,
  DISP_EVENT_DATETIME,
  DISP_EVENT_DATETIME_ERR,

  // wi-fi
  DISP_EVENT_WIFI_CONNECTED,
  DISP_EVENT_WIFI_DISCONNECTED,
  DISP_EVENT_SSID_NOT_FOUND,
  DISP_EVENT_NO_NETWORKS_FOUND,
  DISP_EVENT_CONN_RETRY,
  DISP_EVENT_NO_INTERNET,

  // modem
  DISP_EVENT_SIM_ERROR,
  DISP_EVENT_NETWORK_ERROR,
  DISP_EVENT_GPRS_ERROR,

} displayEvents_t;

// -- display task queue data
typedef struct _DISP_TASK_DATA_
{
  displayEvents_t currentEvent; /*!< Current event to be displayed */
  sensorData_t sensorData;      /*!< Sensor data to be displayed */
  systemStatus_t sysStat;       /*!< System status to be displayed */
  deviceNetworkInfo_t devInfo;  /*!< Device network information to be displayed */
  systemData_t sysData;         /*!< System data to be displayed */
  deviceMeasurement_t measStat; /*!< Device measurement status to be displayed */
} displayData_t;

/*********************************************************
 * @brief function to initialize the display task queue.
 *
 *********************************************************/
void vTaskDisplay_initDataQueue(void);

/******************************************************
 * @brief   Function to create the display task.
 *
 ******************************************************/
void vTaskDisplay_createTask(void);

/******************************************************************
 * @brief function to send display events and data to the queue.
 *
 * @param data
 * @return BaseType_t
 ******************************************************************/
BaseType_t tTaskDisplay_sendEvent(displayData_t *data);

/**********************************************************************
 * @brief function to receive display events and data from the queue.
 *
 * @param data
 * @param xTicksToWait
 * @return BaseType_t
 **********************************************************************/
BaseType_t tTaskDisplay_receiveEvent(displayData_t *data, TickType_t xTicksToWait);

// ===== Configuration Macros =====

// Display task configuration
#ifndef DISPLAY_TASK_STACK_SIZE
#define DISPLAY_TASK_STACK_SIZE (8 * 1024)
#endif

#ifndef DISPLAY_TASK_PRIORITY
#define DISPLAY_TASK_PRIORITY 1
#endif

#endif