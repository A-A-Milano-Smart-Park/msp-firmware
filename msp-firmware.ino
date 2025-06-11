/*
                        Milano Smart Park Firmware
                       Developed by Norman Mulinacci

          This code is usable under the terms and conditions of the
             GNU GENERAL PUBLIC LICENSE Version 3, 29 June 2007 
 */

// I2C bus pins
#define I2C_SDA_PIN 21
#define I2C_SCL_PIN 22

// PMS5003 serial pins
// with WROVER module don't use UART 2 mode on pins 16 and 17: it crashes!
#define PMSERIAL_RX 14
#define PMSERIAL_TX 12

// Select modem type
#define TINY_GSM_MODEM_SIM800

// Serial modem pins
#define MODEM_RST            4
#define MODEM_TX             13
#define MODEM_RX             15

// O3 sensor ADC pin
#define O3_ADC_PIN 32

// Basic system libraries
#include <Arduino.h>
#include <FS.h>
#include <SD.h>
#include <Wire.h>

// current firmware version
#ifdef VERSION_STRING
String ver = VERSION_STRING;
#else
String ver = "DEV";
#endif

// WiFi Client, NTP time management, Modem and SSL libraries
#include <WiFi.h>
#include "time.h"
#include "esp_sntp.h"
#include <TinyGsmClient.h>
#include <SSLClient.h>
#include "libs/trust_anchor.h" // Server Trust Anchor

// Sensors management libraries
//for BME_680
#include <bsec.h>
//for PMS5003
#include <PMS.h>
//for MICS6814
#include <MiCS6814-I2C.h>

// OLED display library
#include <U8g2lib.h>

#ifdef API_SECRET_SALT
String api_secret_salt = API_SECRET_SALT;
#else
String api_secret_salt = "secret_salt";
#endif

// Default server name for SSL data upload
#ifdef API_SERVER
String server = API_SERVER;
bool server_ok = true;
#else
String server = "";
bool server_ok = false;
#endif

//Modem stuff
#if !defined(TINY_GSM_RX_BUFFER) // Increase RX buffer to capture the entire response
#define TINY_GSM_RX_BUFFER 650
#endif
//#define TINY_GSM_DEBUG Serial // Define the serial console for debug prints, if needed

// Hardware UART definitions. Modes: UART0=0(debug out); UART1=1; UART2=2
HardwareSerial gsmSerial(1);
HardwareSerial pmsSerial(2);

// Modem instance
/*
#ifdef DUMP_AT_COMMANDS
#include <StreamDebugger.h>
StreamDebugger debugger(gsmSerial, Serial);
TinyGsm        modem(debugger);
#else*/
TinyGsm        modem(gsmSerial);
//#endif

// Pin to get semi-random data from for SSL
// Pick a pin that's not connected or attached to a randomish voltage source
const int rand_pin = 35;

// Initialize the SSL client library
// We input a client, our trust anchors, and the analog pin
WiFiClient wifi_base;
TinyGsmClient gsm_base(modem);
SSLClient wificlient(wifi_base, TAs, (size_t)TAs_NUM, rand_pin, 1, SSLClient::SSL_ERROR);
SSLClient gsmclient(gsm_base, TAs, (size_t)TAs_NUM, rand_pin, 1, SSLClient::SSL_ERROR);

// BME680, PMS5003 and MICS6814 sensors instances
Bsec bme680;
PMS pms(pmsSerial);
MiCS6814 gas;

// Instance for the OLED 1.3" display with the SH1106 controller
U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE, I2C_SCL_PIN, I2C_SDA_PIN);   // ESP32 Thing, HW I2C with pin remapping

// Global network and system setup variables defaults
bool SD_ok = false;
bool cfg_ok = false;
bool connected_ok = false;
bool use_modem = false;
bool datetime_ok = false;
String ssid = "";
String passw = "";
String apn = "";
String deviceid = "";
String logpath = "";
wifi_power_t wifipow = WIFI_POWER_17dBm;
int32_t avg_measurements = 1;
int32_t avg_delay = 0;
int32_t max_measurements = 0;

typedef struct __STATE_MACHINE__ {
  uint8_t current_state;
  uint8_t next_state;
  uint8_t isFirstTransition;
  uint8_t returnState;
} state_machine_t;

// Create a new state machine instance
state_machine_t mainStateMachine = {0};

typedef enum __SYSTEM_STATES__ {
  SYS_STATE_WAIT_FOR_TIMEOUT,
  SYS_STATE_MANAGE_INTERNET_CONNECTION,
  SYS_STATE_READ_SENSORS ,
  SYS_STATE_ERROR,
  SYS_STATE_UPDATE_DATE_TIME,
  SYS_STATE_EVAL_SENSOR_STATUS,
  SYS_STATE_SEND_DATA,
  SYS_STATE_MAX_STATES
} system_states_t;

// Sensor active variables
bool BME_run = false;
bool PMS_run = false;
bool MICS_run = false;
bool MICS4514_run = false;
bool O3_run = false;

// Variables for BME680
float hum = 0.0;
float temp = 0.0;
float pre = 0.0;
float VOC = 0.0;
float sealevelalt = 122.0; /*!<sea level altitude in meters, defaults to Milan, Italy */

// Variables and structure for PMS5003
PMS::DATA data;
int32_t PM1 = 0;
int32_t PM10 = 0;
int32_t PM25 = 0;

// Variables for MICS
uint16_t MICSCal[3] = {955, 900, 163}; /*!<default R0 values for the sensor (RED, OX, NH3) */
int16_t MICSOffset[3] = {0, 0, 0};     /*!<default point offset values for sensor measurements (RED, OX, NH3) */
const float MICSmm[3] = {28.01f, 46.01f, 17.03f}; /*!<molar mass of (CO, NO2, NH3) */
float MICS_CO = 0.0;
float MICS_NO2 = 0.0;
float MICS_NH3 = 0.0;

// Variables for ZE25-O3
float ozone = 0.0;
int32_t o3zeroval = -1; /*!< ozone sensor ADC zero default offset is 1489, -1 to disable it (0.4V to 1.1V range in 12bit resolution at 0dB attenuation) */

/// \brief Variables for compensation (MICS6814-OX and BME680-VOC) */
float compH = 0.6f;    /*!<default humidity compensation */
float compT = 1.352f;  /*!<default temperature compensation */
float compP = 0.0132f; /*!<default pressure compensation */

/// \brief Date and time vars
String ntp_server = "pool.ntp.org"; /*!<default NTP server */
String timezone = "GMT0";      /*!<default timezone */
struct tm timeinfo;
char Date[11] = {0};
char Time[9] = {0};

#define TIME_MIN_TRIG 0 /*!<Time in minutes to trigger a data update to the server */
#define TIME_SEC_TRIG 0 /*!<Time in seconds to trigger a data update to the server */

#define MIN_TO_SEC(x) ((x) * 60) /*!<Macro to convert minutes to seconds */
#define HOUR_TO_SEC(x) ((x) * 3600) /*!<Macro to convert hours to seconds */

#define SEC_IN_HOUR 3600 /*!<Seconds in an hour */
#define SEC_IN_MIN 60 /*!<Seconds in a minute */

#define SEND_DATA_TIMEOUT_IN_SEC (5 * SEC_IN_MIN) /*!<Timeout for sending data to server in seconds, defaults to 5 minutes */

#define PMS_PREHEAT_TIME_IN_SEC 20 /*!<PMS5003 preheat time in seconds, defaults to 45 seconds */

/// \brief Var for MSP# evaluation
int8_t MSP = -1; /*!<set to -1 to distinguish from grey (0) */

// Sending data to server was successful?
bool sent_ok = false;

#include "libs/sensors.h"
#include "libs/display.h"
#include "libs/sdcard.h"
#include "libs/network.h"

//*******************************************************************************************************************************
//******************************************  SERVER CONNECTION TASK  ***********************************************************
//*******************************************************************************************************************************
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

// Task stack size and priority
#define SEND_DATA_TASK_STACK_SIZE (8*1024) // 8KB stack size for the task
#define SEND_DATA_TASK_PRIORITY   1

// Static task variables
StaticTask_t sendDataTaskBuffer;
StackType_t  sendDataTaskStack[SEND_DATA_TASK_STACK_SIZE];
TaskHandle_t sendDataTaskHandle = NULL;

void sendDataTask(void *pvParameters);

#define SEND_DATA_QUEUE_LENGTH 2

QueueHandle_t sendDataQueue = NULL;

// Wrapper to send data to the queue
bool enqueueSendData(const send_data_t& data, TickType_t ticksToWait = portMAX_DELAY) {
  if (sendDataQueue == NULL) return false;
  return xQueueSend(sendDataQueue, &data, ticksToWait) == pdPASS;
}

// Wrapper to receive data from the queue
bool dequeueSendData(send_data_t* data, TickType_t ticksToWait = portMAX_DELAY) {
  if (sendDataQueue == NULL || data == NULL) return false;
  return xQueueReceive(sendDataQueue, data, ticksToWait) == pdPASS;
}

// Create the queue before using it (call this in setup)
void initSendDataQueue() {
  if (sendDataQueue == NULL) {
    sendDataQueue = xQueueCreate(SEND_DATA_QUEUE_LENGTH, sizeof(send_data_t));
  }
}

//*******************************************************************************************************************************
// LOOP VARIABLES
int32_t errcount = 0;
int8_t BMEfails = 0;
float currtemp = 0.0;
float currpre = 0.0;
float currhum = 0.0;
int8_t PMSfails = 0;
int8_t MICSfails = 0;
int8_t O3fails = 0;
bool senserrs[SENS_STAT_MAX] = {false};

int32_t measurement_count = 0; /*!< Number of measurements in the current cycle */
int32_t curr_minutes = 0;
int32_t curr_seconds = 0;
int32_t curr_total_seconds = 0;
int32_t delay_between_measurements = 0; /*!< Delay between measurements in seconds */
int32_t additional_delay = 0;

//*******************************************************************************************************************************

//*******************************************************************************************************************************
//******************************************  S E T U P  ************************************************************************
//*******************************************************************************************************************************
void setup() {

  // INIT SERIAL, I2C, DISPLAY ++++++++++++++++++++++++++++++++++++
  Serial.begin(115200);
  delay(2000); // give time to serial to initialize properly
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  u8g2.begin();
  //+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

  // SET PIN MODES ++++++++++++++++++++++++++++++++++++
  pinMode(33, OUTPUT);
  pinMode(26, OUTPUT);
  pinMode(25, OUTPUT);
  pinMode(O3_ADC_PIN, INPUT_PULLDOWN);
  analogSetAttenuation(ADC_11db);
  pinMode(MODEM_RST, OUTPUT);
  digitalWrite(MODEM_RST, HIGH);
  //+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

  // Initialize the data queue:
  initSendDataQueue();
  
  // Initialize the send data task
  sendDataTaskHandle = xTaskCreateStaticPinnedToCore(
    sendDataTask,                 // Task function
    "SendDataTask",               // Name
    SEND_DATA_TASK_STACK_SIZE,    // Stack size
    NULL,                         // Parameters
    SEND_DATA_TASK_PRIORITY,      // Priority
    sendDataTaskStack,           // Stack buffer
    &sendDataTaskBuffer,         // Task buffer
    1                             // Core 1
  );

  // BOOT STRINGS ++++++++++++++++++++++++++++++++++++++++++++++++++++++
  Serial.println("\nMILANO SMART PARK");
  Serial.println("FIRMWARE " + ver + " by Norman Mulinacci");
  Serial.println("Compiled " + String(__DATE__) + " " + String(__TIME__) + "\n");
  drawBoot(&ver);
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

  printWiFiMACAddr();
  readSD();

  max_measurements = avg_measurements; // set current measurements to average measurements

  //++++++++++++++++ DETECT AND INIT SENSORS ++++++++++++++++++++++++++++++
  log_i("Detecting and initializing sensors...\n");

  // BME680 +++++++++++++++++++++++++++++++++++++
  drawTwoLines("Detecting BME680...", "", 0);
  bme680.begin(BME68X_I2C_ADDR_HIGH, Wire);
  if (checkBMESensor()) {
    log_i("BME680 sensor detected, initializing...\n");
    drawTwoLines("Detecting BME680...", "BME680 -> Ok!", 1);
    bsec_virtual_sensor_t sensor_list[] = {
      BSEC_OUTPUT_RAW_TEMPERATURE,
      BSEC_OUTPUT_RAW_PRESSURE,
      BSEC_OUTPUT_RAW_HUMIDITY,
      BSEC_OUTPUT_RAW_GAS,
      BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_TEMPERATURE,
      BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_HUMIDITY,
    };
    bme680.updateSubscription(sensor_list, sizeof(sensor_list) / sizeof(sensor_list[0]), BSEC_SAMPLE_RATE_LP);
    BME_run = true;
  } else {
    log_e("BME680 sensor not detected!\n");
    drawTwoLines("Detecting BME680...", "BME680 -> Err!", 1);
  }
  //+++++++++++++++++++++++++++++++++++++++++++++

  // PMS5003 ++++++++++++++++++++++++++++++++++++
  pmsSerial.begin(9600, SERIAL_8N1, PMSERIAL_RX, PMSERIAL_TX); // baud, type, ESP_RX, ESP_TX
  delay(1500);
  drawTwoLines("Detecting PMS5003...", "", 0);
  pms.wakeUp(); // Waking up sensor after sleep
  delay(1500);
  if (pms.readUntil(data)) {
    log_i("PMS5003 sensor detected, initializing...\n");
    drawTwoLines("Detecting PMS5003...", "PMS5003 -> Ok!", 1);
    PMS_run = true;
    pms.sleep(); // Putting sensor to sleep
  } else {
    log_e("PMS5003 sensor not detected!\n");
    drawTwoLines("Detecting PMS5003...", "PMS5003 -> Err!", 1);
  }
  //++++++++++++++++++++++++++++++++++++++++++++++

  // MICS6814 ++++++++++++++++++++++++++++++++++++
  drawTwoLines("Detecting MICS6814...", "", 0);
  if (gas.begin()) { // Connect to sensor using default I2C address (0x04)
    log_i("MICS6814 sensor detected, initializing...\n");
    drawTwoLines("Detecting MICS6814...", "MICS6814 -> Ok!", 0);
    MICS_run = true;
    gas.powerOn(); // turn on heating element and led
    gas.ledOn();
    delay(1500);
    if (checkMicsValues()) {
      log_i("MICS6814 R0 values are already as default!\n");
      drawLine("MICS6814 values OK!", 1);
    } else {
      log_i("Setting MICS6814 R0 values as default... ");
      drawLine("Setting MICS6814...", 1);
      writeMicsValues();
      log_i("Done!\n");
      drawLine("Done!", 1);
    }
    drawMicsValues(gas.getBaseResistance(CH_RED), gas.getBaseResistance(CH_OX), gas.getBaseResistance(CH_NH3));
    gas.setOffsets(MICSOffset);
  } else {
    log_e("MICS6814 sensor not detected!\n");
    drawTwoLines("Detecting MICS6814...", "MICS6814 -> Err!", 1);
  }
  //+++++++++++++++++++++++++++++++++++++++++++++++++++

  // O3 ++++++++++++++++++++++++++++++++++++++++++
  drawTwoLines("Detecting O3...", "", 1);
  if (!isAnalogO3Connected()) {
    log_e("O3 sensor not detected!\n");
    drawTwoLines("Detecting O3...", "O3 -> Err!", 0);
  } else {
    log_i("O3 sensor detected, running...\n");
    drawTwoLines("Detecting O3...", "O3 -> Ok!", 0);
    O3_run = true;
  }
  delay(1500);
  //+++++++++++++++++++++++++++++++++++++++++++++++++++++
  
  if (PMS_run)
  {
    additional_delay = PMS_PREHEAT_TIME_IN_SEC;
  }
  else
  {
    additional_delay = 0; /*!< No additional delay if PMS is not running */
  }

  // Calculate delay between measurements
  delay_between_measurements = SEC_IN_MIN;

  avg_measurements = 0;
  measurement_count = 0; /*!< Reset measurement count */

  sent_ok = false;
  memset(&Date, 0, sizeof(Date));
  memset(&Time, 0, sizeof(Time));

  measurement_count = 0; /*!< Number of measurements in the current cycle */
  curr_minutes = 0;
  curr_seconds = 0;
  curr_total_seconds = 0;

  // CONNECT TO INTERNET AND GET DATE&TIME +++++++++++++++++++++++++++++++++++++++++++++++++++
  if (cfg_ok) connAndGetTime();
  //++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

  mainStateMachine.current_state = SYS_STATE_MANAGE_INTERNET_CONNECTION;  // set initial state
  mainStateMachine.next_state = SYS_STATE_MANAGE_INTERNET_CONNECTION;  // set next state
  mainStateMachine.isFirstTransition = 1; // set first transition flag

}// end of SETUP
//*******************************************************************************************************************************
//*******************************************************************************************************************************
//*******************************************************************************************************************************

// sendData task function
void sendDataTask(void *pvParameters) {
  send_data_t data;
  while (1) {

    if (dequeueSendData(&data, portMAX_DELAY)) {
      // Connect and send data to the server
      if (server_ok && connected_ok && datetime_ok) {
        connectToServer((use_modem) ? &gsmclient : &wificlient, &data);
      }

      Serial.println("Writing log to SD card...");
      if (SD_ok)
      {
        if (checkLogFile())
        {
          logToSD(&data);
        }
      }

      // Print measurements to serial and draw them on the display
      printMeasurementsOnSerial(&data);
      // drawMeasurements(data.temp, data.hum, data.pre, data.VOC, data.PM1, data.PM25, data.PM10, data.MICS_CO, data.MICS_NO2, data.MICS_NH3, data.ozone); 
    }
  }
}

//*******************************************************************************************************************************
//********************************************  L O O P  ************************************************************************
//*******************************************************************************************************************************
void loop()
{
  switch (mainStateMachine.current_state)  // state machine for the main loop
  {
    
    case SYS_STATE_UPDATE_DATE_TIME:
    {
      if (cfg_ok) connAndGetTime();

      mainStateMachine.next_state = SYS_STATE_WAIT_FOR_TIMEOUT;
      break;
    }

    case SYS_STATE_MANAGE_INTERNET_CONNECTION:
    {
      // DISCONNECTING AND TURNING OFF WIFI ONLY IF MODEM IS USED
      if (use_modem == true)
      {
        disconnectWiFi();
      }
      mainStateMachine.next_state = SYS_STATE_WAIT_FOR_TIMEOUT;  // go to next state
      break;
    }

    case SYS_STATE_WAIT_FOR_TIMEOUT:
    {
      if (getLocalTime(&timeinfo))
      {
        curr_minutes = timeinfo.tm_min;
        curr_seconds = timeinfo.tm_sec;
        curr_total_seconds = curr_minutes * SEC_IN_MIN + curr_seconds;

        // Calculate the timeout in seconds taking into account the additional delay for PMS5003 preheating
        uint32_t timeout_seconds = ((curr_total_seconds + additional_delay)  % delay_between_measurements);
        
        // It is time for a measurement
        if((timeout_seconds == 0) && (curr_minutes != 0))
        {
          Serial.println("Timeout expired!");
          Serial.printf("Current time: %02d:%02d:%02d\n", timeinfo.tm_hour, curr_minutes, curr_seconds);

          drawTwoLines("Timeout Expired", "Reading Sensors", 0);
          mainStateMachine.next_state = SYS_STATE_READ_SENSORS;  // go to read sensors state
        }
        else
        {
          char firstRow[17] = {0};
          char secondRow[17] = {0};
          sprintf(firstRow, "meas:%d of %d\0", measurement_count , avg_measurements);
          sprintf(secondRow, "WAIT %02d:%02d min\0", (delay_between_measurements - timeout_seconds) / 60, (delay_between_measurements - timeout_seconds) % 60);
          drawTwoLines(firstRow, secondRow, 0);
          delay(500);
        }
      }
      else
      {
        log_e("Failed to obtain time from NTP!");
        mainStateMachine.next_state = SYS_STATE_WAIT_FOR_TIMEOUT;
      }
      break;
    }

    case SYS_STATE_READ_SENSORS:
    {
      Serial.println("Reading sensors...");
      
      if(mainStateMachine.isFirstTransition)
      {
        mainStateMachine.isFirstTransition = 0; // reset first transition flag
        // Reset sensor variables
        temp = 0.0f;
        pre = 0.0f;
        hum = 0.0f;
        VOC = 0.0f;
        PM1 = 0;
        PM25 = 0;
        PM10 = 0;
        MICS_CO = 0.0f;
        MICS_NO2 = 0.0f;
        MICS_NH3 = 0.0f;
        ozone = 0.0f;

        // Reset current sensor readings
        currtemp = 0.0f;
        currpre = 0.0f;
        currhum = 0.0f;

        BMEfails = 0;
        PMSfails = 0;
        MICSfails = 0;
        O3fails = 0;
        // Reset error counters
        errcount = 0;

        // Update the number of measurements before the next data transmission
        avg_measurements = max_measurements - (curr_minutes % max_measurements); // Calculate the number of measurements to be made in the current cycle

        if(avg_measurements == 0)
        {
          avg_measurements = 1; // If the number of measurements is 0, set it to 1
          // it means we are starting the measurement in the exact minute in which we want to send the data
        }

        log_i("First Transition!\n");
        log_i("Current minutes: %d, avg_measurements: %d, max_measurements: %d\n", curr_minutes, avg_measurements, max_measurements);

        // Reset also the measurement count
        measurement_count = 0;
      }

      // WAKE UP AND PREHEAT PMS5003
      if (PMS_run)
      {
        Serial.printf("Waking up and preheating PMS5003 sensor for %d seconds...\n", PMS_PREHEAT_TIME_IN_SEC);
        pms.wakeUp();
        drawCountdown(PMS_PREHEAT_TIME_IN_SEC, "Preheating PMS5003...");
      }

      // MEASUREMENTS MESSAGE
      log_i("Measurements in progress...\n");
      drawTwoLines("Measurements", "in progress...", 0);

      // READING BME680
      if (BME_run)
      {
        float bmeread = 0.0;
        log_i("Sampling BME680 sensor...");
        errcount = 0;
        while (1)
        { // TODO: add a timeout to this loop
          if (!checkBMESensor())
          {
            if (errcount > 2)
            {
              log_e("Error while sampling BME680 sensor!");
              BMEfails++;
              break;
            }
            delay(1000);
            errcount++;
            continue;
          }
          if (!bme680.run())
          {
            continue;
          }

          bmeread = bme680.temperature;
          log_v("Temperature(*C): %.3f", bmeread);
          currtemp = bmeread;
          temp += bmeread;

          bmeread = bme680.pressure / 100.0f;
          bmeread = (bmeread * pow(1 - (0.0065f * sealevelalt / (currtemp + 0.0065f * sealevelalt + 273.15f)), -5.257f));
          log_v("Pressure(hPa): %.3f", bmeread);
          currpre = bmeread;
          pre += bmeread;

          bmeread = bme680.humidity;
          log_v("Humidity(perc.): %.3f", bmeread);
          currhum = bmeread;
          hum += bmeread;

          bmeread = bme680.gasResistance / 1000.0f;
          bmeread = no2AndVocCompensation(bmeread, currtemp, currpre, currhum);
          log_v("Compensated gas resistance(kOhm): %.3f\n", bmeread);
          VOC += bmeread;

          break;
        }
      }

      // READING PMS5003
      if (PMS_run)
      {
        log_i("Sampling PMS5003 sensor...");
        errcount = 0;
        while (1)
        { // TODO: add a timeout to this loop
          while (pmsSerial.available())
          {
            pmsSerial.read();
          }
          if (!pms.readUntil(data))
          {
            if (errcount > 2)
            {
              log_e("Error while sampling PMS5003 sensor!");
              PMSfails++;
              break;
            }
            delay(1000);
            errcount++;
            continue;
          }
          log_v("PM1(ug/m3): %d", data.PM_AE_UG_1_0);
          PM1 += data.PM_AE_UG_1_0;

          log_v("PM2,5(ug/m3): %d", data.PM_AE_UG_2_5);
          PM25 += data.PM_AE_UG_2_5;

          log_v("PM10(ug/m3): %d\n", data.PM_AE_UG_10_0);
          PM10 += data.PM_AE_UG_10_0;

          break;
        }
      }

      // READING MICS6814
      if (MICS_run) {
        log_i("Sampling MICS6814 sensor...");
        errcount = 0;
        while (1) { // TODO: add a timeout to this loop
          float micsread[3];

          micsread[0] = gas.measureCO();
          micsread[1] = gas.measureNO2();
          micsread[2] = gas.measureNH3();

          if ((micsread[0] < 0) || (micsread[1] < 0) || (micsread[2] < 0)) {
            if (errcount > 2) {
              log_e("Error while sampling!");
              MICSfails++;
              break;
            }
            errcount++;
            delay(1000);
            continue;
          }

          micsread[0] = convertPpmToUgM3(micsread[0], MICSmm[0]);
          log_v("CO(ug/m3): %.3f", micsread[0]);
          MICS_CO += micsread[0];

          micsread[1] = convertPpmToUgM3(micsread[1], MICSmm[1]);
          if (BME_run) micsread[1] = no2AndVocCompensation(micsread[1], currtemp, currpre, currhum);
          log_v("NOx(ug/m3): %.3f", micsread[1]);
          MICS_NO2 += micsread[1];

          micsread[2] = convertPpmToUgM3(micsread[2], MICSmm[2]);
          log_v("NH3(ug/m3): %.3f\n", micsread[2]);
          MICS_NH3 += micsread[2];

          break;
        }
      }

      // READING O3
      if (O3_run)
      {
        float o3read = 0.0;
        log_i("Sampling O3 sensor...");
        errcount = 0;
        while (1)
        { // TODO: add a timeout to this loop
          if (!isAnalogO3Connected())
          {
            if (errcount > 2)
            {
              log_e("Error while sampling O3 sensor!");
              O3fails++;
              break;
            }
            delay(1000);
            errcount++;
            continue;
          }
          o3read = analogUgM3O3Read(&currtemp);
          log_v("O3(ug/m3): %.3f", o3read);
          ozone += o3read;

          break;
        }
        Serial.println();
      }

      // PUT PMS5003 TO SLEEP
      if (PMS_run)
      {
        log_i("Putting PMS5003 sensor to sleep...\n");
        pms.sleep();
      }

      measurement_count++;

      log_i("Sensor values AFTER evaluation:\n");
      log_i("temp: %.2f, hum: %.2f, pre: %.2f, VOC: %.2f, PM1: %d, PM25: %d, PM10: %d, MICS_CO: %.2f, MICS_NO2: %.2f, MICS_NH3: %.2f, ozone: %.2f, MSP: %d, measurement_count: %d\n",
              temp, hum, pre, VOC, PM1, PM25, PM10, MICS_CO, MICS_NO2, MICS_NH3, ozone, MSP, measurement_count);

      log_i("Measurements done, going to timeout...\n");
      mainStateMachine.next_state = SYS_STATE_EVAL_SENSOR_STATUS; 
      break;
    }
    
    case SYS_STATE_EVAL_SENSOR_STATUS:
    {
      Serial.println("Evaluating sensor status...");
      int8_t sens_stat_count = 0;
      for (sens_stat_count = 0; sens_stat_count < SENS_STAT_MAX; sens_stat_count++)
      {
        if (senserrs[sens_stat_count] == true)
        {
          switch (sens_stat_count)
          {
            case SENS_STAT_BME680:
              BME_run = true;
              break;
            case SENS_STAT_PMS5003:
              PMS_run = true;
              break;
            case SENS_STAT_MICS6814:
              MICS_run = true;
              break;
            case SENS_STAT_O3:
              O3_run = true;
              break;
          }
        }
      }

      if(measurement_count >= avg_measurements)
      {
        log_i("All measurements obtained, going to send data...\n");
        mainStateMachine.next_state = SYS_STATE_SEND_DATA;  // go to send data state
      }
      else
      {
        log_i("Not enough measurements obtained, going to wait for timeout...\n");
        mainStateMachine.next_state = SYS_STATE_WAIT_FOR_TIMEOUT;
      }
      
      break;
    }

    case SYS_STATE_SEND_DATA:
    {
      log_i("Sending data to server...\n");

      // We have obtained all the measurements, do the mean and transmit the data
      log_i("All measurements obtained, sending data...\n");
      log_i("Sensor values BEFORE AVERAGE:\n");
      log_i("temp: %.2f, hum: %.2f, pre: %.2f, VOC: %.2f, PM1: %d, PM25: %d, PM10: %d, MICS_CO: %.2f, MICS_NO2: %.2f, MICS_NH3: %.2f, ozone: %.2f, MSP: %d, measurement_count: %d vs %d\n",
        temp, hum, pre, VOC, PM1, PM25, PM10, MICS_CO, MICS_NO2, MICS_NH3, ozone, MSP, measurement_count, avg_measurements);

      drawTwoLines("All measurements", "obtained, sending...", 0);
      if (BME_run || PMS_run || MICS_run || MICS4514_run || O3_run)
      {
        performAverages(BMEfails, PMSfails, MICSfails, O3fails, &senserrs[0], measurement_count);
      }

      // MSP# Index evaluation
      MSP = evaluateMSPIndex(PM25, MICS_NO2, ozone);

      Serial.println("Sending data to server...");
      send_data_t sendData;
      memcpy(&sendData.sendTimeInfo, &timeinfo, sizeof(struct tm)); // copy time info to sendData
      log_i("Current time: %02d:%02d:%02d\n", sendData.sendTimeInfo.tm_hour, sendData.sendTimeInfo.tm_min, sendData.sendTimeInfo.tm_sec);
      // The measuremente starts the minute before the timeout, so we need to add the additional delay
      // additional delay in seconds and the next minute
      sendData.sendTimeInfo.tm_sec = 0;
      // Scale up the minutes and hour to obtain the correct time
      if(sendData.sendTimeInfo.tm_min == 59)
      {
        sendData.sendTimeInfo.tm_min = 0;
        sendData.sendTimeInfo.tm_hour++;
        if(sendData.sendTimeInfo.tm_hour >= 24)
        {
          sendData.sendTimeInfo.tm_hour = 0; // reset hour to 0 if it is 24
        }
      }
      else
      {
        sendData.sendTimeInfo.tm_min++;
      }
      log_i("Scaled time: %02d:%02d:%02d\n", sendData.sendTimeInfo.tm_hour, sendData.sendTimeInfo.tm_min, sendData.sendTimeInfo.tm_sec);
      sendData.temp = temp;
      sendData.hum = hum;
      sendData.pre = pre;
      sendData.VOC = VOC;
      sendData.PM1 = PM1;
      sendData.PM25 = PM25;
      sendData.PM10 = PM10;
      sendData.MICS_CO = MICS_CO;
      sendData.MICS_NO2 = MICS_NO2;
      sendData.MICS_NH3 = MICS_NH3;
      sendData.ozone = ozone;
      sendData.MSP = MSP;

      log_i("Sensor values AFTER AVERAGE:\n");
      log_i("temp: %.2f, hum: %.2f, pre: %.2f, VOC: %.2f, PM1: %d, PM25: %d, PM10: %d, MICS_CO: %.2f, MICS_NO2: %.2f, MICS_NH3: %.2f, ozone: %.2f, MSP: %d, measurement_count: %d\n",
        temp, hum, pre, VOC, PM1, PM25, PM10, MICS_CO, MICS_NO2, MICS_NH3, ozone, MSP, measurement_count);

      log_i("Send Time: %02d:%02d:%02d\n", sendData.sendTimeInfo.tm_hour, sendData.sendTimeInfo.tm_min, sendData.sendTimeInfo.tm_sec);

      enqueueSendData(sendData);

      mainStateMachine.isFirstTransition = 1; // set first transition flag
      // This will clean up the sensor variables for the next cycle

      mainStateMachine.next_state = SYS_STATE_WAIT_FOR_TIMEOUT;  // go to wait for timeout state
      break;
    }

    case SYS_STATE_ERROR:
    {
      log_e("System in error state! Waiting for reset...");
      drawTwoLines("System in error!", "Waiting for reset...", 10);
      mainStateMachine.next_state = SYS_STATE_WAIT_FOR_TIMEOUT;  // go to init vars state
      break;
    }

    default:
    {
      log_e("Unknown state %d!", mainStateMachine.current_state);
      mainStateMachine.next_state = SYS_STATE_WAIT_FOR_TIMEOUT;  // go to error state
      break;
    }
  }
  mainStateMachine.current_state = mainStateMachine.next_state;  // update current state to next state

}
