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

// Include MSP1 libraries
#include "msp1.h"

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
#include "esp_task_wdt.h"

HardwareSerial pmsSerial(PMS_UART_NUM);

// ------------------------------- DEFINES -----------------------------------------------------------------------------

// ------------------------------- INSTANCES  --------------------------------------------------------------------------

// -- BME680 sensor instance
static Bsec bme680;

// -- PMS5003 sensor instance
static PMS pms(pmsSerial);

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

int32_t s32Msp_snapToHourDivisor(int32_t avg);

//*******************************************************************************************************************************

void Msp_getSystemStatus(systemStatus_t *stat)
{
  vMspOs_takeDataAccessMutex();
  memcpy(stat, &sysStat, sizeof(systemStatus_t));
  vMspOs_giveDataAccessMutex();
}

/*!< Transmission boundaries are anchored to minute 00 of every hour, so the measurement
     interval must be an integer divisor of 60 (60, 30, 20, 15, 10, 6, 5, 4, 3, 2, 1).
     Snaps invalid values down to the nearest valid divisor. */
int32_t s32Msp_snapToHourDivisor(int32_t avg)
{
  if (avg < AVG_MEASUREMENTS_MIN)
  {
    return AVG_MEASUREMENTS_MIN;
  }
  if (avg > MIN_IN_HOUR)
  {
    return MIN_IN_HOUR;
  }
  while ((MIN_IN_HOUR % avg) != 0)
  {
    avg--;
  }
  return avg;
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

  if (!bMspOs_initDataAccessMutex())
  {
    Serial.println("FATAL: mutex init failed — halting");
    while (true) { vTaskDelay(pdMS_TO_TICKS(1000)); }
  }

  // Initialize sensor data structures, network parameters, and defaults
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
  Wire.setTimeOut(2000); // prevent indefinite hang on I2C bus stuck

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
  log_i("ver was defined at compile time.");
#else
  log_i("ver is the default.");
#endif
#ifdef API_SECRET_SALT
  log_i("api_secret_salt was defined at compile time.");
#else
  log_i("api_secret_salt is the default.");
#endif
  //+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

  // STEP 0: OTA firmware validation
  log_i("=== STEP 0: OTA Firmware Validation ===");
  vHalFirmware_printOTAInfo();
  if (!bHalFirmware_validateCurrentFirmware())
  {
    log_e("Current firmware validation failed");
  }

  // STEP 1: Load complete system configuration from SD card
  log_i("=== STEP 1: Loading system configuration from SD card ===");
  vHalSdcard_readSD(&sysStat, &devinfo, &sensorData_accumulate, &measStat, &sysData);

  // Phase 2: Check for downloaded firmware and apply update if newer
  if (SD.exists(FIRMWARE_UPDATE_FILE_PATH))
  {
    log_i("Downloaded firmware file found - checking for update");
    if (bHalFirmware_checkAndApplyPendingUpdate(FIRMWARE_UPDATE_FILE_PATH))
    {
      log_w("No firmware update applied from firmware.bin");
    }
    // Always remove firmware file after processing
    if (SD.remove(FIRMWARE_UPDATE_FILE_PATH))
    {
      log_i("Firmware file cleaned up successfully");
    }
    else
    {
      log_w("Failed to remove firmware.bin");
    }
  }

  // STEP 2: Fill system configuration with SD data or defaults
  log_i("=== STEP 2: Configuring system from SD data or defaults ===");
  vMspInit_configureSystemFromSD(&sysData, &sysStat, &devinfo, &measStat);

  log_i("Config summary: SD=%s file=%s SSID=%s server=%s avg=%d modem=%s fwUpgrade=%s",
        sysStat.sdCard ? "OK" : "FAIL",
        sysStat.configuration ? "OK" : "FAIL",
        devinfo.ssid.c_str(),
        sysData.server.c_str(),
        measStat.avg_measurements,
        sysStat.use_modem ? "YES" : "NO",
        sysStat.fwAutoUpgrade ? "YES" : "NO");

  // STEP 3: Start the network task with complete configuration
  log_i("=== STEP 3: Starting network task ===");
  createNetworkEvents();
  initSendDataOp(&sysData, &sysStat, &devinfo);
  vTaskDelay(pdMS_TO_TICKS(2000)); // allow network task to start

  // Wait for network task to initialize
  vTaskDelay(pdMS_TO_TICKS(NETWORK_TASK_STARTUP_DELAY_MS));

  // The interval must divide the hour exactly so transmissions stay anchored to minute 00
  int32_t snapped_avg = s32Msp_snapToHourDivisor(measStat.avg_measurements);
  if (snapped_avg != measStat.avg_measurements)
  {
    log_w("avg_measurements %d is not a divisor of 60, snapped to %d", measStat.avg_measurements, snapped_avg);
    measStat.avg_measurements = snapped_avg;
  }
  measStat.max_measurements = measStat.avg_measurements; /*!< fill the max_measurements with the number set by the user */

#ifndef ENABLE_FIRMWARE_UPDATE_TESTS
  // Enable task watchdog for setup/loop task.
  // Timeout of 90 s covers the longest intentional delay (NTP sync wait, 60 s max).
  // Any unrecoverable hang (I2C bus stuck, infinite loop) triggers an auto-reboot.
  esp_task_wdt_init(90, true);
  esp_task_wdt_add(NULL);

  // STEP 4: Detect and initialize hardware sensors
  log_i("=== STEP 4: Detecting and initializing sensors ===");

  // BME680
  vMsp_updateDataAndSendEvent(DISP_EVENT_BME680_SENSOR_INIT, &sensorData_accumulate, &devinfo, &measStat, &sysData, &sysStat);
  bme680.begin(BME68X_I2C_ADDR_HIGH, Wire);
  if (tHalSensor_checkBMESensor(&bme680))
  {
    log_i("BME680 sensor detected, initializing...");
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
    log_e("BME680 sensor not detected!");
    vMsp_updateDataAndSendEvent(DISP_EVENT_BME680_SENSOR_ERR, &sensorData_accumulate, &devinfo, &measStat, &sysData, &sysStat);
  }

  // PMS5003 ++++++++++++++++++++++++++++++++++++
  pmsSerial.begin(PMS_SERIAL_BAUD, SERIAL_8N1, PMSERIAL_RX, PMSERIAL_TX); // baud, type, ESP_RX, ESP_TX
  delay(PMS_BOOT_DELAY_MS);
  vMsp_updateDataAndSendEvent(DISP_EVENT_PMS5003_SENSOR_INIT, &sensorData_accumulate, &devinfo, &measStat, &sysData, &sysStat);
  delay(PMS_INIT_DELAY_MS);
  if (pms.readUntil(data))
  {
    log_i("PMS5003 sensor detected, initializing...");
    sensorData_accumulate.status.PMS5003Sensor = true;
    vMsp_updateDataAndSendEvent(DISP_EVENT_PMS5003_SENSOR_OKAY, &sensorData_accumulate, &devinfo, &measStat, &sysData, &sysStat);
    measStat.isPmsAwake = true;
  }
  else
  {
    log_e("PMS5003 sensor not detected!");
    vMsp_updateDataAndSendEvent(DISP_EVENT_PMS5003_SENSOR_ERR, &sensorData_accumulate, &devinfo, &measStat, &sysData, &sysStat);
  }

  

  switch (sysStat.gasSensorType)
  {
  case GAS_SENSOR_MICS6814:
  {
    // Gas sensor — type selected from SD card configuration (MICS6814 or MICS4514)
    vMsp_updateDataAndSendEvent(DISP_EVENT_MICSxx14_SENSOR_INIT, &sensorData_accumulate, &devinfo, &measStat, &sysData, &sysStat);
    if (gas.begin())
    {
      log_i("MICS6814 sensor detected, initializing...");
      sensorData_accumulate.status.MICS6814Sensor = true;
      gas.powerOn(); // turn on heating element and LED
      gas.ledOn();
      vMsp_updateDataAndSendEvent(DISP_EVENT_MICSxx14_SENSOR_OKAY, &sensorData_accumulate, &devinfo, &measStat, &sysData, &sysStat);

      sensorR0Value_t r0Values;
      r0Values.redSensor = gas.getBaseResistance(CH_RED);
      r0Values.oxSensor  = gas.getBaseResistance(CH_OX);
      r0Values.nh3Sensor = gas.getBaseResistance(CH_NH3);

      if (tHalSensor_checkMicsValues(&sensorData_accumulate, &r0Values) == STATUS_OK)
      {
        log_i("MICS6814 R0 values are already at default");
        vMsp_updateDataAndSendEvent(DISP_EVENT_MICSxx14_VALUES_OKAY, &sensorData_accumulate, &devinfo, &measStat, &sysData, &sysStat);
      }
      else
      {
        log_i("Setting MICS6814 R0 values to default...");
        vMsp_updateDataAndSendEvent(DISP_EVENT_MICSxx14_DEF_SETTING, &sensorData_accumulate, &devinfo, &measStat, &sysData, &sysStat);
        vHalSensor_writeMicsValues(&sensorData_accumulate);
        vMsp_updateDataAndSendEvent(DISP_EVENT_MICSxx14_DONE, &sensorData_accumulate, &devinfo, &measStat, &sysData, &sysStat);
        log_i("MICS6814 R0 defaults written");
      }
      gas.setOffsets(&sensorData_accumulate.micsTuningData.sensingResInAirOffset.redSensor);
    }
    else
    {
      log_e("MICS6814 sensor not detected!");
      vMsp_updateDataAndSendEvent(DISP_EVENT_MICSxx14_SENSOR_ERR, &sensorData_accumulate, &devinfo, &measStat, &sysData, &sysStat);
    }
    break;
  }

  case GAS_SENSOR_MICS4514:
  {
    // Gas sensor — type selected from SD card configuration (MICS6814 or MICS4514)
    vMsp_updateDataAndSendEvent(DISP_EVENT_MICSxx14_SENSOR_INIT, &sensorData_accumulate, &devinfo, &measStat, &sysData, &sysStat);
    log_i("Initializing MICS4514 sensor (DFRobot SEN0377)...");
    if (mics4514.begin())
    {
      log_i("MICS4514 sensor detected, starting %d-min warmup...", MICS4514_WARMUP_TIME_MIN);
      sensorData_accumulate.status.MICS4514Sensor = true;
      mics4514.wakeUpMode();

      uint8_t loadingStep = 0;
      float delayInMs = MICS4514_WARMUP_POLL_MS;
      vMsp_updateDataAndSendEvent(DISP_EVENT_MICSxx14_SENSOR_OKAY, &sensorData_accumulate, &devinfo, &measStat, &sysData, &sysStat);
      vMsp_updateDataAndSendEvent(DISP_EVENT_MICSxx14_HEATING_UP, &sensorData_accumulate, &devinfo, &measStat, &sysData, &sysStat);
      while (mics4514.warmUpTime(MICS4514_WARMUP_TIME_MIN) == false)
      {
        // stay here, until the sensor get heaten up.
        esp_task_wdt_reset(); // warmup takes ~2 min; reset WDT each 5 s slice
        printf("MIXS4514 - Heat-Up: %3.1f %%\n", (float)((loadingStep * delayInMs) / ((MICS4514_WARMUP_TIME_MIN * (float)SEC_IN_MIN * (float)MS_IN_SEC)) * PERCENT_DIVISOR));
        vTaskDelay(pdMS_TO_TICKS((int32_t)delayInMs));
        loadingStep++;
      }

      // Initialize MICS4514 specific data
      sensorData_accumulate.mics4514Data.warmupComplete = 0;
      sensorData_accumulate.mics4514Data.powerState = 1;
      vMsp_updateDataAndSendEvent(DISP_EVENT_MICSxx14_DONE, &sensorData_accumulate, &devinfo, &measStat, &sysData, &sysStat);
      log_i("MICS4514 initialization complete!");
    }
    else
    {
      log_e("MICS4514 sensor not detected!");
      vMsp_updateDataAndSendEvent(DISP_EVENT_MICSxx14_SENSOR_ERR, &sensorData_accumulate, &devinfo, &measStat, &sysData, &sysStat);
    }
    break;
  }

  case GAS_SENSOR_MSP1:
  {
    // Gas sensor — type selected from SD card configuration (MICS6814 or MICS4514)
    vMsp_updateDataAndSendEvent(DISP_EVENT_MSP1_SENSOR_INIT, &sensorData_accumulate, &devinfo, &measStat, &sysData, &sysStat);
    if (tMsp1_init() != STATUS_OK)
    {
      log_e("MSP1 - INIT ERROR");
      vMsp_updateDataAndSendEvent(DISP_EVENT_MSP1_SENSOR_ERR, &sensorData_accumulate, &devinfo, &measStat, &sysData, &sysStat);
    }
    else
    {
      sensorData_accumulate.status.MSP1Sensor = true;
      vMsp_updateDataAndSendEvent(DISP_EVENT_MSP1_SENSOR_OKAY, &sensorData_accumulate, &devinfo, &measStat, &sysData, &sysStat);
      log_i("MSP1 - INIT OK");
    }
    vMsp1_printStatus();
    break;
  }

  default:
  {
    log_e("Unknown gas sensor type: %d", sysStat.gasSensorType);
    vMsp_updateDataAndSendEvent(DISP_EVENT_MICSxx14_SENSOR_ERR, &sensorData_accumulate, &devinfo, &measStat, &sysData, &sysStat);
    break;
  }
  }

#ifdef MSP1_DEBUG_ENABLE
  {
    /* Ensure MSP1 peripherals are initialised for the debug display even when
     * the SD-card config does not select GAS_SENSOR_MSP1. */
    if (!bMsp1_isReady())
    {
      log_i("MSP1_DEBUG_ENABLE: MSP1 not yet initialised — forcing init");
      if (tMsp1_init() != STATUS_OK)
      {
        log_e("MSP1_DEBUG_ENABLE: MSP1 init failed");
      }
      else
      {
        // Otherwise the debug MSP1 pages show "--": status.MSP1Sensor is set
        // only by the GAS_SENSOR_MSP1 branch above, which does not run here.
        sensorData_accumulate.status.MSP1Sensor = true;
      }
    }
  }
#endif

  // O3 ozone sensor (analog)
  vMsp_updateDataAndSendEvent(DISP_EVENT_O3_SENSOR_INIT, &sensorData_accumulate, &devinfo, &measStat, &sysData, &sysStat);
  if (!tHalSensor_isAnalogO3Connected())
  {
    log_e("O3 sensor not detected!");
    vMsp_updateDataAndSendEvent(DISP_EVENT_O3_SENSOR_ERR, &sensorData_accumulate, &devinfo, &measStat, &sysData, &sysStat);
    sensorData_accumulate.status.O3Sensor = false;
  }
  else
  {
    log_i("O3 sensor detected, running...");
    sensorData_accumulate.status.O3Sensor = true;
    vMsp_updateDataAndSendEvent(DISP_EVENT_O3_SENSOR_OKAY, &sensorData_accumulate, &devinfo, &measStat, &sysData, &sysStat);
  }
#endif

  // Always use 1-minute intervals for measurements
  measStat.delay_between_measurements = SEC_IN_MIN;

  /*!< Reset measurement count */
  measStat.measurement_count = 0;
  measStat.avg_measurements = measStat.max_measurements;
  measStat.data_transmitted = false; // Initialize transmission flag

  sysData.sent_ok = false;
  sysData.ntp_last_sync_day = INVALID_DAY; // Force initial NTP sync
  sysData.server_config_received = false; // No server config yet
  sysData.server_config_response = "";    // Empty config response

  memset(sysData.Date, 0, sizeof(sysData.Date));
  memset(sysData.Time, 0, sizeof(sysData.Time));
  measStat.curr_minutes = 0;
  measStat.curr_seconds = 0;
  measStat.curr_total_seconds = 0;
  measStat.last_transmission_epoch = 0; // 0 = never transmitted
  measStat.next_boundary_msm = 0;       // Will be computed at first measurement cycle start

  // Seed the display struct with the boot-time detection results. Without this
  // the carousel shows "--" for every sensor until the first measurement cycle
  // re-syncs it (SYS_STATE_READ_SENSORS), which with MSP1 is a full minute of
  // "sensor absent" for hardware that was detected fine.
  sensorData_single.status = sensorData_accumulate.status;

  if ((sysStat.configuration == true) || (sysStat.server_ok == true))
  {
    log_i("Configuration OK — requesting network connection");
    mainStateMachine.current_state = SYS_STATE_UPDATE_CONFIG_FROM_SERVER;
    mainStateMachine.next_state = SYS_STATE_UPDATE_CONFIG_FROM_SERVER;
    vMsp_updateDataAndSendEvent(DISP_EVENT_WAIT_FOR_NETWORK_CONN, &sensorData_accumulate, &devinfo, &measStat, &sysData, &sysStat);
  }
  else
  {
    log_e("No valid configuration — entering error state");
    mainStateMachine.current_state = SYS_STATE_ERROR;
    mainStateMachine.next_state = SYS_STATE_ERROR;
  }

  mainStateMachine.isFirstTransition = true;
  memcpy(&sensorData_single, &sensorData_accumulate, sizeof(sensorData_t));
}

void loop()
{
  esp_task_wdt_reset(); // feed WDT each iteration; a blocked state triggers auto-reboot

#ifdef MSP1_DEBUG_ENABLE
  {
    /* Debug read cycle (~1 s): measure heater, run PI step, accumulate sensor
     * samples, compute average, then push a display update.  No data is
     * transmitted, logged, or saved. */
    tMsp1_measureHeater();
    tMsp1_heaterControlStep(); /* holds R_heat at the configured R@Tmax */

    {
      msp1Data_t dbgSnap;
      tMsp1_getDataCopy(&dbgSnap);
      Serial.printf("[MSP1 RAW] CH1(heater) vR14=%.2f mV\n", dbgSnap.heater.vR14_mV);
    }

    unsigned long tDebugStart = millis();
    while (millis() - tDebugStart < 3000UL)
    {
      tMsp1_accumulateSensor();
      {
        msp1Data_t dbgSnap;
        tMsp1_getDataCopy(&dbgSnap);
        // Serial.printf("[MSP1 RAW] CH0(sensor)=%.2f mV%s  CH1(heater) vR14=%.2f mV\n",
        //               dbgSnap.sensor.lastRaw_mV,
        //               dbgSnap.sensor.saturated ? " [SAT]" : "",
        //               dbgSnap.heater.vR14_mV);
      }
      delay(100);
      esp_task_wdt_reset();
    }

    tMsp1_computeSensorAverage();
    tMsp1_computeRno2();

    vMsp_updateDataAndSendEvent(DISP_EVENT_SHOW_MEAS_DATA, &sensorData_accumulate, &devinfo, &measStat, &sysData, &sysStat);
    return;
  }
#endif

  // MSP1 background service: heater measure + PI step + one CH0 sample into
  // ring/accumulator, once per loop() pass in EVERY measurement-cycle state —
  // not just SYS_STATE_WAIT_FOR_TIMEOUT — so the heater stays in closed loop
  // and Rno2 keeps integrating also across EVAL/SEND/config/FOTA passes.
  // Excluded: SYS_STATE_READ_SENSORS (its MSP1 case runs the same sequence
  // itself as part of the measurement) and pre-cycle boot states (their
  // passes block tens of seconds on network events — the power limiter in
  // tMsp1_measureHeater() would react far too slowly to a soft-start
  // overshoot). This is the only site that touches the NAU7802 mux between
  // measurements: one heater->sensor switch per pass, each paying the ~1 s
  // PGA settle flush (see s_selectChannel/MSP1_NAU7802_CHANNEL_FLUSH_READS
  // in msp1.cpp), so a full pass costs ~2.5 s.
  if (sysStat.gasSensorType == GAS_SENSOR_MSP1)
  {
    switch (mainStateMachine.current_state)
    {
    case SYS_STATE_WAIT_FOR_TIMEOUT:
    case SYS_STATE_EVAL_SENSOR_STATUS:
    case SYS_STATE_SEND_DATA:
    case SYS_STATE_UPDATE_CONFIG_FROM_SERVER:
    case SYS_STATE_FW_VERSION_CHECK:
    {
      if (bMsp1_isReady())
      {
        // Each step only runs if the previous one succeeded: heaterControlStep
        // would act on a stale current reading if measureHeater failed, and
        // skipping accumulateSensor too keeps each pass's outcome simple to
        // reason about (either the whole heat->sensor read succeeded, or we
        // log exactly which step broke and retry next pass).
        if (tMsp1_measureHeater() == STATUS_OK)
        {
          if (tMsp1_heaterControlStep() == STATUS_OK)
          {
            if (tMsp1_accumulateSensor() != STATUS_OK)
            {
              log_w("MSP1 service: accumulateSensor failed, skipping this pass's CH0 sample");
            }
          }
          else
          {
            log_w("MSP1 service: heaterControlStep failed, skipping CH0 sample this pass");
          }
        }
        else
        {
          log_w("MSP1 service: measureHeater failed, skipping PI step and CH0 sample this pass");
        }
      }
      break;
    }
    default:
      break;
    }
  }

  switch (mainStateMachine.current_state)
  {
  case SYS_STATE_UPDATE_CONFIG_FROM_SERVER:
  {
    log_i("Checking for server configuration updates from network task...");
    log_i("SD Card status: %s", sysStat.sdCard ? "OK" : "FAIL");

    // Check queue for server config (non-blocking)
    server_config_msg_t configMsg;
    if (dequeueServerConfig(&configMsg, 0))
    {
      if (configMsg.valid && configMsg.response_length > 0)
      {
        log_i("=== SERVER CONFIGURATION RECEIVED FROM NETWORK TASK ===");
        log_i("Config response length: %d bytes", configMsg.response_length);
        log_i("Server config JSON: %s", configMsg.json_response);

        // Convert to String for processing
        String configJson = String(configMsg.json_response);

        // Step 1: Parse server JSON and update system structures
        if (bHalSdcard_updateFromServerConfig(configJson, &devinfo, &sensorData_accumulate, &measStat, &sysStat, &sysData))
        {
          log_i("System structures updated successfully from server");

          // The interval must divide the hour exactly so transmissions stay anchored to minute 00
          int32_t snapped_avg = s32Msp_snapToHourDivisor(measStat.avg_measurements);
          if (snapped_avg != measStat.avg_measurements)
          {
            log_w("Server avg_measurements %d is not a divisor of 60, snapped to %d", measStat.avg_measurements, snapped_avg);
            measStat.avg_measurements = snapped_avg;
          }

          // The server is the source of truth for the configured interval
          measStat.max_measurements = measStat.avg_measurements;
          log_i("Updated max_measurements to %d for boundary alignment", measStat.max_measurements);

          // Step 2: Write updated system structures to config_v4.json
          log_i("Attempting to write configuration to SD card...");
          log_i("Pre-write SD Card status check: %s", sysStat.sdCard ? "OK" : "FAIL");

          if (bHalSdcard_writeConfig(&devinfo, &sensorData_accumulate, &measStat, &sysStat, &sysData))
          {
            log_i("=== Configuration file written successfully to SD card ===");
            log_i("config_v4.json has been updated with server values");
          }
          else
          {
            log_e("=== FAILED to write configuration file to SD card ===");
            log_e("SD Card status: %s", sysStat.sdCard ? "OK" : "FAIL");
            log_e("Please check SD card availability and write permissions");
            log_w("Config applied to RAM but NOT saved to SD card");
          }
        }
        else
        {
          log_e("Failed to update system structures from server response");
          log_e("Check server JSON format and field names");
        }

        log_i("Configuration update processing complete");
      }
      else
      {
        log_w("Received invalid server config message from queue");
      }
    }
    else
    {
      log_i("No new server configuration available from network task");
    }

    // Always proceed to NTP sync after config check
    mainStateMachine.next_state = SYS_STATE_WAIT_FOR_NTP_SYNC;
    mainStateMachine.isFirstTransition = true;
    mainStateMachine.prev_state = SYS_STATE_UPDATE_CONFIG_FROM_SERVER;
    break;
  }
  case SYS_STATE_WAIT_FOR_NTP_SYNC:
  {
    log_i("Waiting for NTP synchronization...");
    bool isConnected = false;
    bool isTimeValid = getLocalTime(&timeinfo);

    int current_day = timeinfo.tm_yday; // Day of year (0-365)

    if (mainStateMachine.isFirstTransition == true)
    {

      log_i("NTP SYNC CHECK - current_day:%d - last_sync_day:%d", current_day, sysData.ntp_last_sync_day);
      // Check if daily NTP sync is needed (at 00:00:00)
      if ((current_day == sysData.ntp_last_sync_day) && (isTimeValid == true))
      {
        log_i("NTP in Sync >>> SYS_STATE_FW_VERSION_CHECK");
        mainStateMachine.next_state = SYS_STATE_FW_VERSION_CHECK;
        mainStateMachine.isFirstTransition = true;
        mainStateMachine.prev_state = SYS_STATE_WAIT_FOR_NTP_SYNC;
        break;
      }
      // If internet is not connected and a sync is needed, trigger a connection request
      if (isNetworkConnected() == false)
        requestNetworkConnection();

      // sensorData_single, not _accumulate: accumulate holds running sums and is
      // zeroed right after SEND_DATA — this state runs immediately after it, so
      // it published all-zero readings on the daily NTP resync.
      vMsp_updateDataAndSendEvent(DISP_EVENT_WAIT_FOR_NETWORK_CONN, &sensorData_single, &devinfo, &measStat, &sysData, &sysStat);
      mainStateMachine.isFirstTransition = false;
    }

    // Wait for the network connection event
    if ((pdTRUE == waitForNetworkEvent(NET_EVENT_CONNECTED, pdMS_TO_TICKS(NETWORK_EVENT_WAIT_MS))) || (isNetworkConnected() == true))
    {
      isConnected = true;
    }

    if (isConnected || isNetworkConnected())
    {
      log_i("NTP sync Triggered");
      requestTimeSync();
    }
    else
    {
      log_v("Still waiting for Connection...");
      mainStateMachine.next_state = SYS_STATE_WAIT_FOR_NTP_SYNC;
      break;
    }

    if (pdTRUE == waitForNetworkEvent(NET_EVENT_TIME_SYNCED, pdMS_TO_TICKS(60000)))
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

      log_i("=== Firmware Update Test Completed ===");
#endif

      mainStateMachine.next_state = SYS_STATE_WAIT_FOR_TIMEOUT;
      mainStateMachine.isFirstTransition = true;
      mainStateMachine.prev_state = SYS_STATE_WAIT_FOR_NTP_SYNC;
      log_i("NTP sync >>>> SYS_STATE_WAIT_FOR_TIMEOUT");
    }
    else
    {
      // Keep waiting - network task is still working on connection and sync
      log_v("Still waiting for NTP sync...");
      mainStateMachine.next_state = SYS_STATE_WAIT_FOR_NTP_SYNC;
    }
    break;
  }
  case SYS_STATE_FW_VERSION_CHECK:
  {
#if defined(SKIP_FIRMWARE_UPDATE)
    // FOTA disabled at compile time (see config.h): never evaluate updates
    // online, go straight to the timeout state.
    log_i("SKIP_FIRMWARE_UPDATE active >>>> SYS_STATE_WAIT_FOR_TIMEOUT");
    mainStateMachine.next_state = SYS_STATE_WAIT_FOR_TIMEOUT;
    mainStateMachine.prev_state = SYS_STATE_FW_VERSION_CHECK;
    mainStateMachine.isFirstTransition = true;
#else
    static int last_fw_check_day = INVALID_DAY;
    static int current_day = INVALID_DAY;
    if (mainStateMachine.isFirstTransition == true)
    {
      // Check for firmware updates after midnight data transmission
      if (getLocalTime(&timeinfo))
      {
        current_day = timeinfo.tm_yday; // Day of year (0-365)

        // Check if this is midnight hour transmission (00:xx) and we haven't checked today
        // Allow firmware check for any minute in the midnight hour, not just 00:00
        if (sysStat.fwAutoUpgrade &&
            ((last_fw_check_day == INVALID_DAY) ||
             ((timeinfo.tm_hour == 0) && (current_day != last_fw_check_day))))
        {
          log_d("Firmware check started: hour=%d, min=%d, day=%d, last_check_day=%d, fwAutoUpgrade=%s",
                timeinfo.tm_hour, timeinfo.tm_min, current_day, last_fw_check_day,
                sysStat.fwAutoUpgrade ? "true" : "false");
          // the firmware update is needed, trigger an internet connection if disconnected
          if (isNetworkConnected() == false)
            requestNetworkConnection();
          mainStateMachine.isFirstTransition = false;
        }
        else
        {
          // Enhanced logging to understand why firmware check was skipped
          log_d("Firmware check skipped: hour=%d, min=%d, day=%d, last_check_day=%d, fwAutoUpgrade=%s",
                timeinfo.tm_hour, timeinfo.tm_min, current_day, last_fw_check_day,
                sysStat.fwAutoUpgrade ? "true" : "false");
          log_i("SYS_STATE_FW_VERSION_CHECK >>>> SYS_STATE_WAIT_FOR_TIMEOUT");
          mainStateMachine.next_state = SYS_STATE_WAIT_FOR_TIMEOUT;
          mainStateMachine.prev_state = SYS_STATE_FW_VERSION_CHECK;
          mainStateMachine.isFirstTransition = true;
          current_day = INVALID_DAY;
        }
      }
    }
    else
    {
      if ((pdTRUE == waitForNetworkEvent(NET_EVENT_CONNECTED, pdMS_TO_TICKS(NETWORK_EVENT_WAIT_MS))) || (isNetworkConnected() == true))
      {
        // The Firmware Update is needed, waiting for the network connection
        if (true == bHalFirmware_checkForUpdates(&sysData, &sysStat, &devinfo))
        {
          last_fw_check_day = current_day;
          log_i("Firmware update check completed for day %d", current_day);
          log_i("SYS_STATE_FW_VERSION_CHECK >>>> SYS_STATE_WAIT_FOR_TIMEOUT");
        }
        else
        {
          log_w("Firmware update check failed for day %d", current_day);
        }
      }
      else
      {
        log_w("Internet connectivity ERROR!!");
        log_w("SKIP FOTA CHECK >>>> SYS_STATE_WAIT_FOR_TIMEOUT");
      }
      mainStateMachine.next_state = SYS_STATE_WAIT_FOR_TIMEOUT;
      mainStateMachine.prev_state = SYS_STATE_FW_VERSION_CHECK;
      mainStateMachine.isFirstTransition = true;
      current_day = INVALID_DAY;
    }
#endif // SKIP_FIRMWARE_UPDATE

    vMsp_updateDataAndSendEvent(DISP_EVENT_WAIT_FOR_TIMEOUT, &sensorData_single, &devinfo, &measStat, &sysData, &sysStat);
    break;
  }
    
  case SYS_STATE_WAIT_FOR_TIMEOUT:
  {
    // MSP1 servicing (heater measure + PI + CH0 sample) runs at the top of
    // loop() for every measurement-cycle state — see the background service
    // block before the state switch. Here only the idle display refresh.
    //
    // Fixed cadence for EVERY gas sensor type, not gated on MSP1 producing
    // samples: such a gate never fires with MSP1 absent or faulted, so the
    // carousel received no sensor data at all and kept redrawing a stale
    // snapshot — including boot-time ones whose status flags predate sensor
    // detection, which is why pulling MSP1 made the BME680 page show "--" for
    // a sensor that was working.
    //
    // sensorData_single, not sensorData_accumulate: accumulate holds running
    // SUMS (divided by performAverages() only inside SYS_STATE_SEND_DATA, for
    // the server) and is zeroed at the start of every cycle. The display shows
    // instant readings.
    static unsigned long lastDisplayRefresh = 0;
    if ((millis() - lastDisplayRefresh) > DISPLAY_IDLE_DELAY_MS)
    {
      lastDisplayRefresh = millis();
      vMsp_updateDataAndSendEvent(DISP_EVENT_SHOW_MEAS_DATA, &sensorData_single, &devinfo, &measStat, &sysData, &sysStat);
    }

    // Periodic SD card presence check
    static unsigned long lastSdCheck = 0;
    if (millis() - lastSdCheck > SD_CARD_CHECK_INTERVAL_MS)
    {
      lastSdCheck = millis();
      vHalSdcard_periodicCheck(&sysStat, &devinfo);
    }

    if (getLocalTime(&timeinfo))
    {
      measStat.curr_minutes = timeinfo.tm_min;
      measStat.curr_seconds = timeinfo.tm_sec;
      measStat.curr_total_seconds = measStat.curr_minutes * SEC_IN_MIN + measStat.curr_seconds;
      measStat.timeout_seconds = ((measStat.curr_total_seconds + measStat.additional_delay) % measStat.delay_between_measurements);

      // Fire once per measurement period (delay_between_measurements seconds
      // long), not on an exact :00-second match: the MSP1 heater/sensor
      // service above (channel-switch settle flush, bounded ADC-timeout
      // retries) makes each pass through this state take anywhere from
      // ~2 s to several seconds, so an exact-second check can step right
      // over the single target second and never fire. What matters is
      // catching the correct period at all, any second within it is fine.
      // currentPeriodIndex is derived from the monotonic epoch (not
      // curr_total_seconds, which wraps every hour), so it's safe across
      // hour/day boundaries and naturally dedupes repeat passes within the
      // same period — no separate edge-triggered lock needed.
      time_t nowEpoch = mktime(&timeinfo);
      long currentPeriodIndex = (long)((nowEpoch + measStat.additional_delay) / measStat.delay_between_measurements);

      static long s_lastFiredPeriodIndex = -1;
      if (currentPeriodIndex == s_lastFiredPeriodIndex)
      {
        mainStateMachine.next_state = SYS_STATE_WAIT_FOR_TIMEOUT;
        break;
      }
      s_lastFiredPeriodIndex = currentPeriodIndex;

      // The period index above IS the trigger — do NOT re-gate on
      // measStat.timeout_seconds == 0 (exact :00 second). The index is consumed
      // (s_lastFiredPeriodIndex) by the first pass entering the period, so an
      // additional exact-second condition kills the period for good whenever
      // that pass does not land on second 00 — which, with MSP1 servicing
      // making a pass take seconds, is nearly always: READ_SENSORS then never
      // runs and every measurement stays at zero.
      log_i("Timeout expired!");
      log_i("Current time: %02d:%02d:%02d", timeinfo.tm_hour, measStat.curr_minutes, measStat.curr_seconds);

      mainStateMachine.prev_state = SYS_STATE_WAIT_FOR_TIMEOUT;
      mainStateMachine.next_state = SYS_STATE_READ_SENSORS; // go to read sensors state
    }
    else
    {
      log_e("Failed to obtain time!");
      mainStateMachine.next_state = SYS_STATE_WAIT_FOR_TIMEOUT;
    }
    break;
  }
    
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

      // Compute the next clock-aligned transmission boundary (minutes since midnight).
      // Formula: next_boundary = ((start_msm / avg_meas) + 1) * avg_meas
      // avg_measurements is guaranteed to be an integer divisor of 60, so boundaries are
      // always anchored to minute 00 of every hour (e.g. avg=5 → :00, :05, ..., :55).
      // Example: avg=3, start=13:03 (msm=783) → (783/3+1)*3 = 786 → 13:06
      {
        int32_t start_msm = timeinfo.tm_hour * MIN_IN_HOUR + timeinfo.tm_min;
        measStat.next_boundary_msm = ((start_msm / measStat.avg_measurements) + 1) * measStat.avg_measurements;
        log_i("NEW CYCLE: start %02d:%02d, first TX at msm=%d (%02d:%02d)",
              timeinfo.tm_hour, timeinfo.tm_min,
              measStat.next_boundary_msm,
              measStat.next_boundary_msm / MIN_IN_HOUR,
              measStat.next_boundary_msm % MIN_IN_HOUR);
      }
      err.BMEfails = 0;
      err.PMSfails = 0;
      err.MICSfails = 0;
      err.O3fails = 0;
      err.count = 0;
      // senserrs[] is documented as per-cycle but was only ever set (in
      // performAverages), never cleared: one failed cycle latched a sensor as
      // "in error" for the rest of the uptime. Its consumer in
      // SYS_STATE_EVAL_SENSOR_STATUS then re-asserted status.X every cycle
      // forever, masking any later failure of that same sensor.
      memset(err.senserrs, 0, sizeof(err.senserrs));

      // Sync sensor detection status to sensorData_single so the display always
      // reflects the current hardware state for instant readings.
      sensorData_single.status = sensorData_accumulate.status;

      log_i("NEW MEASUREMENT CYCLE: Starting fresh — will collect %d measurements (1 per minute)", measStat.avg_measurements);
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

    // sensorData_single is NOT cleared here. It used to be, and the event right
    // below then published an all-zero snapshot the display task latched into
    // lastValidSensorData and redrew on the carousel for the whole duration of
    // this state (seconds, with MSP1 channel-switch settles) — one "everything
    // is 0,00" frame per measurement cycle. Each sensor block below overwrites
    // its own fields on success, and on failure raises fault.X, which the draw
    // functions already render as FAULT instead of a stale value.
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
          delay(SENSOR_RETRY_DELAY_MS);
          continue;
        }

        // Trigger measurement if this is the first attempt
        if (retry == 0)
        {
          // Give BME680 time to prepare measurement (especially gas sensor)
          delay(BME680_MEAS_PREP_DELAY_MS);
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
          delay(BME680_NOT_READY_DELAY_MS);
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

      // Per-measurement fault flag: FAULT on display instead of the stale
      // previous value when every retry failed this measurement.
      sensorData_single.fault.BME680Sensor = !sensor_read_success;
      sensorData_accumulate.fault.BME680Sensor = !sensor_read_success;

      if (sensor_read_success)
      {
        log_i("BME680 measurement #%d completed successfully", measStat.measurement_count + 1);
      }
    }

    // MSP1 heater: the loop()-top background service excludes
    // SYS_STATE_READ_SENSORS, so everything above (accumulation reset,
    // display update, BME680 sampling with its retry delays) runs with no PI
    // correction at all — up to a few seconds. Service it once more here so
    // the MSP1 case below starts from a freshly corrected heater resistance
    // instead of whatever it drifted to during that gap.
    if (sysStat.gasSensorType == GAS_SENSOR_MSP1)
    {
      if (bMsp1_isReady())
      {
        tMsp1_measureHeater();
        (void)tMsp1_heaterControlStep();
      }
    }

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
            delay(SENSOR_RETRY_DELAY_MS);
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

        sensorData_single.fault.MICS6814Sensor = !mics_read_success;
        sensorData_accumulate.fault.MICS6814Sensor = !mics_read_success;

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
            delay(SENSOR_RETRY_DELAY_MS);
            continue;
          }

          // Also calculate immediate gas concentrations for real-time display (sensorData_single)
          // This provides instant feedback for LCD while accumulating for accurate averaging
          if (tHalSensor_calculateMICS4514_Immediate(mics4514, &sensorData_accumulate, &micsLocData) == STATUS_OK)
          {
            // Convert PPM to ug/m3 for immediate display values (same as old approach)
            micsLocData.carbonMonoxide = vGeneric_convertPpmToUgM3(micsLocData.carbonMonoxide, sensorData_accumulate.molarMass.carbonMonoxide);
            log_v("CO(ug/m3): %.3f", micsLocData.carbonMonoxide);
            sensorData_single.pollutionData.carbonMonoxide = micsLocData.carbonMonoxide;

            // Convert NO2 from PPM to ug/m3 (with optional BME680 compensation)
            micsLocData.nitrogenDioxide = vGeneric_convertPpmToUgM3(micsLocData.nitrogenDioxide, sensorData_accumulate.molarMass.nitrogenDioxide);
            if (sensorData_accumulate.status.BME680Sensor)
            {
              micsLocData.nitrogenDioxide = fHalSensor_no2AndVocCompensation(micsLocData.nitrogenDioxide, &localData, &sensorData_accumulate);
            }
            log_v("NOx(ug/m3): %.3f", micsLocData.nitrogenDioxide);
            sensorData_single.pollutionData.nitrogenDioxide = micsLocData.nitrogenDioxide;

            // Convert NH3 from PPM to ug/m3
            micsLocData.ammonia = vGeneric_convertPpmToUgM3(micsLocData.ammonia, sensorData_accumulate.molarMass.ammonia);
            log_v("NH3(ug/m3): %.3f", micsLocData.ammonia);
            sensorData_single.pollutionData.ammonia = micsLocData.ammonia;

            log_i("MICS4514 immediate values for display: CO=%.2f, NO2=%.2f, NH3=%.2f ug/m3",
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

        sensorData_single.fault.MICS4514Sensor = !mics_read_success;
        sensorData_accumulate.fault.MICS4514Sensor = !mics_read_success;

        if (mics_read_success)
        {
          log_i("MICS4514 measurement #%d completed successfully", measStat.measurement_count + 1);
        }
        break;
      }
      break;
    }
    case GAS_SENSOR_MSP1:
    {
      // Same shape as the other sensors: detection status gates the read, the
      // live NAU7802/MCP4725 readiness flags are the runtime check on top of it.
      if (sensorData_accumulate.status.MSP1Sensor && bMsp1_isReady())
      {
        log_i("Sampling MICS-2714 NO2 sensor (MSP1)...");
        err.count = 0;
        bool msp1_read_success = false;

        for (int retry = 0; retry < MAX_SENSOR_RETRIES; retry++)
        {
          /* Step 1 — update heater measurement (NAU7802 CH2) */
          if (tMsp1_measureHeater() != STATUS_OK)
          {
            err.count++;
            log_w("MSP1 heater measure failed, attempt %d/%d", retry + 1, MAX_SENSOR_RETRIES);
            if (retry == (MAX_SENSOR_RETRIES - 1))
            {
              log_e("MSP1 heater measurement failed after %d attempts!", MAX_SENSOR_RETRIES);
              err.MICSfails++;
              break;
            }
            delay(1000);
            continue;
          }

          /* Step 2 — control step: hold R_heat at the configured R@Tmax */
          (void)tMsp1_heaterControlStep();

          /* Step 3 — accumulate one NO2 sample (NAU7802 CH1) */
          if (tMsp1_accumulateSensor() != STATUS_OK)
          {
            err.count++;
            log_w("MSP1 sensor ADC not ready, attempt %d/%d", retry + 1, MAX_SENSOR_RETRIES);
            if (retry == (MAX_SENSOR_RETRIES - 1))
            {
              log_e("MSP1 sensor accumulation failed after %d attempts!", MAX_SENSOR_RETRIES);
              err.MICSfails++;
              break;
            }
            delay(100); /* NAU7802 @ 10 SPS: next sample in ~100 ms */
            continue;
          }

          msp1_read_success = true;

          /* Step 4 — Rno2/R0 for this measurement, accumulated into
           * pollutionData.nitrogenDioxide the same way MICS6814/4514 sum
           * their per-measurement readings (+=) for SYS_STATE_SEND_DATA to
           * divide by measurement_count — not a live snapshot overwrite.
           * tMsp1_peekRno2Ratio() works off the sliding-window average of the
           * last MSP1_DISPLAY_AVG_SAMPLES CH0 samples, so each measurement
           * reflects the sensor around its own timeout instant (~12 s memory)
           * instead of a cumulative window average, whose memory grows with
           * the transmission window and makes the transmitted mean
           * early-sample-weighted (a mid-window gas step showed only ~16%
           * instead of 50%). */
          float fRno2Ratio = 0.0f;
          if (tMsp1_peekRno2Ratio(&fRno2Ratio) == STATUS_OK)
          {
            sensorData_accumulate.pollutionData.nitrogenDioxide += fRno2Ratio;
            sensorData_single.pollutionData.nitrogenDioxide = fRno2Ratio;

            log_v("MSP1 NO2: Rno2/R0=%.4f", fRno2Ratio);
          }

          msp1Data_t msp1Snap;
          tMsp1_getDataCopy(&msp1Snap);
          log_i("MSP1 meas #%d: sample #%lu accumulated — heater %.2f mA (%.1f mW theor.)",
                measStat.measurement_count + 1,
                msp1Snap.sensor.sampleCount,
                msp1Snap.heater.current_mA,
                msp1Snap.heater.power_mW);
          break;
        }

        sensorData_single.fault.MSP1Sensor = !msp1_read_success;
        sensorData_accumulate.fault.MSP1Sensor = !msp1_read_success;

        if (msp1_read_success)
        {
          log_i("MSP1 measurement #%d completed successfully", measStat.measurement_count + 1);
        }
      }
      else if (sensorData_accumulate.status.MSP1Sensor)
      {
        // Detected at boot but not answering now: count it as a failed
        // measurement and raise FAULT, instead of skipping the whole block in
        // silence and transmitting an Rno2 average of nothing.
        msp1Data_t msp1Snap;
        tMsp1_getDataCopy(&msp1Snap);
        log_e("MSP1 not ready (NAU7802=%u, MCP4725=%u) — measurement #%d skipped",
              msp1Snap.nau7802Ready, msp1Snap.mcp4725Ready, measStat.measurement_count + 1);
        err.MICSfails++;
        sensorData_single.fault.MSP1Sensor = true;
        sensorData_accumulate.fault.MSP1Sensor = true;
      }
      break;
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
          delay(SENSOR_RETRY_DELAY_MS);
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

      sensorData_single.fault.O3Sensor = !o3_read_success;
      sensorData_accumulate.fault.O3Sensor = !o3_read_success;

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
          delay(SENSOR_RETRY_DELAY_MS);
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

      sensorData_single.fault.PMS5003Sensor = !pms_read_success;
      sensorData_accumulate.fault.PMS5003Sensor = !pms_read_success;

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
    log_i("Current msm=%d, next TX boundary at msm=%d (%02d:%02d)",
          timeinfo.tm_hour * MIN_IN_HOUR + measStat.curr_minutes,
          measStat.next_boundary_msm,
          measStat.next_boundary_msm / MIN_IN_HOUR,
          measStat.next_boundary_msm % MIN_IN_HOUR);

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
        case SENS_STAT_MSP1:
          sensorData_accumulate.status.MSP1Sensor = true;
          sensorData_single.status.MSP1Sensor = true;
          break;
        }
      }
    }

    // Clock-aligned transmission: send when the current time reaches the pre-computed boundary.
    // Boundary is set at cycle start: next multiple of avg_measurements minutes since midnight,
    // always anchored to minute 00 of every hour (avg_measurements divides 60).
    int32_t curr_msm = timeinfo.tm_hour * MIN_IN_HOUR + measStat.curr_minutes;

    time_t now_epoch = mktime(&timeinfo);

    // Compute seconds elapsed since last transmission (0 if never transmitted)
    int32_t seconds_since_last_tx = (measStat.last_transmission_epoch > 0)
                                        ? (int32_t)(now_epoch - measStat.last_transmission_epoch)
                                        : 0;

    int32_t interval_seconds = (int32_t)(measStat.max_measurements * SEC_IN_MIN);

    // Fallback: enough seconds have elapsed since the last transmission regardless of the
    // pre-computed boundary. Recovers gracefully if the exact boundary minute is missed
    // due to processing delays or clock adjustments.
    bool enough_time_passed = (measStat.last_transmission_epoch > 0) &&
                              (seconds_since_last_tx >= interval_seconds);

    // Duplicate guard: never transmit twice within the same minute. The boundary is
    // re-computed per cycle and always in the future, so this only protects against
    // re-entering the check before the clock moves past the boundary minute.
    bool boundary_not_transmitted = (measStat.last_transmission_epoch == 0) ||
                                    (seconds_since_last_tx >= SEC_IN_MIN);

    log_i("TRANSMISSION CHECK: curr_msm=%d, next_boundary_msm=%d, collected %d measurements, last_tx_epoch=%lld, secs_since_tx=%d",
          curr_msm, measStat.next_boundary_msm, measStat.measurement_count,
          (long long)measStat.last_transmission_epoch, seconds_since_last_tx);

    // Handle midnight rollover: next_boundary_msm may be >= MIN_IN_DAY when the boundary
    // falls in the next calendar day (e.g., cycle starts at 23:58 with avg=3 → boundary=1440).
    // After midnight curr_msm resets to 0, so compare against the wrapped boundary instead.
    // Guard with curr_msm < MIN_IN_HALF_DAY to avoid false-positive triggers in the pre-midnight window.
    uint8_t boundary_crossed;
    if (measStat.next_boundary_msm >= MIN_IN_DAY)
    {
      int32_t wrapped_boundary = measStat.next_boundary_msm - MIN_IN_DAY;
      boundary_crossed = (curr_msm >= wrapped_boundary) && (curr_msm < MIN_IN_HALF_DAY);
      log_i("MIDNIGHT WRAP: wrapped_boundary=%d, curr_msm=%d, boundary_crossed=%s",
            wrapped_boundary, curr_msm, boundary_crossed ? "YES" : "NO");
    }
    else
    {
      boundary_crossed = (curr_msm >= measStat.next_boundary_msm);
    }

    // Missed-boundary fallback: transmit anyway if a full interval has elapsed
    boundary_crossed = boundary_crossed || enough_time_passed;

    log_i("BOUNDARY DETAILS: boundary_crossed=%s, enough_time_passed=%s, not_yet_transmitted=%s",
          boundary_crossed ? "YES" : "NO",
          enough_time_passed ? "YES" : "NO",
          boundary_not_transmitted ? "YES" : "NO");

    if (boundary_crossed && boundary_not_transmitted && (measStat.measurement_count > 0))
    {
      log_i("Clock boundary reached (curr_msm=%d, next_boundary_msm=%d, wrapped=%s): sending average of %d measurements",
            curr_msm, measStat.next_boundary_msm, (measStat.next_boundary_msm >= MIN_IN_DAY ? "yes" : "no"), measStat.measurement_count);
      mainStateMachine.next_state = SYS_STATE_SEND_DATA;
    }
    else if (boundary_crossed && !boundary_not_transmitted)
    {
      log_i("Data already transmitted in this window (%02d:%02d), waiting for next boundary", timeinfo.tm_hour, measStat.curr_minutes);
      mainStateMachine.next_state = SYS_STATE_WAIT_FOR_TIMEOUT;
    }
    else
    {
      log_i("Collecting measurements: %d so far, next TX at %02d:%02d...",
            measStat.measurement_count,
            measStat.next_boundary_msm / MIN_IN_HOUR,
            measStat.next_boundary_msm % MIN_IN_HOUR);
      vMsp_updateDataAndSendEvent(DISP_EVENT_SHOW_MEAS_DATA, &sensorData_single, &devinfo, &measStat, &sysData, &sysStat);
      mainStateMachine.next_state = SYS_STATE_WAIT_FOR_TIMEOUT;
    }

    mainStateMachine.prev_state = SYS_STATE_EVAL_SENSOR_STATUS;
    break;
  }
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

    if (sensorData_accumulate.status.BME680Sensor || sensorData_accumulate.status.PMS5003Sensor || sensorData_accumulate.status.MICS6814Sensor || sensorData_accumulate.status.MICS4514Sensor || sensorData_accumulate.status.O3Sensor || sensorData_accumulate.status.MSP1Sensor)
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
        log_i("sensorData_single updated: CO=%.2f, NO2=%.2f, NH3=%.2f ug/m3",
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

    // Restore sensor status flags after averaging (ALWAYS runs, outside the if-block above).
    // performAverages() clears a sensor's status when all readings failed (runs == 0).
    // sensorData_single.status is the authoritative hardware-detection state (set at boot,
    // never cleared by the measurement loop). Restoring here, unconditionally, breaks the
    // feedback loop where a previously-cleared accumulate.status causes the if-block to be
    // skipped entirely, leaving the status permanently false and sensors unreadable.
    sensorData_accumulate.status = sensorData_single.status;

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

    case GAS_SENSOR_MSP1:
    {
      // Finalise this window's accumulated NAU7802 samples: resets the raw
      // ADC accumulator for the next window and refreshes no2.rno2_ohm for
      // anything else that reads it (e.g. debug logging). Not the source of
      // the sent value below — see next comment.
      tMsp1_computeSensorAverage();
      tMsp1_computeRno2();

      // Send the average of this window's per-measurement Rno2/R0 readings
      // (summed via += in SYS_STATE_READ_SENSORS, same convention as
      // MICS6814/4514's pollutionData.nitrogenDioxide) rather than a single
      // fresh reading — unitless ratio instead of a NO2 concentration since
      // MSP1 has no datasheet-backed ppm conversion yet, and unlike
      // MICS6814/4514 this value is never humidity/temperature/pressure-
      // compensated.
      // The division by the successful reads already happened in
      // performAverages() above, exactly like every other sensor.
      sendData.MICS_CO = 0.0;
      sendData.MICS_NO2 = sensorData_accumulate.pollutionData.nitrogenDioxide;
      sendData.MICS_NH3 = 0.0;
      break;
    }

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

    // Always log to SD card FIRST, regardless of network status
    log_i("Writing data to SD card (mandatory logging)... SD status: %s", sysStat.sdCard ? "OK" : "FAIL");
    if (sysStat.sdCard)
    {
      vHalSdcard_logToSD(&sendData, &sysData, &sysStat, &sensorData_accumulate, &devinfo);
      log_i("Data logged to SD card successfully with date-based folder structure");
    }
    else
    {
      log_w("SD card not available for logging - data will be lost!");
    }

    // Enqueue data for transmission by network task
    if (enqueueSendData(sendData, pdMS_TO_TICKS(SEND_QUEUE_TIMEOUT_MS)))
    {
      log_i("Data enqueued successfully for network transmission");
      measStat.data_transmitted = true;                                  // Mark data as transmitted for this cycle
      measStat.last_transmission_epoch = mktime(&sendData.sendTimeInfo); // Record epoch time to prevent duplicates
      log_i("Recorded transmission epoch %lld (%02d:%02d) to prevent duplicates",
            (long long)measStat.last_transmission_epoch, sendData.sendTimeInfo.tm_hour, sendData.sendTimeInfo.tm_min);
    }
    else
    {
      log_e("Failed to enqueue data for transmission - queue might be full");
    }

    // Display always shows the latest instant reading (sensorData_single).
    // The averaged data was sent to the server; the display is not updated with averages.
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
    // Note: last_transmission_epoch is NOT reset here - it stays to prevent duplicates in the same boundary window

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

    // sensorData_single is NOT reset here.
    // It holds the last valid single reading and continues to be shown on the display
    // until the next READ_SENSORS cycle overwrites it with fresh values.
    // Resetting it here caused the display to show 0.0 / "--" between SEND_DATA and the
    // first measurement of the next cycle.

    log_i("Transmission complete. Next cycle will collect %d measurements (1 per minute).", measStat.avg_measurements);

    mainStateMachine.prev_state = SYS_STATE_SEND_DATA;
    mainStateMachine.next_state = SYS_STATE_UPDATE_CONFIG_FROM_SERVER; // Check for server config from PREVIOUS transmission
    break;
  }
  case SYS_STATE_ERROR:
  {
    vMsp_updateDataAndSendEvent(DISP_EVENT_SYSTEM_ERROR, &sensorData_single, &devinfo, &measStat, &sysData, &sysStat);
    log_e("System in error state! Manual reset required.");
    while (true)
    {
      esp_task_wdt_reset(); // keep WDT fed — this state requires manual intervention
      vTaskDelay(pdMS_TO_TICKS(10000));
    }
    break;
  }
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
  p_tData->status.MSP1Sensor = DISABLED;

  // -- no per-measurement read faults yet
  p_tData->fault.BME680Sensor = DISABLED;
  p_tData->fault.PMS5003Sensor = DISABLED;
  p_tData->fault.MICS6814Sensor = DISABLED;
  p_tData->fault.MICS4514Sensor = DISABLED;
  p_tData->fault.O3Sensor = DISABLED;
  p_tData->fault.MSP1Sensor = DISABLED;

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
  measStat.avg_measurements = AVG_MEASUREMENTS_MIN;
  measStat.isPmsAwake = false;
  measStat.isSensorDataAvailable = false;
  measStat.last_transmission_epoch = 0; // 0 = never transmitted
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
      devInfo->ssid = DEFAULT_WIFI_SSID;
      log_i("Applied default WiFi SSID");
    }
    if (devInfo->passw.length() == 0)
    {
      devInfo->passw = DEFAULT_WIFI_PASSWORD;
      log_i("Applied default WiFi password");
    }
    if (devInfo->apn.length() == 0)
    {
      devInfo->apn = DEFAULT_MODEM_APN;
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
      sysData->server = FALLBACK_API_SERVER;
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
      if (measStat->avg_measurements <= 0)
      {
        log_w("Invalid avg_measurements (%d): value must be > 0. Valid values: 1, 2, 3, 4, 5, 6, 10, 12, 15, 20, 30, 60", measStat->avg_measurements);
      }
      else
      {
        log_w("Invalid avg_measurements (%d): must be submultiple of 60. Valid values: 1, 2, 3, 4, 5, 6, 10, 12, 15, 20, 30, 60", measStat->avg_measurements);
      }
      measStat->avg_measurements = AVG_MEASUREMENTS_DEFAULT;
      log_i("Applied default avg_measurements: %d", measStat->avg_measurements);
    }

    // NTP configuration
    if (sysData->ntp_server.length() == 0)
    {
      sysData->ntp_server = NTP_SERVER_DEFAULT;
      log_i("Applied default NTP server: %s", sysData->ntp_server.c_str());
    }
#ifdef FORCE_TIMEZONE_GMT0
    sysData->timezone = TZ_DEFAULT;
#else
    if (sysData->timezone.length() == 0)
    {
      sysData->timezone = TZ_DEFAULT;
      log_i("Applied default timezone: %s", sysData->timezone.c_str());
    }
#endif
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
      if (measStat->avg_measurements <= 0)
      {
        log_w("Invalid avg_measurements (%d): value must be > 0. Valid values: 1, 2, 3, 4, 5, 6, 10, 12, 15, 20, 30, 60. Setting to 5", measStat->avg_measurements);
      }
      else
      {
        log_w("Invalid avg_measurements (%d): must be submultiple of 60. Valid values: 1, 2, 3, 4, 5, 6, 10, 12, 15, 20, 30, 60. Setting to 5", measStat->avg_measurements);
      }
      measStat->avg_measurements = AVG_MEASUREMENTS_DEFAULT;
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
  sysData->ver = DEFAULT_FW_VERSION;
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
  pinMode(SPARE_OUT_PIN_1, OUTPUT);
  pinMode(SPARE_OUT_PIN_2, OUTPUT);
  pinMode(SPARE_OUT_PIN_3, OUTPUT);
  pinMode(O3_ADC_PIN, INPUT_PULLDOWN);
  analogSetAttenuation(ADC_11db);
  pinMode(MODEM_RST, OUTPUT);
  digitalWrite(MODEM_RST, HIGH);
}

/**
 * @brief Scan I2C bus for connected devices and log their addresses
 */
void vMsp_scanI2CDevices(void)
{
  log_i("=== I2C Device Scanner ===");
  log_i("Scanning I2C bus for devices...");

  int deviceCount = 0;

  for (uint8_t address = I2C_SCAN_ADDR_MIN; address < I2C_SCAN_ADDR_MAX; address++)
  {
    Wire.beginTransmission(address);
    uint8_t error = Wire.endTransmission();

    if (error == I2C_RESULT_SUCCESS)
    {
      log_i("I2C device found at address 0x%02X (%d decimal)", address, address);
      deviceCount++;

      // Add known device identification
      switch (address)
      {
      case BME68X_I2C_ADDR_HIGH:
        log_i("  -> Likely BME680 (address 0x%02X)", BME68X_I2C_ADDR_HIGH);
        break;
      case BME68X_I2C_ADDR_LOW:
        log_i("  -> Likely BME680 alternative address (0x%02X)", BME68X_I2C_ADDR_LOW);
        break;
      case MICS6814_I2C_ADDR:
        log_i("  -> Likely MICS6814 (address 0x%02X)", MICS6814_I2C_ADDR);
        break;
      case MICS4514_I2C_ADDR:
        log_i("  -> Likely MICS4514/DFRobot SEN0377 (address 0x%02X)", MICS4514_I2C_ADDR);
        break;
      case OLED_I2C_ADDR_PRIMARY:
      case OLED_I2C_ADDR_SECONDARY:
        log_i("  -> Likely OLED Display (address 0x%02X)", address);
        break;
      case ADS1X15_I2C_ADDR_GND:
      case ADS1X15_I2C_ADDR_VDD:
      case ADS1X15_I2C_ADDR_SDA:
      case ADS1X15_I2C_ADDR_SCL:
        log_i("  -> Likely ADS1115/ADS1015 ADC (address 0x%02X)", address);
        break;
      default:
        log_i("  -> Unknown device type");
        break;
      }
    }
    else if (error == I2C_RESULT_OTHER_ERROR)
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