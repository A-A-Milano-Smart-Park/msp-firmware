/************************************************************************************************
 * @file    sdCard.cpp
 * @author  AB-Engineering - https://ab-engineering.it
 * @brief   Management of the SD Card functionalities for the Milano Smart Park project
 * @version 0.1
 * @date    2025-07-25
 *
 * @copyright Copyright (c) 2025
 *
 ************************************************************************************************/

// -- includes --
#include <U8g2lib.h>
#include <WiFi.h>
#include <SD.h>
#include <ArduinoJson.h>

#include "sdcard.h"
#include "shared_values.h"
#include "generic_functions.h"
#include "display_task.h"
#include "display.h"
#include "network.h"
#include "mspOs.h"
#include "config.h"
#include "sensors.h"

#define FOLDER_NAME_LEN 16
#define TIMEFORMAT_LEN 30

// File system constants
#define LOG_FILE_EXTENSION ".csv"
#define PATH_SEPARATOR "/"

// Date/Time format constants
#define YEAR_FORMAT "%04d"
#define MONTH_FORMAT "%02d"
#define DAY_FORMAT "%02d"
#define DATE_FORMAT "%d/%m/%Y"
#define TIME_FORMAT "%T"
#define ISO_DATETIME_FORMAT "%Y-%m-%dT%T.000Z"

// Numeric constants
#define BASE_YEAR_OFFSET 1900
#define MONTH_OFFSET 1
#define FIRST_DATA_COLUMN_SEPARATOR ";"

// CSV Header
#define CSV_HEADER "recordedAt;date;time;year;month;temp;hum;PM1;PM2_5;PM10;pres;radiation;nox;co;nh3;o3;voc;msp"

// Log file size and rotation constants  
#define LOG_MAX_SIZE 1000000
#define RETRY_ATTEMPTS 3

// SD Card initialization constants
#define SD_INIT_TIMEOUT_RETRIES 4
#define SD_INIT_DELAY_MS 1000
#define SD_DETECTION_DELAY_MS 300
#define BYTES_TO_MB_DIVISOR (1024 * 1024)

// Default configuration constants
#define DEFAULT_NTP_SERVER "pool.ntp.org"
#define DEFAULT_TIMEZONE "GMT0"
#define DEFAULT_WIFI_POWER "17dBm"
#define UNINITIALIZED_MARKER 255

// Legacy CSV header (for compatibility)
#define LEGACY_CSV_HEADER "sent_ok?;recordedAt;date;time;temp;hum;PM1;PM2_5;PM10;pres;radiation;nox;co;nh3;o3;voc;msp"

//-------------------------- functions --------------------

static uint8_t parseConfig(File fl, deviceNetworkInfo_t *p_tDev, sensorData_t *p_tData, deviceMeasurement_t *pDev, systemStatus_t *sysStat, systemData_t *p_tSysData);
uint8_t initializeSD(systemStatus_t *p_tSys, deviceNetworkInfo_t *p_tDev);
uint8_t checkConfig(const char *configpath, deviceNetworkInfo_t *p_tDev, sensorData_t *p_tData, deviceMeasurement_t *pDev, systemStatus_t *p_tSys, systemData_t *p_tSysData);
uint8_t addToLog(const char *path, const char *oldpath, const char *errpath, String *message);

/**********************************************************************
 * @brief Send network data to display task queue using local structure
 *
 * @param p_tDev
 * @param p_tSys
 * @param event
 **********************************************************************/
static void vMsp_sendNetworkDataToDisplay(deviceNetworkInfo_t *p_tDev, systemStatus_t *p_tSys, displayEvents_t event)
{
  displayData_t localDisplayData = {};
  localDisplayData.currentEvent = event;

  vMspOs_takeDataAccessMutex();
  localDisplayData.devInfo = *p_tDev;
  localDisplayData.sysStat = *p_tSys;
  vMspOs_giveDataAccessMutex();

  tTaskDisplay_sendEvent(&localDisplayData);
}

/**********************************************************************
 * @brief initialize SD card
 *
 * @param p_tSys
 * @param p_tDev
 * @return uint8_t
 **********************************************************************/
uint8_t initializeSD(systemStatus_t *p_tSys, deviceNetworkInfo_t *p_tDev)
{
  // checks for SD Card presence and type

  short timeout = 0;
  while (!SD.begin())
  {
    if (timeout > SD_INIT_TIMEOUT_RETRIES)
    { // errors after 10 seconds
      log_e("No SD Card detected! No internet connection possible!\n");
      vMsp_sendNetworkDataToDisplay(p_tDev, p_tSys, DISP_EVENT_SD_CARD_NOT_PRESENT);

      return false;
    }
    delay(SD_INIT_DELAY_MS); // giving it some time to detect the card properly
    timeout++;
  }
  uint8_t cardType = SD.cardType();
  if (cardType == CARD_MMC)
  {
    log_v("SD Card type: MMC");
  }
  else if (cardType == CARD_SD)
  {
    log_v("SD Card type: SD");
  }
  else if (cardType == CARD_SDHC)
  {
    log_v("SD Card type: SDHC");
  }
  else
  {
    log_e("Unidentified Card type, format the SD Card!  No internet connection possible!\n");
    vMsp_sendNetworkDataToDisplay(p_tDev, p_tSys, DISP_EVENT_SD_CARD_FORMAT);
    return false;
  }
  delay(SD_DETECTION_DELAY_MS);
  log_v("SD Card size: %lluMB\n", SD.cardSize() / BYTES_TO_MB_DIVISOR);
  return true;
}

/*********************************************************
 * @brief parse configuratuion
 *
 * @param fl
 * @param p_tDev
 * @param p_tData
 * @param pDev
 * @param sysStat
 * @param p_tSysData
 * @return uint8_t
 *********************************************************/
static uint8_t parseConfig(File fl, deviceNetworkInfo_t *p_tDev, sensorData_t *p_tData, deviceMeasurement_t *pDev, systemStatus_t *sysStat, systemData_t *p_tSysData)
{ // parses the JSON configuration file on the SD Card

  uint8_t outcome = true;

  // Read the entire file into a string
  String jsonString = fl.readString();
  fl.close();

  // Parse JSON
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, jsonString);

  if (error)
  {
    log_e("Failed to parse config JSON: %s", error.c_str());
    return false;
  }

  JsonObject config = doc[JSON_CONFIG_SECTION];
  if (!config)
  {
    log_e("Missing 'config' section in JSON");
    return false;
  }

  // Parse SSID
  if (!config[JSON_KEY_SSID].isNull())
  {
    p_tDev->ssid = config[JSON_KEY_SSID].as<String>();
    if (p_tDev->ssid.length() > 0)
    {
      log_i("ssid = *%s*", p_tDev->ssid.c_str());
    }
    else
    {
      log_i("SSID value is empty");
    }
  }
  else
  {
    log_e("Missing SSID in config!");
    outcome = false;
  }

  // Parse Password
  if (!config[JSON_KEY_PASSWORD].isNull())
  {
    p_tDev->passw = config[JSON_KEY_PASSWORD].as<String>();
    log_i("passw = *%s*", p_tDev->passw.c_str());
  }
  else
  {
    log_e("Missing PASSWORD in config!");
  }

  // Parse Device ID
  if (!config[JSON_KEY_DEVICE_ID].isNull())
  {
    p_tDev->deviceid = config[JSON_KEY_DEVICE_ID].as<String>();
    if (p_tDev->deviceid.length() == 0)
    {
      log_e("DEVICEID value is empty!");
      outcome = false;
    }
    else
    {
      log_i("deviceid = *%s*", p_tDev->deviceid.c_str());
    }
  }
  else
  {
    log_e("Missing DEVICEID in config!");
    outcome = false;
  }

  // Parse WiFi Power
  if (!config[JSON_KEY_WIFI_POWER].isNull())
  {
    String wifiPowerStr = config[JSON_KEY_WIFI_POWER].as<String>();
    if (wifiPowerStr == "-1dBm")
    {
      log_i("Wifi power is set to POWER_MINUS_1_dBm");
      p_tDev->wifipow = WIFI_POWER_MINUS_1dBm;
    }
    else if (wifiPowerStr == "2dBm")
    {
      log_i("Wifi power is set to POWER_2dBm");
      p_tDev->wifipow = WIFI_POWER_2dBm;
    }
    else if (wifiPowerStr == "5dBm")
    {
      log_i("Wifi power is set to POWER_5dBm");
      p_tDev->wifipow = WIFI_POWER_5dBm;
    }
    else if (wifiPowerStr == "7dBm")
    {
      log_i("Wifi power is set to POWER_7dBm");
      p_tDev->wifipow = WIFI_POWER_7dBm;
    }
    else if (wifiPowerStr == "8.5dBm")
    {
      log_i("Wifi power is set to POWER_8_5dBm");
      p_tDev->wifipow = WIFI_POWER_8_5dBm;
    }
    else if (wifiPowerStr == "11dBm")
    {
      log_i("Wifi power is set to POWER_11dBm");
      p_tDev->wifipow = WIFI_POWER_11dBm;
    }
    else if (wifiPowerStr == "13dBm")
    {
      log_i("Wifi power is set to POWER_13dBm");
      p_tDev->wifipow = WIFI_POWER_13dBm;
    }
    else if (wifiPowerStr == "15dBm")
    {
      log_i("Wifi power is set to POWER_15dBm");
      p_tDev->wifipow = WIFI_POWER_15dBm;
    }
    else if (wifiPowerStr == "17dBm")
    {
      log_i("Wifi power is set to POWER_17dBm");
      p_tDev->wifipow = WIFI_POWER_17dBm;
    }
    else if (wifiPowerStr == "18.5dBm")
    {
      log_i("Wifi power is set to POWER_18_5dBm");
      p_tDev->wifipow = WIFI_POWER_18_5dBm;
    }
    else if (wifiPowerStr == "19dBm")
    {
      log_i("Wifi power is set to POWER_19dBm");
      p_tDev->wifipow = WIFI_POWER_19dBm;
    }
    else if (wifiPowerStr == "19.5dBm")
    {
      log_i("Wifi power is set to POWER_19_5dBm");
      p_tDev->wifipow = WIFI_POWER_19_5dBm;
    }
    else
    {
      log_i("Wifi power parameter not recognized. Falling back to 17dBm");
      p_tDev->wifipow = WIFI_POWER_17dBm;
    }
  }
  else
  {
    log_e("Missing WIFI_POWER in config. Falling back to default value (17dBm)");
    p_tDev->wifipow = WIFI_POWER_17dBm;
  }

  // Parse O3 Zero Value
  p_tData->ozoneData.o3ZeroOffset = config[JSON_KEY_O3_ZERO_VALUE] | -1;
  log_i("o3_zero_value = *%d*", p_tData->ozoneData.o3ZeroOffset);

  // Parse Average Measurements
  pDev->avg_measurements = config[JSON_KEY_AVERAGE_MEASUREMENTS] | 30;
  log_i("avgMeasure = *%d*", pDev->avg_measurements);

  // Parse Sea Level Altitude
  p_tData->gasData.seaLevelAltitude = config[JSON_KEY_SEA_LEVEL_ALTITUDE] | 122.0f;
  log_i("sealevelalt = *%.2f*", p_tData->gasData.seaLevelAltitude);

  // Parse Upload Server
  if (!config[JSON_KEY_UPLOAD_SERVER].isNull())
  {
    String serverStr = config[JSON_KEY_UPLOAD_SERVER].as<String>();
    if (serverStr.length() > 0)
    {
      p_tSysData->server = serverStr;
      p_tSysData->server_ok = true;
      sysStat->server_ok = true;
    }
    else
    {
#ifdef API_SERVER
      log_i("SERVER value is empty. Falling back to value defined at compile time");
#else
      log_e("SERVER value is empty!");
#endif
    }
  }
  else
  {
#ifdef API_SERVER
    log_i("Missing UPLOAD_SERVER in config. Falling back to value defined at compile time");
#else
    log_e("Missing UPLOAD_SERVER in config!");
#endif
  }
  log_i("server = *%s*", p_tSysData->server.c_str());

  // Parse MICS Calibration Values
  if (!config[JSON_KEY_MICS_CALIBRATION_VALUES].isNull())
  {
    JsonObject micsCalib = config[JSON_KEY_MICS_CALIBRATION_VALUES];
    p_tData->micsTuningData.sensingResInAir.redSensor = micsCalib[JSON_KEY_MICS_RED] | R0_RED_SENSOR;
    p_tData->micsTuningData.sensingResInAir.oxSensor = micsCalib[JSON_KEY_MICS_OX] | R0_OX_SENSOR;
    p_tData->micsTuningData.sensingResInAir.nh3Sensor = micsCalib[JSON_KEY_MICS_NH3] | R0_NH3_SENSOR;
  }
  else
  {
    log_e("Missing MICS_CALIBRATION_VALUES in config. Using defaults");
    p_tData->micsTuningData.sensingResInAir.redSensor = R0_RED_SENSOR;
    p_tData->micsTuningData.sensingResInAir.oxSensor = R0_OX_SENSOR;
    p_tData->micsTuningData.sensingResInAir.nh3Sensor = R0_NH3_SENSOR;
  }
  log_i("MICS R0[] = *%d*, *%d*, *%d*", p_tData->micsTuningData.sensingResInAir.redSensor, p_tData->micsTuningData.sensingResInAir.oxSensor, p_tData->micsTuningData.sensingResInAir.nh3Sensor);

  // Parse MICS Measurement Offsets
  if (!config[JSON_KEY_MICS_MEASUREMENTS_OFFSETS].isNull())
  {
    JsonObject micsOffset = config[JSON_KEY_MICS_MEASUREMENTS_OFFSETS];
    p_tData->micsTuningData.sensingResInAirOffset.redSensor = (int16_t)(micsOffset[JSON_KEY_MICS_RED] | DEFAULT_SENSOR_OFFSET);
    p_tData->micsTuningData.sensingResInAirOffset.oxSensor = (int16_t)(micsOffset[JSON_KEY_MICS_OX] | DEFAULT_SENSOR_OFFSET);
    p_tData->micsTuningData.sensingResInAirOffset.nh3Sensor = (int16_t)(micsOffset[JSON_KEY_MICS_NH3] | DEFAULT_SENSOR_OFFSET);
  }
  else
  {
    log_e("Missing MICS_MEASUREMENTS_OFFSETS in config. Using defaults");
    p_tData->micsTuningData.sensingResInAirOffset.redSensor = DEFAULT_SENSOR_OFFSET;
    p_tData->micsTuningData.sensingResInAirOffset.oxSensor = DEFAULT_SENSOR_OFFSET;
    p_tData->micsTuningData.sensingResInAirOffset.nh3Sensor = DEFAULT_SENSOR_OFFSET;
  }
  log_i("MICSoffset[] = *%d*, *%d*, *%d*", p_tData->micsTuningData.sensingResInAirOffset.redSensor, p_tData->micsTuningData.sensingResInAirOffset.oxSensor, p_tData->micsTuningData.sensingResInAirOffset.nh3Sensor);

  // Parse Compensation Factors
  if (!config[JSON_KEY_COMPENSATION_FACTORS].isNull())
  {
    JsonObject compFactors = config[JSON_KEY_COMPENSATION_FACTORS];
    p_tData->compParams.currentHumidity = compFactors[JSON_KEY_COMP_H] | HUMIDITY_COMP_PARAM;
    p_tData->compParams.currentTemperature = compFactors[JSON_KEY_COMP_T] | TEMP_COMP_PARAM;
    p_tData->compParams.currentPressure = compFactors[JSON_KEY_COMP_P] | PRESS_COMP_PARAM;
  }
  else
  {
    log_e("Missing COMPENSATION_FACTORS in config. Using defaults");
    p_tData->compParams.currentHumidity = HUMIDITY_COMP_PARAM;
    p_tData->compParams.currentTemperature = TEMP_COMP_PARAM;
    p_tData->compParams.currentPressure = PRESS_COMP_PARAM;
  }
  log_i("compensation[] = *%.3f*, *%.3f*, *%.6f*", p_tData->compParams.currentHumidity, p_tData->compParams.currentTemperature, p_tData->compParams.currentPressure);

  // Parse Use Modem
  #if (FORCE_GSM_NETWORK_CONNECTION == 1)
  sysStat->use_modem = true;
  #else
  sysStat->use_modem = config[JSON_KEY_USE_MODEM] | false;
  #endif
  log_i("useModem = *%s*", (sysStat->use_modem) ? STR_TRUE : STR_FALSE);

  // Parse Modem APN
  if (!config[JSON_KEY_MODEM_APN].isNull())
  {
    String apnStr = config[JSON_KEY_MODEM_APN].as<String>();
    if (apnStr.length() > 0)
    {
      p_tDev->apn = apnStr;
      log_i("modem_apn = *%s*", p_tDev->apn.c_str());
    }
    else
    {
      log_i("modem_apn is empty");
    }
  }
  else
  {
    log_e("Missing MODEM_APN in config!");
  }

  // Parse NTP Server
  if (!config[JSON_KEY_NTP_SERVER].isNull())
  {
    String ntpStr = config[JSON_KEY_NTP_SERVER].as<String>();
    if (ntpStr.length() > 0)
    {
      p_tSysData->ntp_server = ntpStr;
      log_i("ntp_server = *%s*", p_tSysData->ntp_server.c_str());
    }
    else
    {
      log_e("NTP_SERVER value is empty. Falling back to default value (pool.ntp.org)");
      p_tSysData->ntp_server = DEFAULT_NTP_SERVER;
    }
  }
  else
  {
    log_e("Missing NTP_SERVER in config. Falling back to default value (pool.ntp.org)");
    p_tSysData->ntp_server = DEFAULT_NTP_SERVER;
  }

#ifndef FORCE_TIMEZONE_GMT0
  // Parse Timezone
  if (!config[JSON_KEY_TIMEZONE].isNull())
  {
    String timezoneStr = config[JSON_KEY_TIMEZONE].as<String>();
    if (timezoneStr.length() > 0)
    {
      p_tSysData->timezone = timezoneStr;
      log_i("timezone = *%s*", p_tSysData->timezone.c_str());
    }
    else
    {
      log_i("timezone = *%s*", p_tSysData->timezone.c_str());
    }
  }
  else
  {
    log_e("Missing TIMEZONE in config!");
  }
#else
  // Force timezone to GMT0 regardless of SD card configuration
  p_tSysData->timezone = TZ_DEFAULT;
  log_i("FORCE_TIMEZONE_GMT0 enabled - timezone forced to: %s", p_tSysData->timezone.c_str());
#endif

  // Parse Firmware Auto Upgrade
  sysStat->fwAutoUpgrade = config[JSON_KEY_FW_AUTO_UPGRADE] | false;
  log_i("fwAutoUpgrade = *%s*", (sysStat->fwAutoUpgrade) ? STR_TRUE : STR_FALSE);

  // Parse Gas Sensor Type
  sysStat->gasSensorType = config[JSON_KEY_GAS_SENSOR_TYPE] | GAS_SENSOR_MICS6814; // Default to MICS6814
  log_i("gasSensorType = %d (%s)", sysStat->gasSensorType,
        (sysStat->gasSensorType == GAS_SENSOR_MICS6814) ? "MICS6814" :
        (sysStat->gasSensorType == GAS_SENSOR_MICS4514) ? "MICS4514" : "Unknown");

  return outcome;
}

/***************************************************************
 * @brief check configuration
 *
 * @param configpath
 * @param p_tDev
 * @param p_tData
 * @param pDev
 * @param p_tSys
 * @param p_tSysData
 * @return uint8_t
 ***************************************************************/
uint8_t checkConfig(const char *configpath, deviceNetworkInfo_t *p_tDev, sensorData_t *p_tData, deviceMeasurement_t *pDev, systemStatus_t *p_tSys, systemData_t *p_tSysData)
{ // verifies the existance of the configuration file, creates the file if not found

  File cfgfile;

  if (SD.exists(configpath))
  {
    cfgfile = SD.open(configpath, FILE_READ); // open read only
    log_i("Found config file. Parsing...\n");

    if (parseConfig(cfgfile, p_tDev, p_tData, pDev, p_tSys, p_tSysData))
    {
      return true;
    }
    else
    {
      log_e("Error parsing config file! No network configuration!\n");
      vMsp_sendNetworkDataToDisplay(p_tDev, p_tSys, DISP_EVENT_SD_CARD_CONFIG_ERROR);

      return false;
    }
  }
  else
  {
    log_e("Couldn't find config file! Creating a new one with template...");
    vMsp_sendNetworkDataToDisplay(p_tDev, p_tSys, DISP_EVENT_SD_CARD_CONFIG_CREATE);
    cfgfile = SD.open(configpath, FILE_WRITE); // open r/w

    if (cfgfile)
    {
      // Create JSON template for default config file
      JsonDocument doc;

      // Create config section
      JsonObject config = doc[JSON_CONFIG_SECTION].to<JsonObject>();
      config[JSON_KEY_SSID] = "";
      config[JSON_KEY_PASSWORD] = "";
      config[JSON_KEY_DEVICE_ID] = "";
      config[JSON_KEY_WIFI_POWER] = DEFAULT_WIFI_POWER;
      config[JSON_KEY_O3_ZERO_VALUE] = p_tData->ozoneData.o3ZeroOffset;
      config[JSON_KEY_AVERAGE_MEASUREMENTS] = pDev->avg_measurements;
      config[JSON_KEY_SEA_LEVEL_ALTITUDE] = p_tData->gasData.seaLevelAltitude;
      config[JSON_KEY_UPLOAD_SERVER] = "";

      // MICS calibration values
      JsonObject micsCalib = config[JSON_KEY_MICS_CALIBRATION_VALUES].to<JsonObject>();
      micsCalib[JSON_KEY_MICS_RED] = p_tData->pollutionData.carbonMonoxide;
      micsCalib[JSON_KEY_MICS_OX] = p_tData->pollutionData.nitrogenDioxide;
      micsCalib[JSON_KEY_MICS_NH3] = p_tData->pollutionData.ammonia;

      // MICS measurement offsets
      JsonObject micsOffset = config[JSON_KEY_MICS_MEASUREMENTS_OFFSETS].to<JsonObject>();
      micsOffset[JSON_KEY_MICS_RED] = p_tData->micsTuningData.sensingResInAirOffset.redSensor;
      micsOffset[JSON_KEY_MICS_OX] = p_tData->micsTuningData.sensingResInAirOffset.oxSensor;
      micsOffset[JSON_KEY_MICS_NH3] = p_tData->micsTuningData.sensingResInAirOffset.nh3Sensor;

      // Compensation factors
      JsonObject compFactors = config[JSON_KEY_COMPENSATION_FACTORS].to<JsonObject>();
      compFactors[JSON_KEY_COMP_H] = p_tData->compParams.currentHumidity;
      compFactors[JSON_KEY_COMP_T] = p_tData->compParams.currentTemperature;
      compFactors[JSON_KEY_COMP_P] = p_tData->compParams.currentPressure;

      config[JSON_KEY_USE_MODEM] = p_tSys->use_modem;
      config[JSON_KEY_MODEM_APN] = "";
      config[JSON_KEY_NTP_SERVER] = DEFAULT_NTP_SERVER;
      config[JSON_KEY_TIMEZONE] = DEFAULT_TIMEZONE;
      config[JSON_KEY_FW_AUTO_UPGRADE] = false;

      // Create help section
      JsonObject help = doc[JSON_HELP_SECTION].to<JsonObject>();
      help[JSON_KEY_WIFI_POWER] = "Accepted values: -1, 2, 5, 7, 8.5, 11, 13, 15, 17, 18.5, 19, 19.5 dBm";
      help[JSON_KEY_AVERAGE_MEASUREMENTS] = "Accepted values: 1, 2, 3, 4, 5, 6, 10, 12, 15, 20, 30, 60";
      help[JSON_KEY_SEA_LEVEL_ALTITUDE] = "Value in meters, must be changed according to device location. 122.0 meters is the average altitude in Milan, Italy";
      help[JSON_KEY_TIMEZONE] = "Standard tz timezone definition. More details at https://www.gnu.org/software/libc/manual/html_node/TZ-Variable.html";

      // Serialize and write JSON to file
      serializeJsonPretty(doc, cfgfile);
      log_i("New config file with template created!\n");
      cfgfile.close();
      vMsp_sendNetworkDataToDisplay(p_tDev, p_tSys, DISP_EVENT_SD_CARD_CONFIG_INS_DATA);
    }
    else
    {
      log_e("Error writing to SD Card!\n");
      vMsp_sendNetworkDataToDisplay(p_tDev, p_tSys, DISP_EVENT_SD_CARD_WRITE_DATA);
    }

    return false;
  }
}

/*******************************************************************************
 * @brief Write current system configuration to SD card config file
 *
 * This function writes the current system structures to config_v4.json.
 * It creates the JSON structure from system memory and writes to SD card.
 *
 * @param p_tDev device network info structure
 * @param p_tData sensor data structure
 * @param pDev device measurement structure
 * @param p_tSys system status structure
 * @param p_tSysData system data structure
 * @return bool true if write successful, false otherwise
 ******************************************************************************/
bool bHalSdcard_writeConfig(deviceNetworkInfo_t *p_tDev, sensorData_t *p_tData, deviceMeasurement_t *pDev, systemStatus_t *p_tSys, systemData_t *p_tSysData)
{
  log_i("Writing system configuration to SD card...");

  // Check if SD card is available
  if (p_tSys->sdCard != STATUS_OK)
  {
    log_e("SD card not available!");
    return false;
  }

  // Read existing config to preserve help section
  File config_file = SD.open(CONFIG_PATH, FILE_READ);
  JsonDocument doc;
  JsonObject help;

  if (config_file)
  {
    // Try to read existing help section
    String existing_json = config_file.readString();
    config_file.close();

    JsonDocument existing_doc;
    DeserializationError error = deserializeJson(existing_doc, existing_json);
    if (!error && existing_doc[JSON_HELP_SECTION].is<JsonObject>())
    {
      // Preserve existing help section
      help = doc[JSON_HELP_SECTION].to<JsonObject>();
      JsonObject existing_help = existing_doc[JSON_HELP_SECTION];
      for (JsonPair kv : existing_help)
      {
        help[kv.key()] = kv.value();
      }
    }
  }

  // Create config section from system structures
  JsonObject config = doc[JSON_CONFIG_SECTION].to<JsonObject>();

  // Network configuration
  config[JSON_KEY_SSID] = p_tDev->ssid;
  config[JSON_KEY_PASSWORD] = p_tDev->passw;
  config[JSON_KEY_DEVICE_ID] = p_tDev->deviceid;

  // WiFi power - convert from wifi_power_t enum back to string
  String wifiPowerStr;
  switch (p_tDev->wifipow)
  {
    case WIFI_POWER_MINUS_1dBm: wifiPowerStr = "-1dBm"; break;
    case WIFI_POWER_2dBm: wifiPowerStr = "2dBm"; break;
    case WIFI_POWER_5dBm: wifiPowerStr = "5dBm"; break;
    case WIFI_POWER_7dBm: wifiPowerStr = "7dBm"; break;
    case WIFI_POWER_8_5dBm: wifiPowerStr = "8.5dBm"; break;
    case WIFI_POWER_11dBm: wifiPowerStr = "11dBm"; break;
    case WIFI_POWER_13dBm: wifiPowerStr = "13dBm"; break;
    case WIFI_POWER_15dBm: wifiPowerStr = "15dBm"; break;
    case WIFI_POWER_17dBm: wifiPowerStr = "17dBm"; break;
    case WIFI_POWER_18_5dBm: wifiPowerStr = "18.5dBm"; break;
    case WIFI_POWER_19dBm: wifiPowerStr = "19dBm"; break;
    case WIFI_POWER_19_5dBm: wifiPowerStr = "19.5dBm"; break;
    default: wifiPowerStr = "17dBm"; break;
  }
  config[JSON_KEY_WIFI_POWER] = wifiPowerStr;

  // Sensor configuration
  config[JSON_KEY_O3_ZERO_VALUE] = p_tData->ozoneData.o3ZeroOffset;
  config[JSON_KEY_AVERAGE_MEASUREMENTS] = pDev->avg_measurements;
  config[JSON_KEY_SEA_LEVEL_ALTITUDE] = p_tData->gasData.seaLevelAltitude;
  config[JSON_KEY_UPLOAD_SERVER] = p_tSysData->server;

  // MICS calibration values
  JsonObject micsCalib = config[JSON_KEY_MICS_CALIBRATION_VALUES].to<JsonObject>();
  micsCalib[JSON_KEY_MICS_RED] = p_tData->micsTuningData.sensingResInAir.redSensor;
  micsCalib[JSON_KEY_MICS_OX] = p_tData->micsTuningData.sensingResInAir.oxSensor;
  micsCalib[JSON_KEY_MICS_NH3] = p_tData->micsTuningData.sensingResInAir.nh3Sensor;

  // MICS measurement offsets
  JsonObject micsOffset = config[JSON_KEY_MICS_MEASUREMENTS_OFFSETS].to<JsonObject>();
  micsOffset[JSON_KEY_MICS_RED] = p_tData->micsTuningData.sensingResInAirOffset.redSensor;
  micsOffset[JSON_KEY_MICS_OX] = p_tData->micsTuningData.sensingResInAirOffset.oxSensor;
  micsOffset[JSON_KEY_MICS_NH3] = p_tData->micsTuningData.sensingResInAirOffset.nh3Sensor;

  // Compensation factors
  JsonObject compFactors = config[JSON_KEY_COMPENSATION_FACTORS].to<JsonObject>();
  compFactors[JSON_KEY_COMP_H] = p_tData->compParams.currentHumidity;
  compFactors[JSON_KEY_COMP_T] = p_tData->compParams.currentTemperature;
  compFactors[JSON_KEY_COMP_P] = p_tData->compParams.currentPressure;

  // Modem configuration
  config[JSON_KEY_USE_MODEM] = (p_tSys->use_modem != 0);
  config[JSON_KEY_MODEM_APN] = p_tDev->apn;

  // Time configuration
  config[JSON_KEY_NTP_SERVER] = p_tSysData->ntp_server;
  config[JSON_KEY_TIMEZONE] = p_tSysData->timezone;

  // System configuration
  config[JSON_KEY_FW_AUTO_UPGRADE] = (p_tSys->fwAutoUpgrade != 0);
  config[JSON_KEY_GAS_SENSOR_TYPE] = p_tSys->gasSensorType;

  // Create default help section if it doesn't exist
  if (!help)
  {
    help = doc[JSON_HELP_SECTION].to<JsonObject>();
    help[JSON_KEY_WIFI_POWER] = "Accepted values: -1, 2, 5, 7, 8.5, 11, 13, 15, 17, 18.5, 19, 19.5 dBm";
    help[JSON_KEY_AVERAGE_MEASUREMENTS] = "Accepted values: 1, 2, 3, 4, 5, 6, 10, 12, 15, 20, 30, 60";
    help[JSON_KEY_SEA_LEVEL_ALTITUDE] = "Value in meters, must be changed according to device location. 122.0 meters is the average altitude in Milan, Italy";
    help[JSON_KEY_TIMEZONE] = "Standard tz timezone definition. More details at https://www.gnu.org/software/libc/manual/html_node/TZ-Variable.html";
    help[JSON_KEY_GAS_SENSOR_TYPE] = "Gas sensor type: 0 = MICS6814, 1 = MICS4514 (DFRobot SEN0377)";
  }

  // Write config to SD card
  config_file = SD.open(CONFIG_PATH, FILE_WRITE);
  if (!config_file)
  {
    log_e("Failed to open config file for writing!");
    return false;
  }

  // Serialize and write JSON to file
  if (serializeJsonPretty(doc, config_file) == 0)
  {
    log_e("Failed to write JSON to config file!");
    config_file.close();
    return false;
  }

  config_file.close();
  log_i("Configuration file written successfully!");

  return true;
}

/*******************************************************************************
 * @brief Update system structures from server JSON response
 *
 * This function parses the server configuration JSON and updates the system
 * structures accordingly. Fields not present in the server response are left
 * unchanged in the system structures.
 *
 * @param server_json JSON string from server with configuration updates
 * @param p_tDev device network info structure
 * @param p_tData sensor data structure
 * @param pDev device measurement structure
 * @param p_tSys system status structure
 * @param p_tSysData system data structure
 * @return bool true if update successful, false otherwise
 ******************************************************************************/
bool bHalSdcard_updateFromServerConfig(const String &server_json, deviceNetworkInfo_t *p_tDev, sensorData_t *p_tData, deviceMeasurement_t *pDev, systemStatus_t *p_tSys, systemData_t *p_tSysData)
{
  log_i("Updating system structures from server configuration with validation...");

  // Parse server response
  JsonDocument server_doc;
  DeserializationError error = deserializeJson(server_doc, server_json);
  if (error)
  {
    log_e("Failed to parse server JSON: %s", error.c_str());
    return false;
  }

  bool config_updated = false;
  int fields_validated = 0;
  int fields_rejected = 0;

  // ===== SENSOR CONFIGURATION =====

  // o3_zero_value - can be 0 or negative
  if (!server_doc["o3_zero_value"].isNull())
  {
    int value = server_doc["o3_zero_value"].as<int>();
    p_tData->ozoneData.o3ZeroOffset = value;
    log_i("Updated o3_zero_value: %d", value);
    config_updated = true;
    fields_validated++;
  }

  // average_measurements - minimum value is 2
  if (!server_doc["average_measurements"].isNull())
  {
    int value = server_doc["average_measurements"].as<int>();
    if (value >= 2)
    {
      pDev->avg_measurements = value;
      log_i("Updated average_measurements: %d", value);
      config_updated = true;
      fields_validated++;
    }
    else
    {
      log_w("Rejected average_measurements: %d (minimum is 2) - keeping current value: %d", value, pDev->avg_measurements);
      fields_rejected++;
    }
  }

  // sea_level_altitude - can be 0
  if (!server_doc["sea_level_altitude"].isNull())
  {
    float value = server_doc["sea_level_altitude"].as<float>();
    p_tData->gasData.seaLevelAltitude = value;
    log_i("Updated sea_level_altitude: %.2f", value);
    config_updated = true;
    fields_validated++;
  }

  // ===== MICS CALIBRATION VALUES =====
  // Can be 0, stored as uint16_t

  if (!server_doc["mics_calibration_red"].isNull())
  {
    uint16_t value = server_doc["mics_calibration_red"].as<uint16_t>();
    p_tData->micsTuningData.sensingResInAir.redSensor = value;
    log_i("Updated mics_calibration_red: %d", value);
    config_updated = true;
    fields_validated++;
  }

  if (!server_doc["mics_calibration_ox"].isNull())
  {
    uint16_t value = server_doc["mics_calibration_ox"].as<uint16_t>();
    p_tData->micsTuningData.sensingResInAir.oxSensor = value;
    log_i("Updated mics_calibration_ox: %d", value);
    config_updated = true;
    fields_validated++;
  }

  if (!server_doc["mics_calibration_nh3"].isNull())
  {
    uint16_t value = server_doc["mics_calibration_nh3"].as<uint16_t>();
    p_tData->micsTuningData.sensingResInAir.nh3Sensor = value;
    log_i("Updated mics_calibration_nh3: %d", value);
    config_updated = true;
    fields_validated++;
  }

  // ===== MICS OFFSET VALUES =====
  // Can be 0, stored as int16_t (can be negative)

  if (!server_doc["mics_offset_red"].isNull())
  {
    int16_t value = server_doc["mics_offset_red"].as<int16_t>();
    p_tData->micsTuningData.sensingResInAirOffset.redSensor = value;
    log_i("Updated mics_offset_red: %d", value);
    config_updated = true;
    fields_validated++;
  }

  if (!server_doc["mics_offset_ox"].isNull())
  {
    int16_t value = server_doc["mics_offset_ox"].as<int16_t>();
    p_tData->micsTuningData.sensingResInAirOffset.oxSensor = value;
    log_i("Updated mics_offset_ox: %d", value);
    config_updated = true;
    fields_validated++;
  }

  if (!server_doc["mics_offset_nh3"].isNull())
  {
    int16_t value = server_doc["mics_offset_nh3"].as<int16_t>();
    p_tData->micsTuningData.sensingResInAirOffset.nh3Sensor = value;
    log_i("Updated mics_offset_nh3: %d", value);
    config_updated = true;
    fields_validated++;
  }

  // ===== COMPENSATION FACTORS =====
  // Can be 0

  if (!server_doc["comp_h"].isNull())
  {
    float value = server_doc["comp_h"].as<float>();
    p_tData->compParams.currentHumidity = value;
    log_i("Updated comp_h: %.2f", value);
    config_updated = true;
    fields_validated++;
  }

  if (!server_doc["comp_t"].isNull())
  {
    float value = server_doc["comp_t"].as<float>();
    p_tData->compParams.currentTemperature = value;
    log_i("Updated comp_t: %.2f", value);
    config_updated = true;
    fields_validated++;
  }

  if (!server_doc["comp_p"].isNull())
  {
    float value = server_doc["comp_p"].as<float>();
    p_tData->compParams.currentPressure = value;
    log_i("Updated comp_p: %.2f", value);
    config_updated = true;
    fields_validated++;
  }

  // ===== TIME CONFIGURATION =====
  // Strings cannot be empty

  if (!server_doc["ntp_server"].isNull())
  {
    String value = server_doc["ntp_server"].as<String>();
    if (value.length() > 0)
    {
      p_tSysData->ntp_server = value;
      log_i("Updated ntp_server: %s", value.c_str());
      config_updated = true;
      fields_validated++;
    }
    else
    {
      log_w("Rejected ntp_server: empty string - keeping current value: %s", p_tSysData->ntp_server.c_str());
      fields_rejected++;
    }
  }

#ifndef FORCE_TIMEZONE_GMT0
  if (!server_doc["timezone"].isNull())
  {
    String value = server_doc["timezone"].as<String>();
    if (value.length() > 0)
    {
      p_tSysData->timezone = value;
      log_i("Updated timezone: %s", value.c_str());
      config_updated = true;
      fields_validated++;
    }
    else
    {
      log_w("Rejected timezone: empty string - keeping current value: %s", p_tSysData->timezone.c_str());
      fields_rejected++;
    }
  }
#else
  // Force timezone to GMT0 regardless of server response
  p_tSysData->timezone = TZ_DEFAULT;
  log_i("FORCE_TIMEZONE_GMT0 enabled - timezone forced to: %s", p_tSysData->timezone.c_str());
#endif

  // ===== SYSTEM CONFIGURATION =====

  // fw_auto_upgrade - arrives as 0 or 1 (integer), not boolean
  if (!server_doc["fw_auto_upgrade"].isNull())
  {
    int value = server_doc["fw_auto_upgrade"].as<int>();
    if (value == 0 || value == 1)
    {
      p_tSys->fwAutoUpgrade = value;
      log_i("Updated fw_auto_upgrade: %d (%s)", value, value ? "enabled" : "disabled");
      config_updated = true;
      fields_validated++;
    }
    else
    {
      log_w("Rejected fw_auto_upgrade: %d (must be 0 or 1) - keeping current value: %d", value, p_tSys->fwAutoUpgrade);
      fields_rejected++;
    }
  }

  // gas_sensor_type - 0 (MICS6814) or 1 (MICS4514)
  if (!server_doc["gas_sensor_type"].isNull())
  {
    uint8_t value = server_doc["gas_sensor_type"].as<uint8_t>();
    if (value <= 1)
    {
      p_tSys->gasSensorType = value;
      log_i("Updated gas_sensor_type: %d (%s)", value,
            (value == 0) ? "MICS6814" : "MICS4514");
      config_updated = true;
      fields_validated++;
    }
    else
    {
      log_w("Rejected gas_sensor_type: %d (must be 0 or 1) - keeping current value: %d", value, p_tSys->gasSensorType);
      fields_rejected++;
    }
  }

  // ===== SUMMARY =====

  log_i("Configuration update summary: %d fields validated, %d fields rejected", fields_validated, fields_rejected);

  if (!config_updated)
  {
    log_i("No valid configuration changes received from server");
    return true;
  }

  log_i("System structures updated successfully from server configuration!");
  return true;
}

// Legacy uHalSdcard_checkLogFile function removed - now using date-based logging with automatic file creation

/****************************************************************************
 * @brief add to log
 *
 * @param path
 * @param oldpath
 * @param errpath
 * @param message
 * @return uint8_t
 ***************************************************************************/
uint8_t addToLog(const char *path, const char *oldpath, const char *errpath, String *message)
{ // adds new line to the log file at the top, after the header lines

  String temp = "";
  log_v("Log file is located at: %s\n", path);
  log_v("Old path is: %s\n", oldpath);
  if (!SD.exists(oldpath))
  {
    SD.rename(path, oldpath);
  }
  else
  {
    if (SD.exists(path))
      SD.rename(path, errpath);
    log_e("An error occurred, resuming logging from the old log...\n");
  }
  File oldfile = SD.open(oldpath, FILE_READ); // opening the renamed log
  if (!oldfile)
  {
    log_e("Error opening the renamed the log file!");
    return false;
  }
  File logfile = SD.open(path, FILE_WRITE); // recreates empty logfile
  if (!logfile)
  {
    log_e("Error recreating the log file!");
    return false;
  }
  temp = oldfile.readStringUntil('\r'); // reads until CR character
  logfile.println(temp);
  oldfile.readStringUntil('\n'); // consumes LF character (uses DOS-style CRLF)
  logfile.println(*message);     // printing the new string, only once and after the header
  log_v("New line added!\n");
  while (oldfile.available())
  {                                       // copy the old log file with new string added
    temp = oldfile.readStringUntil('\r'); // reads until CR character
    logfile.println(temp);
    oldfile.readStringUntil('\n'); // consumes LF character (uses DOS-style CRLF)
  }
  oldfile.close();
  logfile.close();
  SD.remove(oldpath); // deleting old log

  return true;
}

/*******************************************************************************
 * @brief log to SD card
 *
 * @param data
 * @param p_tSysData
 * @param p_tSys
 * @param p_tData
 * @param p_tDev
 ******************************************************************************/
/**************************************************************
 * @brief Create date-based log path (YYYY/MM/DD.csv format)
 * Creates folder structure: /YYYY/MM/DD.csv
 *************************************************************/
String sHalSdcard_createDateBasedLogPath(const struct tm* timeInfo)
{
  char yearStr[FOLDER_NAME_LEN];
  char monthStr[FOLDER_NAME_LEN];
  char dayStr[FOLDER_NAME_LEN]; // Generous buffer sizes to avoid any truncation warnings
  
  // Format components with leading zeros
  snprintf(yearStr, sizeof(yearStr), YEAR_FORMAT, timeInfo->tm_year + BASE_YEAR_OFFSET);
  snprintf(monthStr, sizeof(monthStr), MONTH_FORMAT, timeInfo->tm_mon + MONTH_OFFSET);
  snprintf(dayStr, sizeof(dayStr), DAY_FORMAT, timeInfo->tm_mday);
  
  // Create path: /YYYY/MM/DD.csv
  String logPath = PATH_SEPARATOR + String(yearStr) + PATH_SEPARATOR + String(monthStr) + PATH_SEPARATOR + String(dayStr) + LOG_FILE_EXTENSION;
  
  log_i("Generated log path: %s", logPath.c_str());
  return logPath;
}

/**************************************************************
 * @brief Ensure directory path exists, create if necessary
 *************************************************************/
bool bHalSdcard_ensureDirectoryExists(const String& dirPath)
{
  if (SD.exists(dirPath))
  {
    log_v("Directory already exists: %s", dirPath.c_str());
    return true;
  }
  
  log_i("Creating directory: %s", dirPath.c_str());
  if (SD.mkdir(dirPath))
  {
    log_i("Directory created successfully: %s", dirPath.c_str());
    return true;
  }
  else
  {
    log_e("Failed to create directory: %s", dirPath.c_str());
    return false;
  }
}

void vHalSdcard_logToSD(send_data_t *data, systemData_t *p_tSysData, systemStatus_t *p_tSys, sensorData_t *p_tData, deviceNetworkInfo_t *p_tDev)
{ // builds a new logfile line and calls addToLog() using date-based folder structure

  log_i("Logging data to date-based CSV structure on SD Card...");

  // Create date-based log path
  String logPath = sHalSdcard_createDateBasedLogPath(&data->sendTimeInfo);
  
  // Extract directory path from full file path
  int lastSlashIndex = logPath.lastIndexOf(*PATH_SEPARATOR);
  String dirPath = logPath.substring(0, lastSlashIndex);
  
  // Ensure year and month directories exist
  char yearStr[FOLDER_NAME_LEN];
  char monthStr[FOLDER_NAME_LEN]; // Generous buffer sizes to avoid any truncation warnings
  snprintf(yearStr, sizeof(yearStr), YEAR_FORMAT, data->sendTimeInfo.tm_year + BASE_YEAR_OFFSET);
  snprintf(monthStr, sizeof(monthStr), MONTH_FORMAT, data->sendTimeInfo.tm_mon + MONTH_OFFSET);
  
  String yearPath = PATH_SEPARATOR + String(yearStr);
  String monthPath = yearPath + PATH_SEPARATOR + String(monthStr);
  
  // Create directories step by step
  if (!bHalSdcard_ensureDirectoryExists(yearPath))
  {
    log_e("Failed to create year directory: %s", yearPath.c_str());
    return;
  }
  
  if (!bHalSdcard_ensureDirectoryExists(monthPath))
  {
    log_e("Failed to create month directory: %s", monthPath.c_str());
    return;
  }

  strftime(p_tSysData->Date, sizeof(p_tSysData->Date), DATE_FORMAT, &data->sendTimeInfo); // Formatting date as DD/MM/YYYY
  strftime(p_tSysData->Time, sizeof(p_tSysData->Time), TIME_FORMAT, &data->sendTimeInfo);       // Formatting time as HH:MM:SS

  String logvalue = "";
  char timeFormat[TIMEFORMAT_LEN] = {0};
  if (p_tSys->datetime)
    strftime(timeFormat, sizeof(timeFormat), ISO_DATETIME_FORMAT, &data->sendTimeInfo); // formatting date&time in TZ format

  // Data is layed out as follows:
  // "recordedAt;date;time;temp;hum;PM1;PM2_5;PM10;pres;radiation;nox;co;nh3;o3;voc;msp"
  // Note: Removed sent_ok field as data is always logged regardless of transmission status

  // Always log data regardless of transmission status
  logvalue += String(timeFormat);
  logvalue += FIRST_DATA_COLUMN_SEPARATOR;
  logvalue += String(p_tSysData->Date);
  logvalue += FIRST_DATA_COLUMN_SEPARATOR;
  logvalue += String(p_tSysData->Time);
  logvalue += FIRST_DATA_COLUMN_SEPARATOR;
  logvalue += String(data->sendTimeInfo.tm_year + BASE_YEAR_OFFSET); // Add year
  logvalue += FIRST_DATA_COLUMN_SEPARATOR;
  logvalue += String(data->sendTimeInfo.tm_mon + MONTH_OFFSET); // Add month (1-12)
  logvalue += FIRST_DATA_COLUMN_SEPARATOR;
  if (p_tData->status.BME680Sensor)
  {
    logvalue += vGeneric_floatToComma(data->temp);
  }
  logvalue += FIRST_DATA_COLUMN_SEPARATOR;
  if (p_tData->status.BME680Sensor)
  {
    logvalue += vGeneric_floatToComma(data->hum);
  }
  logvalue += FIRST_DATA_COLUMN_SEPARATOR;
  if (p_tData->status.PMS5003Sensor)
  {
    logvalue += String(data->PM1);
  }
  logvalue += FIRST_DATA_COLUMN_SEPARATOR;
  if (p_tData->status.PMS5003Sensor)
  {
    logvalue += String(data->PM25);
  }
  logvalue += FIRST_DATA_COLUMN_SEPARATOR;
  if (p_tData->status.PMS5003Sensor)
  {
    logvalue += String(data->PM10);
  }
  logvalue += FIRST_DATA_COLUMN_SEPARATOR;
  if (p_tData->status.BME680Sensor)
  {
    logvalue += vGeneric_floatToComma(data->pre);
  }
  logvalue += FIRST_DATA_COLUMN_SEPARATOR;
  logvalue += FIRST_DATA_COLUMN_SEPARATOR; // for "radiation"
  if (p_tData->status.MICS6814Sensor)
  {
    logvalue += vGeneric_floatToComma(data->MICS_NO2);
  }
  logvalue += FIRST_DATA_COLUMN_SEPARATOR;
  if (p_tData->status.MICS6814Sensor)
  {
    logvalue += vGeneric_floatToComma(data->MICS_CO);
  }
  logvalue += FIRST_DATA_COLUMN_SEPARATOR;
  if (p_tData->status.MICS6814Sensor)
  {
    logvalue += vGeneric_floatToComma(data->MICS_NH3);
  }
  logvalue += FIRST_DATA_COLUMN_SEPARATOR;
  if (p_tData->status.O3Sensor)
  {
    logvalue += vGeneric_floatToComma(data->ozone);
  }
  logvalue += FIRST_DATA_COLUMN_SEPARATOR;
  if (p_tData->status.BME680Sensor)
  {
    logvalue += vGeneric_floatToComma(data->VOC);
  }
  logvalue += FIRST_DATA_COLUMN_SEPARATOR;
  logvalue += String(data->MSP);

  // Simple append-based logging for date-based files
  bool needsHeader = !SD.exists(logPath);
  
  File logFile = SD.open(logPath, FILE_APPEND);
  if (!logFile)
  {
    log_e("Failed to open log file for writing: %s", logPath.c_str());
    vMsp_sendNetworkDataToDisplay(p_tDev, p_tSys, DISP_EVENT_SD_CARD_LOG_ERROR);
    return;
  }
  
  // Add header if this is a new file
  if (needsHeader)
  {
    logFile.println(CSV_HEADER);
    log_i("CSV header added to new log file: %s", logPath.c_str());
  }
  
  // Append the data
  logFile.println(logvalue);
  logFile.close();
  
  log_i("SD Card log file updated successfully: %s", logPath.c_str());
}

/******************************************************
 * @brief read SD card
 *
 * @param p_tSys
 * @param p_tDev
 * @param p_tData
 * @param pDev
 * @param p_tSysData
 *****************************************************/
void vHalSdcard_readSD(systemStatus_t *p_tSys, deviceNetworkInfo_t *p_tDev, sensorData_t *p_tData, deviceMeasurement_t *pDev, systemData_t *p_tSysData)
{

  log_i("Initializing SD Card...\n");
  vMsp_sendNetworkDataToDisplay(p_tDev, p_tSys, DISP_EVENT_SD_CARD_INIT);
  p_tSys->sdCard = initializeSD(p_tSys, p_tDev);
  if (p_tSys->sdCard)
  {
    log_i("SD Card ok! Reading configuration...\n");
    vMsp_sendNetworkDataToDisplay(p_tDev, p_tSys, DISP_EVENT_CONFIG_READ);
    p_tSys->configuration = checkConfig(CONFIG_PATH, p_tDev, p_tData, pDev, p_tSys, p_tSysData);
    if (p_tSys->server_ok)
    {
      log_e("No server URL defined. Can't upload data!\n");
      vMsp_sendNetworkDataToDisplay(p_tDev, p_tSys, DISP_EVENT_URL_UPLOAD_STAT);
    }
    // Legacy logpath system removed - now using date-based logging
  }
}

/********************************************************
 * @brief periodic SD card presence check
 *
 * @param p_tSys system status structure
 * @param p_tDev device network info structure
 * @return uint8_t current SD card presence status
 ********************************************************/
uint8_t vHalSdcard_periodicCheck(systemStatus_t *p_tSys, deviceNetworkInfo_t *p_tDev)
{
  static uint8_t previousSdStatus = UNINITIALIZED_MARKER; // Use as uninitialized marker
  uint8_t currentSdStatus = false;
  

  // Initialize previousSdStatus based on actual system status on first call
  if (previousSdStatus == UNINITIALIZED_MARKER)
  {
    previousSdStatus = p_tSys->sdCard;
  }

  // Quick check without lengthy initialization
  uint8_t cardType = SD.cardType();
  if (cardType != CARD_NONE)
  {
    currentSdStatus = true;
    log_v("SD Card periodic check: Present");
  }
  else
  {
    // Only try re-initialization if we think the card was previously present
    // This avoids unnecessary SD.begin() calls that might interfere with operations
    if (previousSdStatus == true)
    {
      // Try to re-initialize if card seems missing but was previously present
      if (SD.begin())
      {
        cardType = SD.cardType();
        if (cardType != CARD_NONE)
        {
          currentSdStatus = true;
          log_v("SD Card periodic check: Present (after re-init)");
        }
        else
        {
          currentSdStatus = false;
          log_v("SD Card periodic check: Not present");
        }
      }
      else
      {
        currentSdStatus = false;
        log_v("SD Card periodic check: Not present (re-init failed)");
      }
    }
    else
    {
      currentSdStatus = false;
      log_v("SD Card periodic check: Not present (no re-init attempted)");
    }
  }

  // Update system status and log changes
  if (currentSdStatus != previousSdStatus)
  {
    if (currentSdStatus)
    {
      log_i("SD Card detected - card was inserted");
      vMsp_sendNetworkDataToDisplay(p_tDev, p_tSys, DISP_EVENT_SD_CARD_INIT);
    }
    else
    {
      log_w("SD Card removed - card is no longer present");
      vMsp_sendNetworkDataToDisplay(p_tDev, p_tSys, DISP_EVENT_SD_CARD_NOT_PRESENT);
    }
    previousSdStatus = currentSdStatus;
  }

  // Update system status
  p_tSys->sdCard = currentSdStatus;


  return currentSdStatus;
}
