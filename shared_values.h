/****************************************************************************************
 * @file    shared_values.h
 * @author  AB-Engineering - https://ab-engineering.it
 * @brief   Shared values and definitions for the Milano Smart Park project
 * @version 0.1
 * @date    2025-07-26
 *
 * @copyright Copyright (c) 2025
 *
 ****************************************************************************************/

#ifndef SHARED_VALUES_H
#define SHARED_VALUES_H

// -- includes --
#include <Arduino.h>
#include <WiFiGeneric.h>
#include <stdbool.h>

//-- pin defines --
#define O3_ADC_PIN 32 /*!< O3 sensor ADC pin */

//-- common strings --
#define STR_DOT "."   /*!< Dot separator string */
#define STR_COMMA "," /*!< Comma separator string */
#define EMPTY_STR ""  /*!< Empty string */

//-- date/time buffer sizes --
#define DATE_LEN 11 /*!< Buffer size for date strings ("DD/MM/YYYY" + terminator) */
#define TIME_LEN 9  /*!< Buffer size for time strings ("HH:MM:SS" + terminator) */

#define TIME_MIN_TRIG 0 /*!<Time in minutes to trigger a data update to the server */
#define TIME_SEC_TRIG 0 /*!<Time in seconds to trigger a data update to the server */

#define MIN_TO_SEC(x) ((x) * 60)    /*!<Macro to convert minutes to seconds */
#define HOUR_TO_SEC(x) ((x) * 3600) /*!<Macro to convert hours to seconds */

//-- time conversion constants --
#define SEC_IN_HOUR 3600 /*!<Seconds in an hour */
#define SEC_IN_MIN 60    /*!<Seconds in a minute */
#define MIN_IN_HOUR 60   /*!< Minutes in an hour */
#define MIN_IN_DAY  1440 /*!< Minutes in a day (24 * 60) */
#define MIN_IN_HALF_DAY 720 /*!< Minutes in half a day (12 * 60) — midnight-rollover AM guard */
#define MS_IN_SEC 1000   /*!< Milliseconds in a second */

#define INVALID_DAY (-1) /*!< Sentinel for "no day recorded" (tm_yday is 0-365) */

//-- measurement interval --
#define AVG_MEASUREMENTS_MIN 1     /*!< Minimum measurement interval in minutes */
#define AVG_MEASUREMENTS_DEFAULT 5 /*!< Default measurement interval in minutes (must divide 60) */

//-- UART / PMS5003 serial --
#define PMS_UART_NUM 2      /*!< Hardware UART used by the PMS5003. Modes: UART0=0(debug out); UART1=1; UART2=2 */
#define PMS_SERIAL_BAUD 9600 /*!< PMS5003 serial baud rate */

//-- delays and timeouts (milliseconds) --
#define NETWORK_TASK_STARTUP_DELAY_MS 2000 /*!< Wait for the network task to initialize */
#define NETWORK_EVENT_WAIT_MS 5000         /*!< Wait window for a network connection event */
#define PMS_BOOT_DELAY_MS 1500             /*!< PMS5003 boot delay after serial begin */
#define PMS_INIT_DELAY_MS 1000             /*!< PMS5003 settle delay before first read */
#define SENSOR_RETRY_DELAY_MS 1000         /*!< Delay between sensor read retry attempts */
#define BME680_MEAS_PREP_DELAY_MS 500      /*!< BME680 measurement preparation time (gas sensor) */
#define BME680_NOT_READY_DELAY_MS 300      /*!< BME680 gas measurement completion wait (datasheet: 150-350ms) */
#define DISPLAY_IDLE_DELAY_MS 500          /*!< Display refresh delay while waiting for timeout */
#define SD_CARD_CHECK_INTERVAL_MS 30000    /*!< Periodic SD card presence check interval */
#define SEND_QUEUE_TIMEOUT_MS 500          /*!< Timeout for enqueueing data to the network task */
#define MICS4514_WARMUP_POLL_MS 5000.0f    /*!< Poll period while waiting for MICS4514 warmup */

//-- MICS4514 sensor (DFRobot SEN0377) --
#define MICS4514_WARMUP_TIME_MIN 2 /*!< Minimum warmup time in minutes */
#define MICS4514_I2C_ADDR 0x75     /*!< Default I2C address */

//-- I2C addresses (scanner identification) --
#define MICS6814_I2C_ADDR 0x04        /*!< MICS6814 default I2C address */
#define OLED_I2C_ADDR_PRIMARY 0x3C    /*!< OLED display primary I2C address */
#define OLED_I2C_ADDR_SECONDARY 0x3D  /*!< OLED display secondary I2C address */
#define ADS1X15_I2C_ADDR_GND 0x48     /*!< ADS1115/ADS1015 ADC, ADDR pin to GND */
#define ADS1X15_I2C_ADDR_VDD 0x49     /*!< ADS1115/ADS1015 ADC, ADDR pin to VDD */
#define ADS1X15_I2C_ADDR_SDA 0x4A     /*!< ADS1115/ADS1015 ADC, ADDR pin to SDA */
#define ADS1X15_I2C_ADDR_SCL 0x4B     /*!< ADS1115/ADS1015 ADC, ADDR pin to SCL */

//-- I2C bus scan --
#define I2C_SCAN_ADDR_MIN 1        /*!< First I2C address to scan */
#define I2C_SCAN_ADDR_MAX 127      /*!< Upper bound (exclusive) of the 7-bit I2C address space */
#define I2C_RESULT_SUCCESS 0       /*!< Wire.endTransmission(): success */
#define I2C_RESULT_OTHER_ERROR 4   /*!< Wire.endTransmission(): other error */

//-- general purpose outputs initialized at boot (legacy pin setup) --
#define SPARE_OUT_PIN_1 33 /*!< General purpose output pin, initialized at boot */
#define SPARE_OUT_PIN_2 26 /*!< General purpose output pin, initialized at boot */
#define SPARE_OUT_PIN_3 25 /*!< General purpose output pin, initialized at boot */

//-- default configuration strings --
#define FIRMWARE_UPDATE_FILE_PATH "/firmware.bin" /*!< Downloaded firmware image on SD card */
#define DEFAULT_WIFI_SSID "DefaultSSID"           /*!< Fallback WiFi SSID when SD configuration is missing */
#define DEFAULT_WIFI_PASSWORD "DefaultPass"       /*!< Fallback WiFi password when SD configuration is missing */
#define DEFAULT_MODEM_APN "internet"              /*!< Fallback modem APN when SD configuration is missing */
#define FALLBACK_API_SERVER "milanosmartpark.info" /*!< Used when API_SERVER is not defined at compile time */
#define DEFAULT_FW_VERSION "DEV"                   /*!< Used when VERSION_STRING is not defined at compile time */

/*!
 * @brief Main loop state machine context.
 */
typedef struct __STATE_MACHINE__
{
  uint8_t current_state;   /*!< State being executed in this loop iteration */
  uint8_t next_state;      /*!< State to execute on the next loop iteration */
  bool    isFirstTransition; /*!< True on the first entry into the current state */
  uint8_t return_state;    /*!< State to return to after a temporary detour */
  uint8_t prev_state;      /*!< State executed before the current one */
} state_machine_t;

/*!
 * @brief States of the main loop state machine.
 */
typedef enum __SYSTEM_STATES__
{
  SYS_STATE_UPDATE_CONFIG_FROM_SERVER, /*!< Apply configuration received from the server */
  SYS_STATE_WAIT_FOR_NTP_SYNC,         /*!< Wait for network connection and NTP time sync */
  SYS_STATE_FW_VERSION_CHECK,          /*!< Daily firmware update check */
  SYS_STATE_WAIT_FOR_TIMEOUT,          /*!< Wait for the next minute-aligned measurement slot */
  SYS_STATE_READ_SENSORS,              /*!< Sample all detected sensors */
  SYS_STATE_ERROR,                     /*!< Unrecoverable error, wait for reset */
  SYS_STATE_EVAL_SENSOR_STATUS,        /*!< Evaluate sensor errors and transmission boundary */
  SYS_STATE_SEND_DATA,                 /*!< Average the measurements and enqueue for transmission */
  SYS_STATE_MAX_STATES                 /*!< Number of states */
} system_states_t;

/*!
 * @brief Network event bits exchanged with the network task.
 */
typedef enum __MY_NETWORK_EVENTS__
{
  NET_EVENT_CONNECTED     = (1 << 0), /*!< Network connected event */
  NET_EVENT_DISCONNECTED  = (1 << 1), /*!< Network disconnected event */
  NET_EVENT_TIME_SYNCED   = (1 << 2), /*!< Time synchronization completed */
  NET_EVENT_DATA_SENT     = (1 << 3), /*!< Data transmission completed */
  NET_EVENT_ERROR         = (1 << 4), /*!< Network error occurred */
  //------------------------------
  NET_EVENT_MAX                       /*!< Maximum number of events */
} net_evt_t;

/*!
 * @brief Generic operation status.
 */
typedef enum __MSP_STAT_
{
  STATUS_ERR, /*!< Operation failed */
  STATUS_OK,  /*!< Operation succeeded */
} mspStatus_t;

/*!
 * @brief Detection status of each peripheral sensor (true when detected at boot).
 */
typedef struct __PERIPHERAL_STAT__
{
  uint8_t BME680Sensor;   /*!< BME680 gas sensor detected */
  uint8_t PMS5003Sensor;  /*!< PMS5003 particulate sensor detected */
  uint8_t MICS6814Sensor; /*!< MICS6814 pollution sensor detected */
  uint8_t MICS4514Sensor; /*!< MICS4514 pollution sensor detected */
  uint8_t O3Sensor;       /*!< ZE25-O3 ozone sensor detected */
  uint8_t MSP1Sensor;     /*!< MSP1 (MICS-2714 NO2 + NAU7802 + MCP4725) detected */
} peripheralStatus_t;

/*!
 * @brief BME680 environmental readings.
 */
typedef struct __BME680_DATA__
{
  float humidity;                  /*!< Relative humidity in percent */
  float temperature;               /*!< Temperature in degrees Celsius */
  float pressure;                  /*!< Sea-level compensated pressure in hPa */
  float volatileOrganicCompounds;  /*!< Compensated gas resistance in kOhm */
  float seaLevelAltitude;          /*!< Altitude above sea level in meters (for pressure compensation) */
} bme680Data_t;

/*!
 * @brief PMS5003 particulate matter readings.
 */
typedef struct __PMS5003_DATA__
{
  int32_t particleMicron1;  /*!< Particles that are 1.0 microns or smaller in diameter (ug/m3) */
  int32_t particleMicron25; /*!< Particles that are 2.5 microns or smaller in diameter (ug/m3) */
  int32_t particleMicron10; /*!< Particles that are 10.0 microns or smaller in diameter (ug/m3) */
} pms5003Data_t;

/*!
 * @brief Single MICS6814 sensor reading.
 */
typedef struct __MICS6814_SENSOR_READING__
{
  float carbonMonoxide;  /*!< Carbon monoxide CO reading in PPM */
  float nitrogenDioxide; /*!< Nitrogen dioxide NO2 reading in PPM */
  float ammonia;         /*!< Ammonia NH3 reading in PPM */
} MICS6814SensorReading_t;

/*!
 * @brief Single MICS4514 sensor reading.
 */
typedef struct __MICS4514_SENSOR_READING__
{
  float carbonMonoxide;  /*!< Carbon monoxide CO reading in PPM */
  float nitrogenDioxide; /*!< Nitrogen dioxide NO2 reading in PPM */
  float ammonia;         /*!< Ammonia NH3 reading in PPM */
} MICS4514SensorReading_t;

/*!
 * @brief MICS6814 base resistance (R0) values measured in clean air.
 */
typedef struct __MICS6814_RESISTANCE_VAL__
{
  uint16_t redSensor; /*!< RED channel base resistance (CO) */
  uint16_t oxSensor;  /*!< OX channel base resistance (NO2) */
  uint16_t nh3Sensor; /*!< NH3 channel base resistance */
} sensorR0Value_t;

/*!
 * @brief MICS6814 per-channel measurement offsets.
 */
typedef struct __MICS6814_OFFSET_VAL__
{
  int16_t redSensor; /*!< Typical CO detection */
  int16_t oxSensor;  /*!< Typical NO2 detection */
  int16_t nh3Sensor; /*!< Typical NH3 detection */
} sensorOffsetValue_t;

/*!
 * @brief Molar masses used for PPM to ug/m3 conversion.
 */
typedef struct __MICS6814_MOLAR_MASS_VAL__
{
  float carbonMonoxide;  /*!< CO molar mass (red sensor) */
  float nitrogenDioxide; /*!< NO2 molar mass (ox sensor) */
  float ammonia;         /*!< NH3 molar mass (NH3 sensor) */
} sensorMolarMassValue_t;

/*!
 * @brief Pollution gas concentrations in ug/m3.
 */
typedef struct __MICS6814_DATA_
{
  float carbonMonoxide;  /*!< Carbon monoxide CO 1 - 1000ppm */
  float nitrogenDioxide; /*!< Nitrogen dioxide NO2 0.05 - 10ppm */
  float ammonia;         /*!< Ammonia NH3 1 - 500ppm */
} pollutionReading;

/*!
 * @brief MICS6814 calibration data (base resistances and offsets).
 */
typedef struct __MICS_TUNING_DATA__
{
  sensorR0Value_t sensingResInAir;           /*!< Sensing resistance in clean air (R0) */
  sensorOffsetValue_t sensingResInAirOffset; /*!< Sensor offset values */
} MICS_Tuning_Data_t;

/*!
 * @brief MICS4514 runtime state.
 */
typedef struct __MICS4514_DATA__
{
  uint8_t warmupComplete; /*!< Track if sensor warmup is complete */
  uint8_t powerState;     /*!< Track current power state */
} MICS4514Data_t;

/*!
 * @brief MICS4514 raw ADC accumulator for cycle averaging.
 */
typedef struct __MICS4514_ADC_ACCUMULATOR__
{
  uint32_t oxVoltageSum;  /*!< Accumulated OX sensor ADC values */
  uint32_t redVoltageSum; /*!< Accumulated RED sensor ADC values */
} MICS4514AdcAccumulator_t;

/*!
 * @brief ZE25-O3 ozone sensor data.
 */
typedef struct __ZE25_O3_VAL__
{
  float ozone;          /*!< Ozone concentration in ug/m3 */
  int32_t o3ZeroOffset; /*!< Zero calibration offset (-1 = sensor disabled) */
} ze25Data_t;

/*!
 * @brief Environmental parameters for MICS6814-OX and BME680-VOC compensation.
 */
typedef struct __COMPENSATION_PARAMS_
{
  float currentTemperature; /*!< Temperature compensation parameter */
  float currentPressure;    /*!< Pressure compensation parameter */
  float currentHumidity;    /*!< Humidity compensation parameter */
  float inputGasResistance; /*!< Raw gas resistance input for compensation */
} compensationsParams_t;

/*!
 * @brief Aggregated sensor data, status and calibration for one device.
 */
typedef struct __SENSOR_DATA__
{
  peripheralStatus_t status;        /*!< Sensor detection status */
  peripheralStatus_t fault;         /*!< Per-measurement read fault: set in SYS_STATE_READ_SENSORS
                                         when a sensor present at boot (status set) fails all
                                         retries this measurement, cleared on the next successful
                                         read. Display shows FAULT instead of the stale last value. */
  bme680Data_t gasData;             /*!< BME680 environmental data */
  pms5003Data_t airQualityData;     /*!< PMS5003 particulate data */
  sensorMolarMassValue_t molarMass; /*!< Molar mass data */
  pollutionReading pollutionData;   /*!< Where to place the data taken from the MICSxxxx sensors */
  MICS_Tuning_Data_t micsTuningData;/*!< Used when gasSensorType == GAS_SENSOR_MICS6814 */
  MICS4514Data_t mics4514Data;      /*!< Used when gasSensorType == GAS_SENSOR_MICS4514 */
  MICS4514AdcAccumulator_t mics4514AdcAccumulator; /*!< Accumulate raw ADC values for averaging */
  ze25Data_t ozoneData;             /*!< ZE25-O3 ozone data */
  compensationsParams_t compParams; /*!< Variables for compensation (MICS6814-OX and BME680-VOC) */
  int8_t MSP;                       /*!< MSP# air quality index (-1 = not evaluated) */
} sensorData_t;

/*!
 * @brief Indexes into the per-sensor error flag array.
 */
typedef enum __SENSOR_STATUS_ARRAY__
{
  SENS_STAT_BME680    = 0,  /*!< BME680 sensor status */
  SENS_STAT_PMS5003   = 1,  /*!< PMS5003 sensor status */
  SENS_STAT_MICSxxxx  = 2,  /*!< MICS6814 sensor status */
  SENS_STAT_O3        = 3,  /*!< Ozone sensor status */
  SENS_STAT_MSP1      = 4,  /*!< MSP1 (MICS-2714 NO2) sensor status */
  // ---
  SENS_STAT_MAX /*!< Number of tracked sensors */
} sens_status_t;

/*!
 * @brief Per-cycle sensor reading error counters.
 */
typedef struct __LOOP_VARIABLES_
{
  int32_t count;     /*!< Retry counter for the sensor being read */
  int8_t BMEfails;   /*!< Failed BME680 readings in the current cycle */
  int8_t PMSfails;   /*!< Failed PMS5003 readings in the current cycle */
  int8_t MICSfails;  /*!< Failed MICSxxxx readings in the current cycle */
  int8_t O3fails;    /*!< Failed O3 readings in the current cycle */
  bool senserrs[SENS_STAT_MAX]; /*!< Per-sensor error flags (see sens_status_t) */
} errorVars_t;

/*!
 * @brief Indexes of the pollutants contributing to the MSP# index.
 */
typedef enum __MSP_ELEMENT_INDEX__
{
  MSP_INDEX_PM25  = 0,  /*!< PM2.5 index */
  MSP_INDEX_NO2   = 1,  /*!< NO2 index */
  MSP_INDEX_O3    = 2,  /*!< O3 index */
  MSP_INDEX_MAX   = 3   /*!< Number of pollutant indexes */
} msp_index_t;

/*!
 * @brief Supported gas sensor types.
 */
typedef enum __GAS_SENSOR_TYPE__
{
  GAS_SENSOR_MICS6814 = 0U, /*!< MICS6814 sensor (I2C, MiCS6814-I2C library) */
  GAS_SENSOR_MICS4514 = 1U, /*!< MICS4514 sensor (DFRobot SEN0377) */
  GAS_SENSOR_MSP1     = 2U, /*!< MSP1 sensor */
  // -----------------------
  GAS_SENSOR_MAX
} gas_sensor_type_t;

/*!
 * @brief System-wide status flags.
 */
typedef struct __SYSTEM_STATUS__
{
  uint8_t sdCard;        /*!< SD card detected and readable */
  uint8_t configuration; /*!< Valid configuration loaded */
  uint8_t connection;    /*!< Network connection established */
  uint8_t use_modem;     /*!< Use GSM modem instead of WiFi */
  uint8_t datetime;      /*!< Date and time are valid */
  uint8_t server_ok;     /*!< Upload server is configured */
  uint8_t fwAutoUpgrade; /*!< Automatic firmware upgrade enabled */
  uint8_t gasSensorType; /*!< 0 = MICS6814, 1 = MICS4514, etc. (see gas_sensor_type_t) */
} systemStatus_t;

/*!
 * @brief Device network configuration and identity.
 */
typedef struct __NETWORK__
{
  String ssid;     /*!< WiFi SSID */
  String passw;    /*!< WiFi password */
  String apn;      /*!< Modem APN */
  String deviceid; /*!< Device identifier for the upload server */
  // String logpath; // Removed - now using date-based logging
  wifi_power_t wifipow;   /*!< WiFi transmission power */
  char baseMacChr[18] = {0}; /*!< Device base MAC address string */
  String remain;   /*!< Display string: remaining networks */
  String noNet;    /*!< Display string: no network found */
  String foundNet; /*!< Display string: network found */
} deviceNetworkInfo_t;

/*!
 * @brief Measurement cycle bookkeeping and transmission timing.
 */
typedef struct __MEASUREMENT__
{
  int32_t avg_measurements;  /*!< Measurement interval in minutes (integer divisor of 60) */
  int32_t max_measurements;  /*!< Configured interval; kept in sync with avg_measurements */
  int32_t measurement_count; /*!< Number of measurements in the current cycle */
  bool data_transmitted; /*!< Flag to prevent duplicate transmissions in the same cycle */
  time_t last_transmission_epoch; /*!< Unix timestamp of last transmission (0 = never transmitted) */
  int32_t curr_minutes;       /*!< Current minute of the hour */
  int32_t curr_seconds;       /*!< Current second of the minute */
  int32_t curr_total_seconds; /*!< Seconds elapsed since the start of the current hour */
  int32_t delay_between_measurements; /*!< Delay between measurements in seconds */
  int32_t additional_delay;   /*!< Extra delay applied to the measurement trigger in seconds */
  uint32_t timeout_seconds;   /*!< Seconds remaining until the next measurement slot */
  uint8_t isPmsAwake;         /*!< PMS5003 is awake and ready to read */
  uint8_t isSensorDataAvailable; /*!< At least one measurement was taken (display can show data) */
  int32_t next_boundary_msm; /*!< Minutes since midnight of next clock-aligned transmission boundary */
} deviceMeasurement_t;

/*!
 * @brief System-wide runtime data (time sync, server, firmware version).
 */
typedef struct __SYSTEMDATA_T__
{
  String ntp_server; /*!< NTP server string */
  String timezone;   /*!< Time zone string */
  uint8_t sent_ok;   /*!< Sending data to server was successful? */
  uint8_t server_ok; /*!< Default server name for SSL data upload */
  String server;     /*!< Upload server hostname */
  String api_secret_salt; /*!< Salt used to authenticate with the API */
  String ver;             /*!< Firmware version string */
  String currentDataTime; /*!< Current date and time as a display string */
  char Date[DATE_LEN];    /*!< Current date string */
  char Time[TIME_LEN];    /*!< Current time string */
  int ntp_last_sync_day;  /*!< Day of year (0-365) when NTP was last synced */
  String server_config_response; /*!< JSON response from server with configuration */
  bool server_config_received;   /*!< Flag indicating new config available from server */
} systemData_t;

/*!
 * @brief Server configuration message for inter-task communication via queue.
 */
typedef struct __SERVER_CONFIG_MSG_T__
{
  char json_response[512];  /*!< Server JSON response (max 512 bytes) */
  uint16_t response_length; /*!< Actual length of response */
  bool valid;               /*!< Whether this message contains valid data */
} server_config_msg_t;

/*!
 * @brief Averaged measurement record queued for transmission to the server.
 */
typedef struct __SEND_DATA__
{
  tm sendTimeInfo; /*!< Date and time of the data to be sent */
  float temp;      /*!< Average temperature in degrees Celsius */
  float hum;       /*!< Average relative humidity in percent */
  float pre;       /*!< Average pressure in hPa */
  float VOC;       /*!< Average volatile organic compounds in kOhm */
  int32_t PM1;     /*!< Average PM1.0 in ug/m3 */
  int32_t PM25;    /*!< Average PM2.5 in ug/m3 */
  int32_t PM10;    /*!< Average PM10 in ug/m3 */
  float MICS_CO;   /*!< Average carbon monoxide in ug/m3 */
  float MICS_NO2;  /*!< Average nitrogen dioxide in ug/m3 */
  float MICS_NH3;  /*!< Average ammonia in ug/m3 */
  float ozone;     /*!< Average ozone in ug/m3 */
  int8_t MSP;      /*!< MSP# Index */
} send_data_t;

#endif

/*********************************** EOF ***********************************/