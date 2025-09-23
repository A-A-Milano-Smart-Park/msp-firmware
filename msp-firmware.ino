/*
                        Milano Smart Park Firmware
                       Developed by Norman Mulinacci

          This code is usable under the terms and conditions of the
             GNU GENERAL PUBLIC LICENSE Version 3, 29 June 2007
 */
// Select modem type - required before TinyGsmClient.h include
#define TINY_GSM_MODEM_SIM800

// Basic system libraries
#include <Arduino.h>
#include <FS.h>
#include <SD.h>

// Project configuration
#include "config.h"
#include <Wire.h>

#include <stdint.h>
#include <stdio.h>

// WiFi Client, NTP time management, Modem and SSL libraries
#include <WiFi.h>
#include "time.h"
#include "esp_sntp.h"
#include <TinyGsmClient.h>
#include <SSLClient.h>

// Sensors management libraries
// for BME_680
#include <bsec.h>
// for PMS5003
#include <PMS.h>
// for MICS6814
#include <MiCS6814-I2C.h>
#include "DFRobot_MICS.h" // MICS4514 sensor library

// OLED display library
#include <U8g2lib.h>

// --local dependency includes  --
#include "shared_values.h"
#include "display.h"
#include "sensors.h"
#include "network.h"
#include "generic_functions.h"
#include "sdcard.h"
#include "trust_anchor.h"
#include "display_task.h"
#include "mspOs.h"
#include "firmware_update.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"

// Hardware UART definitions. Modes: UART0=0(debug out); UART1=1; UART2=2

HardwareSerial pmsSerial(2);

// ------------------------------- DEFINES -----------------------------------------------------------------------------

// ------------------------------- INSTANCES  --------------------------------------------------------------------------

// -- BME680 sensor instance
static Bsec bme680;

// -- PMS5003 sensor instance
static PMS pms(pmsSerial);

// -- MICS4514 sensor constants (from DFRobot library)
#define MICS4514_WARMUP_TIME_MIN 3 // Minimum warmup time in minutes
#define MICS4514_I2C_ADDR 0x75     // Default I2C address

// -- MICS6814 sensors instances
static MiCS6814 gas;                                        // MICS6814 sensor instance
static DFRobot_MICS_I2C mics4514(&Wire, MICS4514_I2C_ADDR); // MICS4514 sensor instance

// -- Create a new state machine instance
static state_machine_t mainStateMachine;

// -- sensor data and status instance
static sensorData_t sensorData_accumulate;
static sensorData_t sensorData_single;

// -- bme 680 data instance
static bme680Data_t localData;

// -- reading status instance
static errorVars_t err;

// -- system status instance
static systemStatus_t sysStat;

// -- device network info instance
static deviceNetworkInfo_t devinfo;

// -- device measurement status
static deviceMeasurement_t measStat;

// -- time information data
static tm timeinfo;

// -- time zone and ntp server data
static systemData_t sysData;

// -- structure for PMS5003
static PMS::DATA data;

//---------------------------------------- FUNCTIONS ----------------------------------------------------------------------

void vMspInit_sensorStatusAndData(sensorData_t *p_tData);

void vMspInit_setDefaultNtpTimezoneStatus(systemData_t *p_tData);

void vMspInit_setDefaultSslName(systemData_t *p_tData);

void vMspInit_setApiSecSaltAndFwVer(systemData_t *p_tData);

void vMsp_setGpioPins(void);

void vMspInit_NetworkAndMeasInfo(void);
void vMspInit_MeasInfo(void);

void vMsp_scanI2CDevices(void);

void Msp_getSystemStatus(systemStatus_t *stat);

//*******************************************************************************************************************************

void Msp_getSystemStatus(systemStatus_t *stat)
{

  vMspOs_takeDataAccessMutex();

  memcpy(stat, &sysStat, sizeof(systemStatus_t));

  vMspOs_giveDataAccessMutex();
}

/******************************************************************************************************************/

//*******************************************************************************************************************************
//******************************************  S E T U P  ************************************************************************
//*******************************************************************************************************************************
void setup()
{
  memset(&mainStateMachine, 0, sizeof(mainStateMachine)); // Initialize the state machine structure
  memset(&sensorData_accumulate, 0, sizeof(sensorData_accumulate));
  memset(&sensorData_single, 0, sizeof(sensorData_single));
  sysData = systemData_t();

  vMsp_setGpioPins();

  vMspOs_initDataAccessMutex(); // Create the OS mutex for data access

  // init the sensor data structure /status / offset values with defualt values
  vMspInit_sensorStatusAndData(&sensorData_accumulate);

  // init device network parameters
  vMspInit_NetworkAndMeasInfo();

  // init the ntp server and timezone data with default values (network task will override from SD config)
  vMspInit_setDefaultNtpTimezoneStatus(&sysData);

  // Default server name for SSL data upload
  vMspInit_setDefaultSslName(&sysData);

  // set API Secret salt and FW Version
  vMspInit_setApiSecSaltAndFwVer(&sysData);

  // Initialize the serial port and I2C for the display
  vHalDisplay_initSerialAndI2c();

#ifdef ENABLE_I2C_SCANNER
  // Scan I2C bus for connected devices
  vMsp_scanI2CDevices();
#endif

  // Initialize the display task data queue
  vTaskDisplay_initDataQueue();

  // Create the display task
  vTaskDisplay_createTask();

  // BOOT STRINGS ++++++++++++++++++++++++++++++++++++++++++++++++++++++
  printf("\nMILANO SMART PARK\n");
  printf("FIRMWARE %s\n", sysData.ver.c_str());
  printf("First project by Norman Mulinacci\n");
  printf("Refactor and optimization by AB-Engineering - https://ab-engineering.it\n");
  printf("Compiled %s %s\n", __DATE__, __TIME__);

  vMsp_updateDataAndSendEvent(DISP_EVENT_DEVICE_BOOT, &sensorData_accumulate, &devinfo, &measStat, &sysData, &sysStat);

#ifdef VERSION_STRING
  log_i("ver was defined at compile time.\n");
#else
  log_i("ver is the default.\n");
#endif
#ifdef API_SECRET_SALT
  log_i("api_secret_salt was defined at compile time.\n");
#else
  log_i("api_secret_salt is the default.\n");
#endif
  //+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

  // STEP 0: OTA Firmware Validation and Management
  log_i("=== STEP 0: OTA Firmware Validation ===");
  vHalFirmware_printOTAInfo();

  // Validate current firmware after boot
  if (bHalFirmware_validateCurrentFirmware())
  {
    log_i("Current firmware validated successfully");
  }
  else
  {
    log_e("Current firmware validation failed");
    // Could implement rollback logic here if needed
  }

  // STEP 1: Single SD Card Configuration Reading
  log_i("=== STEP 1: Loading complete system configuration from SD card ===");
  vHalSdcard_readSD(&sysStat, &devinfo, &sensorData_accumulate, &measStat, &sysData);

  // Phase 2: Check for downloaded firmware and apply update if newer
  if (SD.exists("/firmware.bin"))
  {
    log_i("Downloaded firmware file found - checking for update");
    if (bHalFirmware_checkAndApplyPendingUpdate("/firmware.bin"))
    {
      log_i("Firmware update applied successfully");
    }
    else
    {
      log_w("No firmware update needed or update failed - cleaning up file");
    }
    // Always remove firmware file after processing
    if (SD.remove("/firmware.bin"))
    {
      log_i("Firmware file cleaned up successfully");
    }
    else
    {
      log_w("Failed to remove firmware.bin file");
    }
  }

  // STEP 2: Fill system configuration with SD data or defaults
  log_i("=== STEP 2: Configuring system with loaded data or defaults ===");
  vMspInit_configureSystemFromSD(&sysData, &sysStat, &devinfo, &measStat);

  // Debug: Check final configuration status
  log_i("Final Configuration Status:");
  log_i("  SD Card: %s", sysStat.sdCard ? "OK" : "FAILED");
  log_i("  Config File: %s", sysStat.configuration ? "OK" : "FAILED");
  log_i("  WiFi SSID: %s", devinfo.ssid.c_str());
  log_i("  Server: %s", sysData.server.c_str());
  log_i("  *** MEASUREMENT CONFIG: avg_measurements=%d, max_measurements=%d ***", measStat.avg_measurements, measStat.max_measurements);
  log_i("  Use Modem: %s", sysStat.use_modem ? "YES" : "NO");
  log_i("  Firmware Auto-Upgrade: %s", sysStat.fwAutoUpgrade ? "ENABLED" : "DISABLED");

  // Firmware update tests will be run after network connection is established

  // STEP 3: Start network task with complete configuration
  log_i("=== STEP 3: Starting network task ===");
  createNetworkEvents();
  initSendDataOp(&sysData, &sysStat, &devinfo);

  // Wait for network task to initialize
  vTaskDelay(pdMS_TO_TICKS(2000));

  measStat.max_measurements = measStat.avg_measurements; /*!< fill the max_measurements with the number set by the user */

#ifndef ENABLE_FIRMWARE_UPDATE_TESTS
  //++++++++++++++++ DETECT AND INIT SENSORS ++++++++++++++++++++++++++++++
  log_i("Detecting and initializing sensors...\n");

  // BME680 +++++++++++++++++++++++++++++++++++++
  vMsp_updateDataAndSendEvent(DISP_EVENT_BME680_SENSOR_INIT, &sensorData_accumulate, &devinfo, &measStat, &sysData, &sysStat);

  bme680.begin(BME68X_I2C_ADDR_HIGH, Wire);
  if (tHalSensor_checkBMESensor(&bme680))
  {
    log_i("BME680 sensor detected, initializing...\n");
    bsec_virtual_sensor_t sensor_list[] = {
        BSEC_OUTPUT_RAW_TEMPERATURE,
        BSEC_OUTPUT_RAW_PRESSURE,
        BSEC_OUTPUT_RAW_HUMIDITY,
        BSEC_OUTPUT_RAW_GAS,
        BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_TEMPERATURE,
        BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_HUMIDITY,
    };
    bme680.updateSubscription(sensor_list, sizeof(sensor_list) / sizeof(sensor_list[0]), BSEC_SAMPLE_RATE_LP);
    sensorData_accumulate.status.BME680Sensor = true;
    vMsp_updateDataAndSendEvent(DISP_EVENT_BME680_SENSOR_OKAY, &sensorData_accumulate, &devinfo, &measStat, &sysData, &sysStat);
  }
  else
  {
    log_e("BME680 sensor not detected!\n");
    vMsp_updateDataAndSendEvent(DISP_EVENT_BME680_SENSOR_ERR, &sensorData_accumulate, &devinfo, &measStat, &sysData, &sysStat);
  }
  //+++++++++++++++++++++++++++++++++++++++++++++

  // PMS5003 ++++++++++++++++++++++++++++++++++++
  pmsSerial.begin(9600, SERIAL_8N1, PMSERIAL_RX, PMSERIAL_TX); // baud, type, ESP_RX, ESP_TX
  delay(1500);
  vMsp_updateDataAndSendEvent(DISP_EVENT_PMS5003_SENSOR_INIT, &sensorData_accumulate, &devinfo, &measStat, &sysData, &sysStat);
  delay(1000);
  if (pms.readUntil(data))
  {
    log_i("PMS5003 sensor detected, initializing...\n");
    sensorData_accumulate.status.PMS5003Sensor = true;
    vMsp_updateDataAndSendEvent(DISP_EVENT_PMS5003_SENSOR_OKAY, &sensorData_accumulate, &devinfo, &measStat, &sysData, &sysStat);
    measStat.isPmsAwake = true;
  }
  else
  {
    log_e("PMS5003 sensor not detected!\n");
    vMsp_updateDataAndSendEvent(DISP_EVENT_PMS5003_SENSOR_ERR, &sensorData_accumulate, &devinfo, &measStat, &sysData, &sysStat);
  }
  //++++++++++++++++++++++++++++++++++++++++++++++

  // MICS6814 ++++++++++++++++++++++++++++++++++++
  vMsp_updateDataAndSendEvent(DISP_EVENT_MICS6814_SENSOR_INIT, &sensorData_accumulate, &devinfo, &measStat, &sysData, &sysStat);

  // Initialize gas sensor based on configuration type
  switch (sysStat.gasSensorType)
  {
  case GAS_SENSOR_MICS6814:
  {
    if (gas.begin())
    { // Connect to MICS6814 sensor using default I2C address (0x04)
      log_i("MICS6814 sensor detected, initializing...\n");
      sensorData_accumulate.status.MICS6814Sensor = true;
      gas.powerOn(); // turn on heating element and led
      gas.ledOn();
      vMsp_updateDataAndSendEvent(DISP_EVENT_MICS6814_SENSOR_OKAY, &sensorData_accumulate, &devinfo, &measStat, &sysData, &sysStat);

      sensorR0Value_t r0Values;
      r0Values.redSensor = gas.getBaseResistance(CH_RED);
      r0Values.oxSensor = gas.getBaseResistance(CH_OX);
      r0Values.nh3Sensor = gas.getBaseResistance(CH_NH3);

      if (tHalSensor_checkMicsValues(&sensorData_accumulate, &r0Values) == STATUS_OK)
      {
        log_i("MICS6814 R0 values are already as default!\n");
        vMsp_updateDataAndSendEvent(DISP_EVENT_MICS6814_VALUES_OKAY, &sensorData_accumulate, &devinfo, &measStat, &sysData, &sysStat);
      }
      else
      {
        log_i("Setting MICS6814 R0 values as default... ");
        vMsp_updateDataAndSendEvent(DISP_EVENT_MICS6814_DEF_SETTING, &sensorData_accumulate, &devinfo, &measStat, &sysData, &sysStat);

        vHalSensor_writeMicsValues(&sensorData_accumulate);

        vMsp_updateDataAndSendEvent(DISP_EVENT_MICS6814_DONE, &sensorData_accumulate, &devinfo, &measStat, &sysData, &sysStat);
        log_i("Done!\n");
      }
      gas.setOffsets(&sensorData_accumulate.micsTuningData.sensingResInAirOffset.redSensor);
    }
    else
    {
      log_e("MICS6814 sensor not detected!\n");
      vMsp_updateDataAndSendEvent(DISP_EVENT_MICS6814_SENSOR_ERR, &sensorData_accumulate, &devinfo, &measStat, &sysData, &sysStat);
    }
    break;
  }

  case GAS_SENSOR_MICS4514:
  {
    log_i("Initializing MICS4514 sensor (DFRobot SEN0377)...\n");

    if (mics4514.begin())
    { // Connect to MICS4514 sensor using I2C
      log_i("MICS4514 sensor detected, initializing...\n");
      sensorData_accumulate.status.MICS4514Sensor = true;

      mics4514.wakeUpMode();

      // Start sensor warmup process
      log_i("Starting MICS4514 warmup process (%d minutes)...\n", MICS4514_WARMUP_TIME_MIN);
      uint8_t loadingStep = 0;
      float delayInMs = 5000.0;
      while (mics4514.warmUpTime(MICS4514_WARMUP_TIME_MIN) == false)
      {
        // stay here, until the sensor get heaten up.
        printf("MIXS4514 - Heat-Up: %3.1f %%\n", (float)((loadingStep * delayInMs) / ((MICS4514_WARMUP_TIME_MIN * 60.0f * 1000.0f)) * 100.0f));
        vTaskDelay(pdMS_TO_TICKS((int32_t)delayInMs));
        loadingStep++;
      }

      // Initialize MICS4514 specific data
      sensorData_accumulate.mics4514Data.warmupComplete = 0;
      sensorData_accumulate.mics4514Data.powerState = 1;

      vMsp_updateDataAndSendEvent(DISP_EVENT_MICS6814_SENSOR_OKAY, &sensorData_accumulate, &devinfo, &measStat, &sysData, &sysStat);
      log_i("MICS4514 initialization complete!\n");
    }
    else
    {
      log_e("MICS4514 sensor not detected!\n");
      vMsp_updateDataAndSendEvent(DISP_EVENT_MICS6814_SENSOR_ERR, &sensorData_accumulate, &devinfo, &measStat, &sysData, &sysStat);
    }
    break;
  }

  default:
  {
    log_e("Unknown gas sensor type: %d\n", sysStat.gasSensorType);
    vMsp_updateDataAndSendEvent(DISP_EVENT_MICS6814_SENSOR_ERR, &sensorData_accumulate, &devinfo, &measStat, &sysData, &sysStat);
    break;
  }
  }
  //+++++++++++++++++++++++++++++++++++++++++++++++++++

  // O3 ++++++++++++++++++++++++++++++++++++++++++
  vMsp_updateDataAndSendEvent(DISP_EVENT_O3_SENSOR_INIT, &sensorData_accumulate, &devinfo, &measStat, &sysData, &sysStat);

  if (!tHalSensor_isAnalogO3Connected())
  {
    log_e("O3 sensor not detected!\n");
    vMsp_updateDataAndSendEvent(DISP_EVENT_O3_SENSOR_ERR, &sensorData_accumulate, &devinfo, &measStat, &sysData, &sysStat);
    sensorData_accumulate.status.O3Sensor = false;
  }
  else
  {
    log_i("O3 sensor detected, running...\n");
    sensorData_accumulate.status.O3Sensor = true;
    vMsp_updateDataAndSendEvent(DISP_EVENT_O3_SENSOR_OKAY, &sensorData_accumulate, &devinfo, &measStat, &sysData, &sysStat);
  }
#endif
  //+++++++++++++++++++++++++++++++++++++++++++++++++++++

  // Always use 1-minute intervals for measurements
  measStat.delay_between_measurements = SEC_IN_MIN;

  /*!< Reset measurement count */
  measStat.measurement_count = 0;
  measStat.avg_measurements = 0;
  measStat.data_transmitted = false; // Initialize transmission flag

  sysData.sent_ok = false;
  sysData.ntp_last_sync_day = -1; // Force initial NTP sync

  memset(sysData.Date, 0, sizeof(sysData.Date));
  memset(sysData.Time, 0, sizeof(sysData.Time));
  measStat.curr_minutes = 0;
  measStat.curr_seconds = 0;
  measStat.curr_total_seconds = 0;
  measStat.last_transmission_minute = -1; // Initialize to invalid minute

  // STEP 4: Request network connection and wait for NTP sync
  log_i("=== STEP 4: Requesting network connection and NTP sync ===");
  if ((sysStat.configuration == true) || (sysStat.server_ok == true))
  {
    log_i("Configuration available, requesting network connection...");
    // Request network connection (time sync will happen automatically)
    requestNetworkConnection();
    mainStateMachine.current_state = SYS_STATE_WAIT_FOR_NTP_SYNC; // Wait for NTP sync first
    mainStateMachine.next_state = SYS_STATE_WAIT_FOR_NTP_SYNC;
  }
  else
  {
    log_e("Device has no valid configuration, going into error mode!");
    mainStateMachine.current_state = SYS_STATE_ERROR; // set initial state to error if no config
    mainStateMachine.next_state = SYS_STATE_ERROR;    // set next state
  }

  //++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  mainStateMachine.isFirstTransition = 1; // set first transition flag

  // copy the whole sensorData_accumulate into sensorData_single
  memcpy(&sensorData_single, &sensorData_accumulate, sizeof(sensorData_t));

} // end of SETUP
//*******************************************************************************************************************************
//*******************************************************************************************************************************
//*******************************************************************************************************************************

//*******************************************************************************************************************************
//********************************************  L O O P  ************************************************************************
//*******************************************************************************************************************************
void loop()
{
  switch (mainStateMachine.current_state) // state machine for the main loop
  {
  case SYS_STATE_WAIT_FOR_NTP_SYNC:
  {
    log_i("Waiting for NTP synchronization...");
    if (mainStateMachine.isFirstTransition == true)
    {
      vMsp_updateDataAndSendEvent(DISP_EVENT_WAIT_FOR_NETWORK_CONN, &sensorData_accumulate, &devinfo, &measStat, &sysData, &sysStat);
      mainStateMachine.isFirstTransition = false;
    }

    // Wait for network connection and automatic time sync from network task
    if (pdTRUE == waitForNetworkEvent(NET_EVENT_TIME_SYNCED, pdMS_TO_TICKS(5000)))
    {
      log_i("NTP sync completed, starting measurement cycle");
      // Record the sync day for daily tracking
      if (getLocalTime(&timeinfo))
      {
        sysData.ntp_last_sync_day = timeinfo.tm_yday;
        sysStat.connection = true;
        log_i("NTP sync recorded for day %d", sysData.ntp_last_sync_day);
      }

#ifdef ENABLE_FIRMWARE_UPDATE_TESTS
      // Run actual firmware update check now that internet connection is established
      log_i("=== Running Firmware Update Test (Production Function) ===");
      log_i("Testing bHalFirmware_checkForUpdates() with actual parameters");

      if (bHalFirmware_checkForUpdates(&sysData, &sysStat, &devinfo))
      {
        log_i("Firmware update check completed successfully");
      }
      else
      {
        log_w("Firmware update check failed or no updates available");
      }

      // Test midnight behavior scenarios
      vHalFirmware_testMidnightBehavior(&sysData, &sysStat, &devinfo);

      log_i("=== Firmware Update Test Completed ===");
#endif

      mainStateMachine.next_state = SYS_STATE_WAIT_FOR_TIMEOUT;
      mainStateMachine.isFirstTransition = true;
      mainStateMachine.prev_state = SYS_STATE_WAIT_FOR_NTP_SYNC;
    }
    else
    {
      // Keep waiting - network task is still working on connection and sync
      log_v("Still waiting for NTP sync...");
      mainStateMachine.next_state = SYS_STATE_WAIT_FOR_NTP_SYNC;
    }
    break;
  }
    //------------------------------------------------------------------------------------------------------------------------
    //------------------------------------------------------------------------------------------------------------------------

  case SYS_STATE_WAIT_FOR_TIMEOUT:
  {
    // Periodic SD card presence check
    static unsigned long lastSdCheck = 0;
    if (millis() - lastSdCheck > 30000)
    { // Check every 30 seconds
      lastSdCheck = millis();
      vHalSdcard_periodicCheck(&sysStat, &devinfo);
    }

    if (getLocalTime(&timeinfo))
    {
      measStat.curr_minutes = timeinfo.tm_min;
      measStat.curr_seconds = timeinfo.tm_sec;
      measStat.curr_total_seconds = measStat.curr_minutes * SEC_IN_MIN + measStat.curr_seconds;

      // Calculate if we are exactly at the start of a minute (00 seconds)
      // We trigger measurement only when seconds == 0, ensuring exact minute alignment
      measStat.timeout_seconds = ((measStat.curr_total_seconds + measStat.additional_delay) % measStat.delay_between_measurements);

      // It is time for a measurement? If so continue, otherwise break.
      if (measStat.timeout_seconds != 0)
      {
        mainStateMachine.next_state = SYS_STATE_WAIT_FOR_TIMEOUT;
        break;
      }

      // It is time for a measurement - trigger at exactly 00 seconds of every minute
      if (measStat.timeout_seconds == 0)
      {
        log_i("Timeout expired!");
        log_i("Current time: %02d:%02d:%02d", timeinfo.tm_hour, measStat.curr_minutes, measStat.curr_seconds);

        // Only set first transition flag when coming from states that require reset
        if ((mainStateMachine.prev_state == SYS_STATE_WAIT_FOR_NTP_SYNC) || (mainStateMachine.prev_state == SYS_STATE_SEND_DATA))
        {
          mainStateMachine.isFirstTransition = true;
          log_i("Setting first transition flag for new measurement cycle");
        }
        mainStateMachine.prev_state = SYS_STATE_WAIT_FOR_TIMEOUT;
        mainStateMachine.next_state = SYS_STATE_READ_SENSORS; // go to read sensors state
      }
      else
      {
        // if it is not the first transition, wait for timeout
        if (measStat.isSensorDataAvailable == false)
        {
          vMsp_updateDataAndSendEvent(DISP_EVENT_WAIT_FOR_TIMEOUT, &sensorData_single, &devinfo, &measStat, &sysData, &sysStat);
          delay(500);
        }
      }
    }
    else
    {
      log_e("Failed to obtain time!");
      mainStateMachine.next_state = SYS_STATE_WAIT_FOR_TIMEOUT;
    }
    break;
  }
    //------------------------------------------------------------------------------------------------------------------------
    //------------------------------------------------------------------------------------------------------------------------

  case SYS_STATE_READ_SENSORS:
  {
    log_i("Reading sensors...");

    // Reset accumulation variables when starting a new measurement cycle
    // This happens when measurement_count is 0 (starting fresh cycle)
    if (measStat.measurement_count == 0)
    {
      log_i("Starting new measurement cycle - resetting accumulation variables");
      // Reset sensor accumulation variables for new measurement cycle
      sensorData_accumulate.gasData.temperature = 0.0f;
      sensorData_accumulate.gasData.pressure = 0.0f;
      sensorData_accumulate.gasData.humidity = 0.0f;
      sensorData_accumulate.gasData.volatileOrganicCompounds = 0.0f;
      sensorData_accumulate.airQualityData.particleMicron1 = 0.0f;
      sensorData_accumulate.airQualityData.particleMicron25 = 0.0f;
      sensorData_accumulate.airQualityData.particleMicron10 = 0.0f;
      sensorData_accumulate.pollutionData.carbonMonoxide = 0.0f;
      sensorData_accumulate.pollutionData.nitrogenDioxide = 0.0f;
      sensorData_accumulate.pollutionData.ammonia = 0.0f;
      sensorData_accumulate.ozoneData.ozone = 0.0f;

      // Reset MICS4514 ADC accumulator for new measurement cycle
      sensorData_accumulate.mics4514AdcAccumulator.oxVoltageSum = 0;
      sensorData_accumulate.mics4514AdcAccumulator.redVoltageSum = 0;

      // Calculate measurements needed to reach next transmission boundary
      // For max_measurements=5: boundaries at 0,5,10,15,20,25,30,35,40,45,50,55
      int minutes_to_next_boundary;
      int next_boundary_minute;

      // Calculate next boundary from current minute
      next_boundary_minute = ((measStat.curr_minutes / measStat.max_measurements) + 1) * measStat.max_measurements;

      // Handle hour wraparound (when next boundary >= 60)
      if (next_boundary_minute >= 60)
      {
        next_boundary_minute = 0; // Wrap to minute 00 of next hour
      }

      // Calculate measurements needed
      if (next_boundary_minute == 0)
      {
        // Special case: crossing hour boundary (e.g., 56->57->58->59->00)
        minutes_to_next_boundary = (60 - measStat.curr_minutes) + 1;
      }
      else
      {
        // Normal case within the hour
        minutes_to_next_boundary = next_boundary_minute - measStat.curr_minutes + 1;
      }

      measStat.avg_measurements = minutes_to_next_boundary;

      log_i("BOUNDARY CALCULATION: curr_minute=%d, max_measurements=%d, cycle_needs=%d, next_boundary=%d",
            measStat.curr_minutes, measStat.max_measurements, minutes_to_next_boundary, next_boundary_minute);

      if (measStat.avg_measurements == 0)
      {
        measStat.avg_measurements = 1; // If the number of measurements is 0, set it to 1
        // it means we are starting the measurement in the exact minute in which we want to send the data
      }

      err.BMEfails = 0;
      err.PMSfails = 0;
      err.MICSfails = 0;
      err.O3fails = 0;
      err.count = 0;

      // Note: measurement_count will be incremented after this sensor reading completes
      log_i("NEW MEASUREMENT CYCLE: Starting fresh with count %d (target: %d measurements)", measStat.measurement_count, measStat.avg_measurements);
    }

    // Prevent over-collection: if we already have enough measurements, skip sensor reading
    if (measStat.measurement_count >= measStat.avg_measurements)
    {
      log_i("Target measurements (%d) already reached, skipping sensor reading", measStat.avg_measurements);
      mainStateMachine.next_state = SYS_STATE_EVAL_SENSOR_STATUS;
      mainStateMachine.prev_state = SYS_STATE_READ_SENSORS;
      break;
    }

    log_i("=== READING SENSOR #%d (target: %d measurements) ===", measStat.measurement_count + 1, measStat.avg_measurements);

    vMsp_updateDataAndSendEvent(DISP_EVENT_READING_SENSORS, &sensorData_single, &devinfo, &measStat, &sysData, &sysStat);

    // READING BME680
    if (sensorData_accumulate.status.BME680Sensor)
    {
      log_i("Sampling BME680 sensor...");
      err.count = 0;
      // Attempt to read BME680 sensor with maximum retries
      bool sensor_read_success = false;
      for (int retry = 0; retry < MAX_SENSOR_RETRIES; retry++)
      {
        if (!tHalSensor_checkBMESensor(&bme680))
        {
          err.count++;
          log_w("BME680 sensor check failed, attempt %d/%d", retry + 1, MAX_SENSOR_RETRIES);
          if (retry == (MAX_SENSOR_RETRIES - 1)) // Last attempt failed
          {
            log_e("Error while sampling BME680 sensor after %d attempts!", MAX_SENSOR_RETRIES);
            err.BMEfails++;
            break;
          }
          delay(1000);
          continue;
        }

        // Trigger measurement if this is the first attempt
        if (retry == 0)
        {
          // Give BME680 time to prepare measurement (especially gas sensor)
          delay(500);
        }

        if (!bme680.run())
        {
          log_v("BME680 sensor not ready, waiting... (attempt %d/%d)", retry + 1, MAX_SENSOR_RETRIES);
          if (retry == (MAX_SENSOR_RETRIES - 1)) // Last attempt and still not ready
          {
            log_w("BME680 sensor not ready after %d attempts - measurement #%d will be excluded from averaging", MAX_SENSOR_RETRIES, measStat.measurement_count + 1);
            err.BMEfails++;
          }
          // Increase delay for BME680 gas measurement to complete (datasheet: 150-350ms)
          delay(300);
          continue;
        }

        // Successfully read sensor data
        sensor_read_success = true;
        localData.temperature = bme680.temperature;
        log_i("BME680 Temperature: %.3f C (measurement #%d)", localData.temperature, measStat.measurement_count + 1);
        sensorData_accumulate.gasData.temperature += localData.temperature;
        sensorData_single.gasData.temperature = localData.temperature;
        log_i("Temperature accumulated total: %.3f C", sensorData_accumulate.gasData.temperature);

        localData.pressure = bme680.pressure / PERCENT_DIVISOR;
        localData.pressure = (localData.pressure *
                              pow(1 - (STD_TEMP_LAPSE_RATE * sensorData_accumulate.gasData.seaLevelAltitude /
                                       (localData.temperature + STD_TEMP_LAPSE_RATE * sensorData_accumulate.gasData.seaLevelAltitude + CELIUS_TO_KELVIN)),
                                  ISA_DERIVED_EXPONENTIAL));
        log_v("Pressure(hPa): %.3f", localData.pressure);
        sensorData_accumulate.gasData.pressure += localData.pressure;
        sensorData_single.gasData.pressure = localData.pressure;

        localData.humidity = bme680.humidity;
        log_v("Humidity(perc.): %.3f", localData.humidity);
        sensorData_accumulate.gasData.humidity += localData.humidity;
        sensorData_single.gasData.humidity = localData.humidity;

        localData.volatileOrganicCompounds = bme680.gasResistance / MICROGRAMS_PER_GRAM;
        localData.volatileOrganicCompounds = fHalSensor_no2AndVocCompensation(localData.volatileOrganicCompounds, &localData, &sensorData_accumulate);
        log_v("Compensated gas resistance(kOhm): %.3f\n", localData.volatileOrganicCompounds);
        sensorData_accumulate.gasData.volatileOrganicCompounds += localData.volatileOrganicCompounds;
        sensorData_single.gasData.volatileOrganicCompounds = localData.volatileOrganicCompounds;
        break;
      }

      if (sensor_read_success)
      {
        log_i("BME680 measurement #%d completed successfully", measStat.measurement_count + 1);
      }
    }
    //------------------------------------------------------------------------------------------------------------------------
    //------------------------------------------------------------------------------------------------------------------------

    // Gas sensor reading (MICS6814 or MICS4514)
    switch (sysStat.gasSensorType)
    {
    case GAS_SENSOR_MICS6814:
    {
      if (sensorData_accumulate.status.MICS6814Sensor)
      {
        log_i("Sampling MICS6814 sensor...");
        err.count = 0;
        // Attempt to read MICS6814 sensor with maximum retries
        bool mics_read_success = false;
        for (int retry = 0; retry < MAX_SENSOR_RETRIES; retry++)
        {
          MICS6814SensorReading_t micsLocData;

          micsLocData.carbonMonoxide = gas.measureCO();
          micsLocData.nitrogenDioxide = gas.measureNO2();
          micsLocData.ammonia = gas.measureNH3();

          if ((micsLocData.carbonMonoxide < 0) || (micsLocData.nitrogenDioxide < 0) || (micsLocData.ammonia < 0))
          {
            err.count++;
            log_w("MICS6814 sensor reading failed, attempt %d/%d", retry + 1, MAX_SENSOR_RETRIES);
            if (retry == (MAX_SENSOR_RETRIES - 1)) // Last attempt failed
            {
              log_e("Error while sampling MICS6814 sensor after %d attempts!", MAX_SENSOR_RETRIES);
              err.MICSfails++;
              break;
            }
            delay(1000);
            continue;
          }

          // Successfully read sensor data
          mics_read_success = true;
          micsLocData.carbonMonoxide = vGeneric_convertPpmToUgM3(micsLocData.carbonMonoxide, sensorData_accumulate.molarMass.carbonMonoxide);
          log_v("CO(ug/m3): %.3f", micsLocData.carbonMonoxide);
          sensorData_accumulate.pollutionData.carbonMonoxide += micsLocData.carbonMonoxide;
          sensorData_single.pollutionData.carbonMonoxide = micsLocData.carbonMonoxide;

          micsLocData.nitrogenDioxide = vGeneric_convertPpmToUgM3(micsLocData.nitrogenDioxide, sensorData_accumulate.molarMass.nitrogenDioxide);
          if (sensorData_accumulate.status.BME680Sensor)
          {
            micsLocData.nitrogenDioxide = fHalSensor_no2AndVocCompensation(micsLocData.nitrogenDioxide, &localData, &sensorData_accumulate);
          }
          log_v("NOx(ug/m3): %.3f", micsLocData.nitrogenDioxide);
          sensorData_accumulate.pollutionData.nitrogenDioxide += micsLocData.nitrogenDioxide;
          sensorData_single.pollutionData.nitrogenDioxide = micsLocData.nitrogenDioxide;

          micsLocData.ammonia = vGeneric_convertPpmToUgM3(micsLocData.ammonia, sensorData_accumulate.molarMass.ammonia);
          log_v("NH3(ug/m3): %.3f\n", micsLocData.ammonia);
          sensorData_accumulate.pollutionData.ammonia += micsLocData.ammonia;
          sensorData_single.pollutionData.ammonia = micsLocData.ammonia;

          break;
        }

        if (mics_read_success)
        {
          log_i("MICS6814 measurement #%d completed successfully", measStat.measurement_count + 1);
        }
      }
      break;
    }
    case GAS_SENSOR_MICS4514:
    {
      if (sensorData_accumulate.status.MICS4514Sensor)
      {
        log_i("Sampling MICS4514 sensor...");
        err.count = 0;
        // Attempt to read MICS4514 sensor with maximum retries
        bool mics_read_success = false;
        for (int retry = 0; retry < MAX_SENSOR_RETRIES; retry++)
        {
          MICS4514SensorReading_t micsLocData;

          // Read raw ADC values and accumulate for averaging
          if (tHalSensor_readMICS4514_ADC(mics4514, &sensorData_accumulate) != STATUS_OK)
          {
            err.count++;
            log_w("MICS4514 ADC reading failed, attempt %d/%d", retry + 1, MAX_SENSOR_RETRIES);
            if (retry == (MAX_SENSOR_RETRIES - 1)) // Last attempt failed
            {
              log_e("Error while reading MICS4514 ADC after %d attempts!", MAX_SENSOR_RETRIES);
              err.MICSfails++;
              break;
            }
            delay(1000);
            continue;
          }

          // Also calculate immediate gas concentrations for real-time display (sensorData_single)
          // This provides instant feedback for LCD while accumulating for accurate averaging
          if (tHalSensor_calculateMICS4514_Immediate(mics4514, &sensorData_accumulate, &micsLocData) == STATUS_OK)
          {
            // Convert PPM to µg/m³ for immediate display values (same as old approach)
            micsLocData.carbonMonoxide = vGeneric_convertPpmToUgM3(micsLocData.carbonMonoxide, sensorData_accumulate.molarMass.carbonMonoxide);
            log_v("CO(ug/m3): %.3f", micsLocData.carbonMonoxide);
            sensorData_single.pollutionData.carbonMonoxide = micsLocData.carbonMonoxide;

            // Convert NO2 from PPM to µg/m³ (with optional BME680 compensation)
            micsLocData.nitrogenDioxide = vGeneric_convertPpmToUgM3(micsLocData.nitrogenDioxide, sensorData_accumulate.molarMass.nitrogenDioxide);
            if (sensorData_accumulate.status.BME680Sensor)
            {
              micsLocData.nitrogenDioxide = fHalSensor_no2AndVocCompensation(micsLocData.nitrogenDioxide, &localData, &sensorData_accumulate);
            }
            log_v("NOx(ug/m3): %.3f", micsLocData.nitrogenDioxide);
            sensorData_single.pollutionData.nitrogenDioxide = micsLocData.nitrogenDioxide;

            // Convert NH3 from PPM to µg/m³
            micsLocData.ammonia = vGeneric_convertPpmToUgM3(micsLocData.ammonia, sensorData_accumulate.molarMass.ammonia);
            log_v("NH3(ug/m3): %.3f", micsLocData.ammonia);
            sensorData_single.pollutionData.ammonia = micsLocData.ammonia;

            log_i("MICS4514 immediate values for display: CO=%.2f, NO2=%.2f, NH3=%.2f µg/m³",
                  micsLocData.carbonMonoxide, micsLocData.nitrogenDioxide, micsLocData.ammonia);
          }
          else
          {
            log_w("MICS4514 immediate gas calculation failed, keeping previous display values");
          }

          // Successfully read and accumulated ADC data
          mics_read_success = true;
          log_i("MICS4514 measurement #%d: ADC accumulated and display updated", measStat.measurement_count + 1);

          break;
        }

        if (mics_read_success)
        {
          log_i("MICS4514 measurement #%d completed successfully", measStat.measurement_count + 1);
        }
        break;
      }
    }
    default:
    {
      log_w("WRONG GAS SENSOR SELECTED - OR NO GAS SENSOR");
      break;
    }
    }

    // READING O3
    if (sensorData_accumulate.status.O3Sensor)
    {
      ze25Data_t o3Data;
      log_i("Sampling O3 sensor...");
      err.count = 0;
      // Attempt to read O3 sensor with maximum retries
      bool o3_read_success = false;
      for (int retry = 0; retry < MAX_SENSOR_RETRIES; retry++)
      {
        if (!tHalSensor_isAnalogO3Connected())
        {
          err.count++;
          log_w("O3 sensor connection check failed, attempt %d/%d", retry + 1, MAX_SENSOR_RETRIES);
          if (retry == (MAX_SENSOR_RETRIES - 1)) // Last attempt failed
          {
            log_e("Error while sampling O3 sensor after %d attempts!", MAX_SENSOR_RETRIES);
            err.O3fails++;
            break;
          }
          delay(1000);
          continue;
        }

        // Successfully read sensor data
        o3_read_success = true;
        o3Data.ozone = fHalSensor_analogUgM3O3Read(&localData.temperature, &sensorData_accumulate);
        log_v("O3(ug/m3): %.3f", o3Data.ozone);
        sensorData_accumulate.ozoneData.ozone += o3Data.ozone;
        sensorData_single.ozoneData.ozone = o3Data.ozone;

        break;
      }

      if (o3_read_success)
      {
        log_i("O3 measurement #%d completed successfully", measStat.measurement_count + 1);
      }
    }
    else
    {
      sensorData_accumulate.ozoneData.ozone = 0.0f;
      sensorData_single.ozoneData.ozone = 0.0f;
    }

    // READING PMS5003
    if (sensorData_accumulate.status.PMS5003Sensor)
    {
      log_i("Sampling PMS5003 sensor...");
      err.count = 0;
      // Attempt to read PMS5003 sensor with maximum retries
      bool pms_read_success = false;
      for (int retry = 0; retry < MAX_SENSOR_RETRIES; retry++)
      {
        // Clear serial buffer
        while (pmsSerial.available())
        {
          pmsSerial.read();
        }

        if (!pms.readUntil(data))
        {
          err.count++;
          log_w("PMS5003 sensor reading failed, attempt %d/%d", retry + 1, MAX_SENSOR_RETRIES);
          if (retry == (MAX_SENSOR_RETRIES - 1)) // Last attempt failed
          {
            log_e("Error while sampling PMS5003 sensor after %d attempts!", MAX_SENSOR_RETRIES);
            err.PMSfails++;
            break;
          }
          delay(1000);
          continue;
        }

        // Successfully read sensor data
        pms_read_success = true;
        log_v("PM1(ug/m3): %d", data.PM_AE_UG_1_0);
        sensorData_accumulate.airQualityData.particleMicron1 += data.PM_AE_UG_1_0;
        sensorData_single.airQualityData.particleMicron1 = data.PM_AE_UG_1_0;

        log_v("PM2,5(ug/m3): %d", data.PM_AE_UG_2_5);
        sensorData_accumulate.airQualityData.particleMicron25 += data.PM_AE_UG_2_5;
        sensorData_single.airQualityData.particleMicron25 = data.PM_AE_UG_2_5;

        log_v("PM10(ug/m3): %d\n", data.PM_AE_UG_10_0);
        sensorData_accumulate.airQualityData.particleMicron10 += data.PM_AE_UG_10_0;
        sensorData_single.airQualityData.particleMicron10 = data.PM_AE_UG_10_0;

        break;
      }

      if (pms_read_success)
      {
        log_i("PMS5003 measurement #%d completed successfully", measStat.measurement_count + 1);
      }
    }

    // Calculate MSP index for the single measurement data
    sensorData_single.MSP = sHalSensor_evaluateMSPIndex(&sensorData_single);

    measStat.isSensorDataAvailable = true;

    measStat.measurement_count++;
    log_i("MEASUREMENT COUNT: Incremented to %d (target: %d measurements)", measStat.measurement_count, measStat.avg_measurements);
    log_i("Current minute: %d, expected transmission at boundary: %s",
          measStat.curr_minutes, ((measStat.curr_minutes % measStat.avg_measurements) == 0) ? "YES" : "NO");

    log_i("Sensor values AFTER evaluation:\n");
    log_i("temp: %.2f, hum: %.2f, pre: %.2f, VOC: %.2f, PM1: %d, PM25: %d, PM10: %d, MICS_CO: %.2f, MICS_NO2: %.2f, MICS_NH3: %.2f, ozone: %.2f, MSP: %d, measurement_count: %d\n",
          sensorData_accumulate.gasData.temperature, sensorData_accumulate.gasData.humidity, sensorData_accumulate.gasData.pressure, sensorData_accumulate.gasData.volatileOrganicCompounds,
          sensorData_accumulate.airQualityData.particleMicron1, sensorData_accumulate.airQualityData.particleMicron25, sensorData_accumulate.airQualityData.particleMicron10,
          sensorData_accumulate.pollutionData.carbonMonoxide, sensorData_accumulate.pollutionData.nitrogenDioxide, sensorData_accumulate.pollutionData.ammonia,
          sensorData_accumulate.ozoneData.ozone, sensorData_accumulate.MSP, measStat.measurement_count);
    log_i("Measurements done, going to timeout...\n");

    mainStateMachine.prev_state = SYS_STATE_READ_SENSORS;
    mainStateMachine.next_state = SYS_STATE_EVAL_SENSOR_STATUS;
    break;
  }
  //------------------------------------------------------------------------------------------------------------------------
  //------------------------------------------------------------------------------------------------------------------------
  case SYS_STATE_EVAL_SENSOR_STATUS:
  {
    log_i("Evaluating sensor status...");

    // Update current time for clock-aligned mode evaluation
    if (getLocalTime(&timeinfo))
    {
      measStat.curr_minutes = timeinfo.tm_min;
      log_v("Current time for evaluation: %02d:%02d", timeinfo.tm_hour, measStat.curr_minutes);
    }
    int8_t sens_stat_count = 0;
    for (sens_stat_count = 0; sens_stat_count < SENS_STAT_MAX; sens_stat_count++)
    {
      if (err.senserrs[sens_stat_count] == true)
      {
        switch (sens_stat_count)
        {
        case SENS_STAT_BME680:
          sensorData_accumulate.status.BME680Sensor = true;
          sensorData_single.status.BME680Sensor = true;
          break;
        case SENS_STAT_PMS5003:
          sensorData_accumulate.status.PMS5003Sensor = true;
          sensorData_single.status.PMS5003Sensor = true;
          break;
        case SENS_STAT_MICSxxxx:
          sensorData_accumulate.status.MICS6814Sensor = true;
          sensorData_single.status.MICS6814Sensor = true;
          break;
        case SENS_STAT_O3:
          sensorData_accumulate.status.O3Sensor = true;
          sensorData_single.status.O3Sensor = true;
          break;
        }
      }
    }

    // Check if it's time to send data
    // We transmit when: 1) we have collected avg_measurements AND 2) we're at a transmission boundary AND 3) haven't transmitted at this boundary yet
    bool have_enough_measurements = (measStat.measurement_count >= measStat.avg_measurements);

    // Improved boundary logic: check if current minute is at interval boundary OR enough time has passed since last transmission
    bool at_transmission_boundary = ((measStat.curr_minutes % measStat.avg_measurements) == 0); // Standard interval boundary

    // Alternative boundary check: if enough time has passed since last transmission (handles hour rollover)
    bool enough_time_passed = false;
    if (measStat.last_transmission_minute >= 0) // Only check if we have a previous transmission
    {
      int minutes_since_last_tx;
      if (measStat.curr_minutes >= measStat.last_transmission_minute)
      {
        // Same hour
        minutes_since_last_tx = measStat.curr_minutes - measStat.last_transmission_minute;
      }
      else
      {
        // Hour rollover (e.g., from 58 to 2)
        minutes_since_last_tx = (60 - measStat.last_transmission_minute) + measStat.curr_minutes;
      }
      enough_time_passed = (minutes_since_last_tx >= measStat.avg_measurements);
    }
    else
    {
      // First transmission ever - always allow
      enough_time_passed = true;
    }

    // Use either standard boundary OR enough time has passed
    at_transmission_boundary = at_transmission_boundary || enough_time_passed;

    bool boundary_not_transmitted = (measStat.last_transmission_minute != measStat.curr_minutes); // Prevent duplicate transmissions at same boundary

    // Enhanced logging for boundary logic debugging
    int minutes_since_last_tx = 0;
    if (measStat.last_transmission_minute >= 0)
    {
      if (measStat.curr_minutes >= measStat.last_transmission_minute)
      {
        minutes_since_last_tx = measStat.curr_minutes - measStat.last_transmission_minute;
      }
      else
      {
        minutes_since_last_tx = (60 - measStat.last_transmission_minute) + measStat.curr_minutes;
      }
    }

    log_i("TRANSMISSION CHECK: collected %d/%d measurements, boundary=%s (minute %d, interval=%d), last_tx_minute=%d",
          measStat.measurement_count, measStat.avg_measurements,
          at_transmission_boundary ? "YES" : "NO", measStat.curr_minutes, measStat.avg_measurements, measStat.last_transmission_minute);

    log_i("BOUNDARY DETAILS: standard_boundary=%s, enough_time_passed=%s, minutes_since_last_tx=%d",
          ((measStat.curr_minutes % measStat.avg_measurements) == 0) ? "YES" : "NO",
          enough_time_passed ? "YES" : "NO", minutes_since_last_tx);

    if (have_enough_measurements && at_transmission_boundary && boundary_not_transmitted)
    {
      log_i("All measurements obtained, going to send data...\n");
      mainStateMachine.next_state = SYS_STATE_SEND_DATA; // go to send data state
    }
    else if (!boundary_not_transmitted)
    {
      log_i("Data already transmitted at this boundary (minute %d), waiting for next boundary", measStat.curr_minutes);
      mainStateMachine.next_state = SYS_STATE_WAIT_FOR_TIMEOUT;
    }
    else
    {
      // MEASUREMENTS MESSAGE
      log_i("Measurements in progress...\n");
      vMsp_updateDataAndSendEvent(DISP_EVENT_SHOW_MEAS_DATA, &sensorData_single, &devinfo, &measStat, &sysData, &sysStat);

      log_i("Not enough measurements obtained, going to wait for timeout...\n");
      mainStateMachine.next_state = SYS_STATE_WAIT_FOR_TIMEOUT;
    }

    mainStateMachine.prev_state = SYS_STATE_EVAL_SENSOR_STATUS;
    break;
  }
  //------------------------------------------------------------------------------------------------------------------------
  //------------------------------------------------------------------------------------------------------------------------
  case SYS_STATE_SEND_DATA:
  {
    log_i("Sending data to server...\n");

    // We have obtained all the measurements, do the mean and transmit the data
    log_i("All measurements obtained, sending data...\n");
    log_i("Sensor values BEFORE AVERAGE:\n");
    log_i("temp: %.2f, hum: %.2f, pre: %.2f, VOC: %.2f, PM1: %d, PM25: %d, PM10: %d, MICS_CO: %.2f, MICS_NO2: %.2f, MICS_NH3: %.2f, ozone: %.2f, MSP: %d, measurement_count: %d vs %d\n",
          sensorData_accumulate.gasData.temperature, sensorData_accumulate.gasData.humidity, sensorData_accumulate.gasData.pressure,
          sensorData_accumulate.gasData.volatileOrganicCompounds,
          sensorData_accumulate.airQualityData.particleMicron1, sensorData_accumulate.airQualityData.particleMicron25, sensorData_accumulate.airQualityData.particleMicron10,
          sensorData_accumulate.pollutionData.carbonMonoxide, sensorData_accumulate.pollutionData.nitrogenDioxide, sensorData_accumulate.pollutionData.ammonia,
          sensorData_accumulate.ozoneData.ozone, sensorData_accumulate.MSP, measStat.measurement_count, measStat.avg_measurements);

    vMsp_updateDataAndSendEvent(DISP_EVENT_SENDING_MEAS, &sensorData_single, &devinfo, &measStat, &sysData, &sysStat);

    if (sensorData_accumulate.status.BME680Sensor || sensorData_accumulate.status.PMS5003Sensor || sensorData_accumulate.status.MICS6814Sensor || sensorData_accumulate.status.MICS4514Sensor || sensorData_accumulate.status.O3Sensor)
    {
      log_i("Computing averages from %d measurements", measStat.measurement_count);
      log_i("Error counts: BME=%d, PMS=%d, MICS=%d, O3=%d", err.BMEfails, err.PMSfails, err.MICSfails, err.O3fails);

      // Log values before averaging for debugging
      log_i("BEFORE AVERAGING: temp=%.2f, PM2.5=%d, CO=%.2f",
            sensorData_accumulate.gasData.temperature,
            sensorData_accumulate.airQualityData.particleMicron25,
            sensorData_accumulate.pollutionData.carbonMonoxide);

      vHalSensor_performAverages(&err, &sensorData_accumulate, &measStat);

      // Update sensorData_single with final averaged values for LCD display
      if (sensorData_accumulate.status.MICS4514Sensor)
      {
        log_i("Updating sensorData_single with final averaged MICS4514 values");
        sensorData_single.pollutionData.carbonMonoxide = sensorData_accumulate.pollutionData.carbonMonoxide;
        sensorData_single.pollutionData.nitrogenDioxide = sensorData_accumulate.pollutionData.nitrogenDioxide;
        sensorData_single.pollutionData.ammonia = sensorData_accumulate.pollutionData.ammonia;
        log_i("sensorData_single updated: CO=%.2f, NO2=%.2f, NH3=%.2f µg/m³",
              sensorData_single.pollutionData.carbonMonoxide,
              sensorData_single.pollutionData.nitrogenDioxide,
              sensorData_single.pollutionData.ammonia);
      }

      // Log values after averaging for debugging
      log_i("AFTER AVERAGING: temp=%.2f, PM2.5=%d, CO=%.2f",
            sensorData_accumulate.gasData.temperature,
            sensorData_accumulate.airQualityData.particleMicron25,
            sensorData_accumulate.pollutionData.carbonMonoxide);
    }

    // MSP# Index evaluation
    sensorData_accumulate.MSP = sHalSensor_evaluateMSPIndex(&sensorData_accumulate);

    log_i("Sending data to server...");
    send_data_t sendData;
    memcpy(&sendData.sendTimeInfo, &timeinfo, sizeof(tm)); // copy time info to sendData
    log_i("Current time: %02d:%02d:%02d\n", sendData.sendTimeInfo.tm_hour, sendData.sendTimeInfo.tm_min, sendData.sendTimeInfo.tm_sec);
    // Keep the original timestamp - measurements are aligned to minute boundaries
    // No need to manipulate time as measurements now start exactly at :00 seconds
    sendData.sendTimeInfo.tm_sec = 0; // Always send with :00 seconds for consistency

    log_i("Send timestamp: %02d:%02d:%02d\n", sendData.sendTimeInfo.tm_hour, sendData.sendTimeInfo.tm_min, sendData.sendTimeInfo.tm_sec);
    sendData.temp = sensorData_accumulate.gasData.temperature;
    sendData.hum = sensorData_accumulate.gasData.humidity;
    sendData.pre = sensorData_accumulate.gasData.pressure;
    sendData.VOC = sensorData_accumulate.gasData.volatileOrganicCompounds;
    sendData.PM1 = sensorData_accumulate.airQualityData.particleMicron1;
    sendData.PM25 = sensorData_accumulate.airQualityData.particleMicron25;
    sendData.PM10 = sensorData_accumulate.airQualityData.particleMicron10;
    // Assign gas sensor data based on sensor type
    switch (sysStat.gasSensorType)
    {
    case GAS_SENSOR_MICS6814:
      sendData.MICS_CO = sensorData_accumulate.pollutionData.carbonMonoxide;
      sendData.MICS_NO2 = sensorData_accumulate.pollutionData.nitrogenDioxide;
      sendData.MICS_NH3 = sensorData_accumulate.pollutionData.ammonia;
      break;

    case GAS_SENSOR_MICS4514:
      sendData.MICS_CO = sensorData_accumulate.pollutionData.carbonMonoxide;
      sendData.MICS_NO2 = sensorData_accumulate.pollutionData.nitrogenDioxide;
      sendData.MICS_NH3 = sensorData_accumulate.pollutionData.ammonia;
      break;

    default:
      // Default to zero if no gas sensor configured
      sendData.MICS_CO = 0.0;
      sendData.MICS_NO2 = 0.0;
      sendData.MICS_NH3 = 0.0;
      break;
    }
    sendData.ozone = sensorData_accumulate.ozoneData.ozone;
    sendData.MSP = sensorData_accumulate.MSP;

    log_i("Sensor values AFTER AVERAGE:\n");
    log_i("temp: %.2f, hum: %.2f, pre: %.2f, VOC: %.2f, PM1: %d, PM25: %d, PM10: %d, MICS_CO: %.2f, MICS_NO2: %.2f, MICS_NH3: %.2f, ozone: %.2f, MSP: %d, measurement_count: %d\n",
          sensorData_accumulate.gasData.temperature, sensorData_accumulate.gasData.humidity, sensorData_accumulate.gasData.pressure,
          sensorData_accumulate.gasData.volatileOrganicCompounds,
          sensorData_accumulate.airQualityData.particleMicron1, sensorData_accumulate.airQualityData.particleMicron25, sensorData_accumulate.airQualityData.particleMicron10,
          sensorData_accumulate.pollutionData.carbonMonoxide, sensorData_accumulate.pollutionData.nitrogenDioxide, sensorData_accumulate.pollutionData.ammonia,
          sensorData_accumulate.ozoneData.ozone, sensorData_accumulate.MSP, measStat.measurement_count);

    log_i("Send Time: %02d:%02d:%02d\n", sendData.sendTimeInfo.tm_hour, sendData.sendTimeInfo.tm_min, sendData.sendTimeInfo.tm_sec);

    // Enqueue data for transmission by network task
    if (enqueueSendData(sendData, pdMS_TO_TICKS(500)))
    {
      log_i("Data enqueued successfully for network transmission");
      measStat.data_transmitted = true;                          // Mark data as transmitted for this cycle
      measStat.last_transmission_minute = measStat.curr_minutes; // Record the transmission boundary minute
      log_i("Recorded transmission at minute %d to prevent duplicates", measStat.last_transmission_minute);
    }
    else
    {
      log_e("Failed to enqueue data for transmission - queue might be full");
    }

    // show the already captured data
    vMsp_updateDataAndSendEvent(DISP_EVENT_SHOW_MEAS_DATA, &sensorData_single, &devinfo, &measStat, &sysData, &sysStat);

    log_i("Data transmission time: %02d:%02d:%02d", sendData.sendTimeInfo.tm_hour, sendData.sendTimeInfo.tm_min, sendData.sendTimeInfo.tm_sec);
    log_i("temp: %.2f, hum: %.2f, pre: %.2f, VOC: %.2f, PM1: %d, PM25: %d, PM10: %d, MICS_CO: %.2f, MICS_NO2: %.2f, MICS_NH3: %.2f, ozone: %.2f, MSP: %d, measurement_count: %d vs %d",
          sensorData_accumulate.gasData.temperature, sensorData_accumulate.gasData.humidity, sensorData_accumulate.gasData.pressure,
          sensorData_accumulate.gasData.volatileOrganicCompounds,
          sensorData_accumulate.airQualityData.particleMicron1, sensorData_accumulate.airQualityData.particleMicron25, sensorData_accumulate.airQualityData.particleMicron10,
          sensorData_accumulate.pollutionData.carbonMonoxide, sensorData_accumulate.pollutionData.nitrogenDioxide, sensorData_accumulate.pollutionData.ammonia,
          sensorData_accumulate.ozoneData.ozone, sensorData_accumulate.MSP, measStat.measurement_count, measStat.avg_measurements);

    mainStateMachine.isFirstTransition = true; // set first transition flag
    // This will clean up the sensor variables for the next cycle

    // Always reset measurement count after transmission attempt (success or failure)
    log_i("TRANSMISSION ATTEMPT COMPLETE: Resetting measurement_count from %d to 0", measStat.measurement_count);
    measStat.measurement_count = 0;
    measStat.data_transmitted = false; // Reset flag for new measurement cycle
    // Note: last_transmission_minute is NOT reset here - it stays to prevent duplicates at the same boundary

    // Reset all sensor data (both single and accumulated) for clean start of next cycle
    log_i("Resetting all sensor data for next measurement cycle");
    // Reset accumulated sensor data
    sensorData_accumulate.gasData.temperature = 0.0f;
    sensorData_accumulate.gasData.pressure = 0.0f;
    sensorData_accumulate.gasData.humidity = 0.0f;
    sensorData_accumulate.gasData.volatileOrganicCompounds = 0.0f;
    sensorData_accumulate.airQualityData.particleMicron1 = 0.0f;
    sensorData_accumulate.airQualityData.particleMicron25 = 0.0f;
    sensorData_accumulate.airQualityData.particleMicron10 = 0.0f;
    sensorData_accumulate.pollutionData.carbonMonoxide = 0.0f;
    sensorData_accumulate.pollutionData.nitrogenDioxide = 0.0f;
    sensorData_accumulate.pollutionData.ammonia = 0.0f;
    sensorData_accumulate.ozoneData.ozone = 0.0f;

    // Reset MICS4514 ADC accumulator
    sensorData_accumulate.mics4514AdcAccumulator.oxVoltageSum = 0;
    sensorData_accumulate.mics4514AdcAccumulator.redVoltageSum = 0;

    // Reset single measurement data
    sensorData_single.gasData.temperature = 0.0f;
    sensorData_single.gasData.pressure = 0.0f;
    sensorData_single.gasData.humidity = 0.0f;
    sensorData_single.gasData.volatileOrganicCompounds = 0.0f;
    sensorData_single.airQualityData.particleMicron1 = 0.0f;
    sensorData_single.airQualityData.particleMicron25 = 0.0f;
    sensorData_single.airQualityData.particleMicron10 = 0.0f;
    sensorData_single.pollutionData.carbonMonoxide = 0.0f;
    sensorData_single.pollutionData.nitrogenDioxide = 0.0f;
    sensorData_single.pollutionData.ammonia = 0.0f;
    sensorData_single.ozoneData.ozone = 0.0f;

    // Check for firmware updates after midnight data transmission
    if (getLocalTime(&timeinfo))
    {
      int current_day = timeinfo.tm_yday; // Day of year (0-365)
      static int last_fw_check_day = -1;

      // Check if this is midnight hour transmission (00:xx) and we haven't checked today
      // Allow firmware check for any minute in the midnight hour, not just 00:00
      if ((timeinfo.tm_hour == 0) &&
          (current_day != last_fw_check_day) && sysStat.fwAutoUpgrade)
      {
        log_i("Midnight hour data transmission completed (time: %02d:%02d) - triggering daily firmware update check",
              timeinfo.tm_hour, timeinfo.tm_min);

        if (true == bHalFirmware_checkForUpdates(&sysData, &sysStat, &devinfo))
        {
          last_fw_check_day = current_day;
          log_i("Firmware update check completed for day %d", current_day);
        }
        else
        {
          log_w("Firmware update check failed for day %d", current_day);
        }
      }
      else
      {
        // Enhanced logging to understand why firmware check was skipped
        log_d("Firmware check skipped: hour=%d, min=%d, day=%d, last_check_day=%d, fwAutoUpgrade=%s",
              timeinfo.tm_hour, timeinfo.tm_min, current_day, last_fw_check_day,
              sysStat.fwAutoUpgrade ? "true" : "false");
      }

      log_i("NTP SYNC CHECK - current_day:%d - last_sync_day:%d", current_day, sysData.ntp_last_sync_day);
      // Check if daily NTP sync is needed (at 00:00:00)
      if (((timeinfo.tm_hour == 0) && (timeinfo.tm_min == 0) && (timeinfo.tm_sec == 0)) &&
          (current_day != sysData.ntp_last_sync_day))
      {
        log_i("Daily NTP sync needed - triggering time synchronization");
        sysData.ntp_last_sync_day = current_day;
        requestNetworkConnection(); // This will trigger NTP sync
        mainStateMachine.next_state = SYS_STATE_WAIT_FOR_NTP_SYNC;
        break;
      }
    }

    // After transmission, system is now aligned - next cycles will be full measurements
    log_i("Data sent successfully. System now aligned - next cycles will collect %d measurements", measStat.max_measurements);

    mainStateMachine.prev_state = SYS_STATE_SEND_DATA;
    mainStateMachine.next_state = SYS_STATE_WAIT_FOR_TIMEOUT; // go to wait for timeout state
    break;
  }
  //------------------------------------------------------------------------------------------------------------------------
  //------------------------------------------------------------------------------------------------------------------------
  case SYS_STATE_ERROR:
  {
    vMsp_updateDataAndSendEvent(DISP_EVENT_SYSTEM_ERROR, &sensorData_single, &devinfo, &measStat, &sysData, &sysStat);
    log_e("System in error state! Waiting for reset...");
    vTaskDelay(portMAX_DELAY);
    break;
  }
  //------------------------------------------------------------------------------------------------------------------------
  //------------------------------------------------------------------------------------------------------------------------
  default:
  {
    log_e("Unknown state %d!", mainStateMachine.current_state);
    mainStateMachine.next_state = SYS_STATE_WAIT_FOR_TIMEOUT; // go to error state
    break;
  }
  }
  mainStateMachine.current_state = mainStateMachine.next_state; // update current state to next state
}

/*********************************************************
 * @brief init function
 *
 * @param p_tData
 *********************************************************/
void vMspInit_sensorStatusAndData(sensorData_t *p_tData)
{
  vMspOs_takeDataAccessMutex();

  // -- set default sensor status to disabled
  p_tData->status.BME680Sensor = DISABLED;
  p_tData->status.PMS5003Sensor = DISABLED;
  p_tData->status.MICS6814Sensor = DISABLED;
  p_tData->status.MICS4514Sensor = DISABLED;
  p_tData->status.O3Sensor = DISABLED;

  // -- set gas sensor default values
  p_tData->gasData.humidity = 0.0;
  p_tData->gasData.temperature = 0.0;
  p_tData->gasData.pressure = 0.0;
  p_tData->gasData.volatileOrganicCompounds = 0.0;
  p_tData->gasData.seaLevelAltitude = SEA_LEVEL_ALTITUDE_IN_M;

  // -- set air quality sensor default values
  p_tData->airQualityData.particleMicron1 = 0;
  p_tData->airQualityData.particleMicron25 = 0;
  p_tData->airQualityData.particleMicron10 = 0;

  // -- default R0 values for the sensor (RED, OX, NH3)
  p_tData->micsTuningData.sensingResInAir.redSensor = R0_RED_SENSOR;
  p_tData->micsTuningData.sensingResInAir.oxSensor = R0_OX_SENSOR;
  p_tData->micsTuningData.sensingResInAir.nh3Sensor = R0_NH3_SENSOR;

  // -- default point offset values for sensor measurements (RED, OX, NH3)
  p_tData->micsTuningData.sensingResInAirOffset.redSensor = 0;
  p_tData->micsTuningData.sensingResInAirOffset.oxSensor = 0;
  p_tData->micsTuningData.sensingResInAirOffset.nh3Sensor = 0;

  // -- molar mass of (CO, NO2, NH3)
  p_tData->molarMass.carbonMonoxide = CO_MOLAR_MASS;
  p_tData->molarMass.nitrogenDioxide = NO2_MOLAR_MASS;
  p_tData->molarMass.ammonia = NH3_MOLAR_MASS;

  // -- MICS6814 sensor data
  p_tData->pollutionData.carbonMonoxide = 0;
  p_tData->pollutionData.nitrogenDioxide = 0;
  p_tData->pollutionData.ammonia = 0;

  // -- ozone detection module data
  p_tData->ozoneData.ozone = 0;
  p_tData->ozoneData.o3ZeroOffset = O3_SENS_DISABLE_ZERO_OFFSET;

  // -- compensation parameters for MICS6814-OX and BME680-VOC data
  p_tData->compParams.currentHumidity = HUMIDITY_COMP_PARAM; /*!<default humidity compensation */
  p_tData->compParams.currentTemperature = TEMP_COMP_PARAM;  /*!<default temperature compensation */
  p_tData->compParams.currentPressure = PRESS_COMP_PARAM;    /*!<default pressure compensation */

  p_tData->MSP = MSP_DEFAULT_DATA; /*!<set to -1 to distinguish from grey (0) */

  vMspOs_giveDataAccessMutex();
}

void vMspInit_NetworkAndMeasInfo(void)
{
  // Initialize measurement info
  vMspInit_MeasInfo();

  // Initialize network configuration (network task will load from SD)
  devinfo = deviceNetworkInfo_t{}; // Initialize all members to default values
  devinfo.wifipow = WIFI_POWER_17dBm;
}

void vMspInit_MeasInfo(void)
{
  memset(&measStat, 0, sizeof(deviceMeasurement_t));
  measStat.avg_measurements = 1;
  measStat.isPmsAwake = false;
  measStat.isSensorDataAvailable = false;
  measStat.last_transmission_minute = -1; // Initialize to invalid minute
}

/**
 * @brief Configure system with SD card data or apply defaults
 * @param sysData System data structure
 * @param sysStat System status structure
 * @param devInfo Device network info structure
 * @param measStat Measurement statistics structure
 */
void vMspInit_configureSystemFromSD(systemData_t *sysData, systemStatus_t *sysStat,
                                    deviceNetworkInfo_t *devInfo, deviceMeasurement_t *measStat)
{
  log_i("Configuring system from SD data or defaults...");

  // Measurement configuration - ensure it's a valid submultiple of 60
  int valid_intervals[] = {1, 2, 3, 4, 5, 6, 10, 12, 15, 20, 30, 60};
  bool is_valid = false;

  // Apply configuration defaults if SD card data is missing or invalid
  if ((!sysStat->configuration) || (!sysStat->sdCard))
  {
    log_w("SD configuration unavailable, applying defaults");

    // Network defaults
    if (devInfo->ssid.length() == 0)
    {
      devInfo->ssid = "DefaultSSID";
      log_i("Applied default WiFi SSID");
    }
    if (devInfo->passw.length() == 0)
    {
      devInfo->passw = "DefaultPass";
      log_i("Applied default WiFi password");
    }
    if (devInfo->apn.length() == 0)
    {
      devInfo->apn = "internet";
      log_i("Applied default APN");
    }

    // Server configuration
    if (sysData->server.length() == 0)
    {
#ifdef API_SERVER
      sysData->server = API_SERVER;
      sysStat->server_ok = true;
      log_i("Applied compile-time API server: %s", sysData->server.c_str());
#else
      sysData->server = "milanosmartpark.info";
      sysStat->server_ok = true;
      log_w("Applied fallback server: %s", sysData->server.c_str());
#endif
    }

    // API credentials
    if (sysData->api_secret_salt.length() == 0)
    {
      sysData->api_secret_salt = __API_SECRET_SALT;
#ifdef API_SECRET_SALT
      log_i("Applied compile-time API secret");
#else
      log_w("Applied fallback API secret");
#endif
    }

    for (int i = 0; i < sizeof(valid_intervals) / sizeof(valid_intervals[0]); i++)
    {
      if (measStat->avg_measurements == valid_intervals[i])
      {
        is_valid = true;
        break;
      }
    }

    if (!is_valid || measStat->avg_measurements <= 0)
    {
      measStat->avg_measurements = 5; // Default to 5 measurements
      log_i("Applied default avg_measurements: %d (must be submultiple of 60)", measStat->avg_measurements);
    }

    // NTP configuration
    if (sysData->ntp_server.length() == 0)
    {
      sysData->ntp_server = NTP_SERVER_DEFAULT;
      log_i("Applied default NTP server: %s", sysData->ntp_server.c_str());
    }
    if (sysData->timezone.length() == 0)
    {
      sysData->timezone = TZ_DEFAULT;
      log_i("Applied default timezone: %s", sysData->timezone.c_str());
    }
  }
  else
  {
    log_i("Using SD card configuration");

    // Validate loaded configuration

    for (int i = 0; i < sizeof(valid_intervals) / sizeof(valid_intervals[0]); i++)
    {
      if (measStat->avg_measurements == valid_intervals[i])
      {
        is_valid = true;
        break;
      }
    }

    if (!is_valid || measStat->avg_measurements <= 0)
    {
      log_w("Invalid avg_measurements (%d), must be submultiple of 60. Setting to 5", measStat->avg_measurements);
      measStat->avg_measurements = 5;
    }

    // Ensure server_ok flag is set if server is configured
    if (sysData->server.length() > 0 && !sysStat->server_ok)
    {
      sysStat->server_ok = true;
      log_i("Server configured, setting server_ok flag");
    }
  }

// Apply firmware version
#ifdef VERSION_STRING
  sysData->ver = VERSION_STRING;
#else
  sysData->ver = "DEV";
#endif

  log_i("System configuration completed successfully");
}

void vMspInit_setDefaultNtpTimezoneStatus(systemData_t *p_tData)
{
  // set default values
  p_tData->ntp_server = NTP_SERVER_DEFAULT; /*!<default NTP server */
  p_tData->timezone = TZ_DEFAULT;           /*!<default timezone */
}

void vMsp_setGpioPins(void)
{
  // SET PIN MODES ++++++++++++++++++++++++++++++++++++
  pinMode(33, OUTPUT);
  pinMode(26, OUTPUT);
  pinMode(25, OUTPUT);
  pinMode(O3_ADC_PIN, INPUT_PULLDOWN);
  analogSetAttenuation(ADC_11db);
  pinMode(MODEM_RST, OUTPUT);
  digitalWrite(MODEM_RST, HIGH);
  //+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
}

/**
 * @brief Scan I2C bus for connected devices and log their addresses
 */
void vMsp_scanI2CDevices(void)
{
  log_i("=== I2C Device Scanner ===");
  log_i("Scanning I2C bus for devices...");

  int deviceCount = 0;

  for (uint8_t address = 1; address < 127; address++)
  {
    Wire.beginTransmission(address);
    uint8_t error = Wire.endTransmission();

    if (error == 0)
    {
      log_i("I2C device found at address 0x%02X (%d decimal)", address, address);
      deviceCount++;

      // Add known device identification
      switch (address)
      {
      case 0x77:
        log_i("  -> Likely BME680 (address 0x77)");
        break;
      case 0x76:
        log_i("  -> Likely BME680 alternative address (0x76)");
        break;
      case 0x04:
        log_i("  -> Likely MICS6814 (address 0x04)");
        break;
      case 0x75:
        log_i("  -> Likely MICS4514/DFRobot SEN0377 (address 0x75)");
        break;
      case 0x3C:
      case 0x3D:
        log_i("  -> Likely OLED Display (address 0x%02X)", address);
        break;
      case 0x48:
      case 0x49:
      case 0x4A:
      case 0x4B:
        log_i("  -> Likely ADS1115/ADS1015 ADC (address 0x%02X)", address);
        break;
      default:
        log_i("  -> Unknown device type");
        break;
      }
    }
    else if (error == 4)
    {
      log_e("Unknown error at address 0x%02X", address);
    }
  }

  if (deviceCount == 0)
  {
    log_w("No I2C devices found!");
  }
  else
  {
    log_i("Scan complete. Found %d I2C devices.", deviceCount);
  }
  log_i("=== End I2C Scanner ===");
}

//************************************** EOF **************************************