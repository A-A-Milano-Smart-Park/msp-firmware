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
#include "time.h"
#include <type_traits>

//--------------------------------------------------------------------------------------------------
//------------------ DISPLAY TASK SECTION ----------------------------------------------------------
//--------------------------------------------------------------------------------------------------

// -- defines --
// Task configuration - public values defined in display_task.h
#define DISP_QUEUE_LENGTH 20 // Internal queue configuration
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

// xQueueSend/xQueueReceive copy items byte by byte: anything with an owning
// pointer (String) would arrive dangling.
static_assert(std::is_trivially_copyable<displayData_t>::value,
              "displayData_t travels through a FreeRTOS queue: keep it free of String and other owning types");

// -- display data instance --
static displayData_t data{};
// Preserved in BSS (no stack cost).
// updateDisplayStatus() only populates devInfo + sysStat; it zero-inits
// sensorData, measStat and sysData. These statics carry the last values
// supplied by vMsp_updateDataAndSendEvent so network-only events cannot
// wipe them out.
static sensorData_t       lastValidSensorData{};
static deviceMeasurement_t lastValidMeasStat{};
static dispSysData_t       lastValidSysData{};

#define EVENT_WAIT_TIMEOUT 100 // short so GPIO0 pause/resume reacts quickly
#define RESET_TIMEOUT 10

// -- measurement page navigation (GPIO0 / boot button) --
#define MEAS_PAGE_BUTTON_PIN 0
#define MEAS_PAGE_DEBOUNCE_MS 200
#define MEAS_PAGE_SCROLL_MS 3000 // auto-scroll dwell time per measurement page

// measurement pages cycled by GPIO0; MSP1 slots skipped when sensor absent
typedef enum
{
  MEAS_PAGE_BME680 = 0,
  MEAS_PAGE_PMS5003,
  MEAS_PAGE_MICSXX14,
  MEAS_PAGE_OZONE,
  MEAS_PAGE_MSP_INDEX,
  MEAS_PAGE_MSP1_ADC_SENSOR,
  MEAS_PAGE_MSP1_HEATER_DAC,
  MEAS_PAGE_MSP1_RNO2,
  MEAS_PAGE_FIRMWARE_VERSION,
  MEAS_PAGE_DATETIME,
  MEAS_PAGE_CONFIG_VALUES,
  MEAS_PAGE_COUNT
} measPage_t;

// contiguous MSP1 page block, skipped as a whole when the sensor is absent
#define MSP1_PAGE_SPAN (MEAS_PAGE_MSP1_RNO2 - MEAS_PAGE_MSP1_ADC_SENSOR + 1)

static volatile uint32_t buttonPressCount = 0;

static void IRAM_ATTR vMeasPageButtonIsr(void)
{
  static uint32_t lastPressTick = 0;
  uint32_t nowTick = xTaskGetTickCountFromISR();
  if ((nowTick - lastPressTick) >= pdMS_TO_TICKS(MEAS_PAGE_DEBOUNCE_MS))
  {
    lastPressTick = nowTick;
    buttonPressCount++;
  }
}

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

  TickType_t eventWaitTimeout = pdMS_TO_TICKS(EVENT_WAIT_TIMEOUT);
  displayEvents_t displayEvents = DISP_EVENT_WAIT_FOR_EVENT;

  // GPIO0 (boot button) cycles the measurement pages
  pinMode(MEAS_PAGE_BUTTON_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(MEAS_PAGE_BUTTON_PIN), vMeasPageButtonIsr, FALLING);

  uint32_t lastButtonPressCount = 0;
  uint8_t measPage = 0;
  bool measPageDirty = true; // redraw only on page change, new data, or after another event
  bool autoScroll = true;    // GPIO0 toggles: pause on current page / resume scrolling
  TickType_t lastPageChangeTick = xTaskGetTickCount();

  while (1)
  {
    switch (dispFSM.current_state)
    {
    case DISP_EVENT_WAIT_FOR_EVENT:
    {
      if (pdTRUE != tTaskDisplay_receiveEvent(&data, eventWaitTimeout))
      {
        uint32_t presses = buttonPressCount;
        if (presses != lastButtonPressCount)
        {
          lastButtonPressCount = presses;
          // Consume without toggling until the first measurement page is up:
          // attachInterrupt() on GPIO0 can latch a spurious FALLING edge at
          // boot (ESP32 quirk), which paused the carousel before it was ever
          // visible — it then looked like it needed a press to start. The
          // button only pauses/resumes a carousel the user can actually see.
          if (dispFSM.isFirstTransition == false)
          {
            autoScroll = !autoScroll; // GPIO0: pause on current page / resume scrolling
            lastPageChangeTick = xTaskGetTickCount();
          }
        }
        if (autoScroll && ((xTaskGetTickCount() - lastPageChangeTick) >= pdMS_TO_TICKS(MEAS_PAGE_SCROLL_MS)))
        {
#if defined(MSP1_DEBUG_ENABLE)
          uint8_t numPages = MSP1_PAGE_SPAN; // debug mode: only the MSP1 pages
#else
          // MSP1 replaces the MICS page; without MSP1 its page block is hidden
          uint8_t numPages = (data.sysStat.gasSensorType == GAS_SENSOR_MSP1) ? (MEAS_PAGE_COUNT - 1) : (MEAS_PAGE_COUNT - MSP1_PAGE_SPAN);
#endif
          measPage = (measPage + 1) % numPages;
          lastPageChangeTick = xTaskGetTickCount();
          measPageDirty = true;
        }
        // check if we have received the first data
        if ((false == dispFSM.isFirstTransition) && measPageDirty)
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
        if (data.hasSensorData)
        {
          // Full event from vMsp_updateDataAndSendEvent — save both fields.
          lastValidSensorData = data.sensorData;
          lastValidMeasStat   = data.measStat;
        }
        else
        {
          // Network-only event (updateDisplayStatus) — restore the fields it
          // does not populate so they are never overwritten with zeros.
          data.sensorData = lastValidSensorData;
          data.measStat   = lastValidMeasStat;
        }

        // sysData is tracked separately: updateDisplayStatus() takes an optional
        // sysData and DISP_EVENT_DATETIME_OK passes the freshly NTP-synced
        // Date/Time through it. Restoring it from lastValidSysData just because
        // the event carried no sensor data threw that Date/Time away and drew
        // the empty strings the display had at boot.
        if (data.hasSysData)
        {
          lastValidSysData = data.sysData;
        }
        else
        {
          data.sysData = lastValidSysData;
        }

        displayEvents = data.currentEvent;
        dispFSM.next_state = displayEvents;
        if (displayEvents == DISP_EVENT_SHOW_MEAS_DATA)
        {
          dispFSM.isFirstTransition = false;
        }
        measPageDirty = true; // any event repaints the screen: restore meas page after it
      }
      break;
    }
    // set up cases
    case DISP_EVENT_DEVICE_BOOT:
    {
      vHalDisplay_DrawBoot(data.sysData.ver, FIVE_SEC_TIMEOUT);
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
    case DISP_EVENT_MSP1_SENSOR_INIT:
    {
      vHalDisplay_drawTwoLines("Detecting MSP1...", "", ONE_SEC_TIMEOUT, &data.sysStat, &data.devInfo);
      dispFSM.next_state = dispFSM.return_state;
      break;
    }
    
    case DISP_EVENT_MSP1_SENSOR_OKAY:
    {
      vHalDisplay_drawTwoLines("Detecting MSP1...", "MSP1 -> Ok!", ONE_SEC_TIMEOUT, &data.sysStat, &data.devInfo);
      dispFSM.next_state = dispFSM.return_state;
      break;
    }

      case DISP_EVENT_MSP1_SENSOR_ERR:
    {
      vHalDisplay_drawTwoLines("Detecting MSP1...", "MSP1 -> Err!", ONE_SEC_TIMEOUT, &data.sysStat, &data.devInfo);
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
      vHalDisplay_drawTwoLines(data.sysData.Date, data.sysData.Time, ONE_SEC_TIMEOUT, &data.sysStat, &data.devInfo);
      dispFSM.next_state = dispFSM.return_state;
      break;
    }
    case DISP_EVENT_DATETIME:
    {
      vHalDisplay_drawTwoLines("Date & Time:", data.sysData.currentDataTime, ONE_SEC_TIMEOUT, &data.sysStat, &data.devInfo);
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
      vHalDisplay_drawLine(data.devInfo.noNet, ONE_SEC_TIMEOUT, &data.sysStat, &data.devInfo);
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
      vHalDisplay_drawTwoLines("Retrying...", data.devInfo.remain, ONE_SEC_TIMEOUT, &data.sysStat, &data.devInfo);
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
#if defined(MSP1_DEBUG_ENABLE)
      // Debug mode: only the MSP1 pages, cycled by GPIO0.
      measPage_t page = (measPage_t)(MEAS_PAGE_MSP1_ADC_SENSOR + measPage);
#else
      // Static page; zero delay keeps the screen fixed until next redraw.
      measPage_t page = (measPage_t)measPage;
      if (data.sysStat.gasSensorType == GAS_SENSOR_MSP1)
      {
        if (page >= MEAS_PAGE_MICSXX14)
        {
          page = (measPage_t)(page + 1); // skip the MICS page: MSP1 replaces it
        }
      }
      else if (page >= MEAS_PAGE_MSP1_ADC_SENSOR)
      {
        page = (measPage_t)(page + MSP1_PAGE_SPAN); // skip the MSP1 page block
      }
      // Hard guard: MSP1 pages are shown only when gasSensorType == GAS_SENSOR_MSP1,
      // regardless of how the page mapping above evolves.
      if ((data.sysStat.gasSensorType != GAS_SENSOR_MSP1) &&
          (page >= MEAS_PAGE_MSP1_ADC_SENSOR) && (page <= MEAS_PAGE_MSP1_RNO2))
      {
        page = (measPage_t)(MEAS_PAGE_MSP1_RNO2 + 1);
      }
#endif
      // Until the first SYS_STATE_READ_SENSORS completes, sensorData carries
      // the boot zeros, not readings: the carousel starts on the first
      // WAIT_FOR_TIMEOUT pass, before the first measurement. Clearing status
      // draws every sensor-value page as "--" instead of 0. The MSP1 pages
      // read live values from the MSP1 module and have their own "n/a".
      sensorData_t *p_tSensor = &data.sensorData;
      sensorData_t tNoData;
      if (!data.measStat.isSensorDataAvailable)
      {
        tNoData = data.sensorData;
        tNoData.status = {};
        tNoData.fault = {};
        tNoData.MSP = 0;
        p_tSensor = &tNoData;
      }
      switch (page)
      {
      case MEAS_PAGE_BME680:
        vHalDisplay_drawBme680GasSensorData(p_tSensor, &data.sysStat, &data.devInfo, 0);
        break;
      case MEAS_PAGE_PMS5003:
        vHalDisplay_drawPMS5003AirQualitySensorData(p_tSensor, &data.sysStat, &data.devInfo, 0);
        break;
      case MEAS_PAGE_MICSXX14:
        vHalDisplay_drawMICSxx14PollutionSensorData(p_tSensor, &data.sysStat, &data.devInfo, 0);
        break;
      case MEAS_PAGE_OZONE:
        vHalDisplay_drawOzoneSensorData(p_tSensor, &data.sysStat, &data.devInfo, 0);
        break;
      case MEAS_PAGE_MSP_INDEX:
        vHalDisplay_drawMspIndexData(p_tSensor, &data.sysStat, &data.devInfo, 0);
        break;
      case MEAS_PAGE_MSP1_ADC_SENSOR:
        vHalDisplay_drawMsp1AdcSensorData(&data.sensorData, &data.sysStat, &data.devInfo, 0);
        break;
      case MEAS_PAGE_MSP1_HEATER_DAC:
        vHalDisplay_drawMsp1HeaterDacData(&data.sensorData, &data.sysStat, &data.devInfo, 0);
        break;
      case MEAS_PAGE_MSP1_RNO2:
        vHalDisplay_drawMsp1Rno2Data(&data.sensorData, &data.sysStat, &data.devInfo, 0);
        break;
      case MEAS_PAGE_FIRMWARE_VERSION:
        vHalDisplay_drawFirmwareVersion(&data.sysStat, &data.devInfo, 0);
        break;
      case MEAS_PAGE_DATETIME:
        vHalDisplay_drawCurrentDateTime(&data.sysStat, &data.devInfo, 0);
        break;
      case MEAS_PAGE_CONFIG_VALUES:
      default:
        vHalDisplay_drawConfigurationValues(&data.sensorData, &data.sysStat, &data.devInfo, &data.measStat, 0);
        break;
      }
      measPageDirty = false;
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
