/*
                        Milano Smart Park Firmware
                   Copyright (c) 2021 Norman Mulinacci

          This code is usable under the terms and conditions of the
             GNU GENERAL PUBLIC LICENSE Version 3, 29 June 2007

             Parts of this code are based on open source works
                 freely distributed by Luca Crotti @2019
*/

// Basic system libraries
#include <Arduino.h>
#include <FS.h>
#include <SD.h>
#include <Wire.h>

#ifdef VERSION_STRING // current firmware version
String ver = VERSION_STRING;
#else
String ver = "v3.1.0";
#endif

// WiFi Client, NTP time management and SSL libraries
#include <WiFi.h>
#include <time.h>
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

// i2c bus pins
#define I2C_SDA_PIN 21
#define I2C_SCL_PIN 22

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

// Analog pin 32 (Ozone sensor raw adc data) to get semi-random data from for SSL
// Pick a pin that's not connected or attached to a randomish voltage source
const int rand_pin = 32;

// Initialize the SSL client library
// We input a WiFi Client, our trust anchors, and the analog pin
WiFiClient base_client;
SSLClient client(base_client, TAs, (size_t)TAs_NUM, rand_pin, 1, SSLClient::SSL_ERROR);

// Hardware UART definitions. Modes: UART0=0(debug out); UART1=1; UART2=2
// HardwareSerial sim800Serial(1);
HardwareSerial pmsSerial(2);

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
bool datetime_ok = false;
String ssid = "";
String passw = "";
String deviceid = "";
String logpath = "";
wifi_power_t wifipow = WIFI_POWER_19_5dBm;
int avg_measurements = 30;
int avg_delay = 55;

// Sensor active variables
bool BME_run;
bool PMS_run;
bool MICS_run;
bool O3_run;

// Variables for BME680
float hum = 0.0;
float temp = 0.0;
float pre = 0.0;
float VOC = 0.0;
float sealevelalt = 122.0; // sea level altitude in meters, defaults to Milan, Italy

// Variables and structure for PMS5003
PMS::DATA data;
int PM1 = 0;
int PM10 = 0;
int PM25 = 0;

// Variables for MICS6814
float MICS_CO = 0.0;
float MICS_NO2 = 0.0;
float MICS_NH3 = 0.0;

// Variables for ZE25-O3
float ozone = 0.0;
int o3zeroval = 1489; // ozone sensor ADC zero default offset (0.4V to 1.1V range in 12bit resolution at 0dB attenuation)

// Date and time vars
String recordedAt = "";
String dayStamp = "";
String timeStamp = "";

// Var for MSP# evaluation
short MSP = -1; // set to -1 to distinguish from grey (0)

// Sending data to server was successful?
bool sent_ok = false;

// Include system functions ordered on dependencies
#include "libs/sensors.h"
#include "libs/display.h"
#include "libs/sdcard.h"
#include "libs/network.h"


//*******************************************************************************************************************************
//******************************************  S E T U P  ************************************************************************
//*******************************************************************************************************************************
void setup() {

  // INIT EEPROM, I2C, DISPLAY, SERIAL SENSORS ++++++++++++++++++++++++++++++++++++
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  u8g2.begin();
  Serial.begin(115200);
  delay(2000);// time for serial init

  // SET UNUSED PINS TO OUTPUT AND ADC ++++++++++++++++++++++++++++++++++++
  pinMode(33, OUTPUT);
  pinMode(25, OUTPUT);
  pinMode(26, OUTPUT);
  pinMode(27, OUTPUT);
  pinMode(13, OUTPUT);
  pinMode(4, OUTPUT);
  pinMode(0, OUTPUT);
  pinMode(2, OUTPUT);
  pinMode(15, OUTPUT);
  analogReadResolution(12); // ADC settings for ZE25-O3
  analogSetWidth(12);
  analogSetAttenuation((adc_attenuation_t)0);

  // BOOT STRINGS ++++++++++++++++++++++++++++++++++++++++++++++++++++++
  short buildyear = 2021;
  Serial.println("\nMILANO SMART PARK");
  Serial.println("FIRMWARE " + ver);
  Serial.printf("by Norman Mulinacci, %d\n\n", buildyear);
#ifdef VERSION_STRING
  log_d("ver was defined at compile time.\n");
#else
  log_d("ver is the default.\n");
#endif
#ifdef API_SECRET_SALT
  log_d("api_secret_salt was defined at compile time.\n");
#else
  log_d("api_secret_salt is the default.\n");
#endif
  drawBoot(&ver, buildyear);
  //+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

  // SD CARD INIT, CHECK AND PARSE CONFIGURATION ++++++++++++++++++++++++++++++++++++++++++++++++++
  Serial.println("Initializing SD Card...\n");
  drawTwoLines(23, "Initializing", 35, "SD Card...", 0);
  SD_ok = initializeSD();
  if (SD_ok) {
    Serial.println("Reading configuration...\n");
    cfg_ok = checkConfig("/config_v3.txt");
    if (!server_ok) {
      log_e("No server URL defined. Can't upload data!\n");
      drawTwoLines(20, "No URL defined!", 35, "No upload!", 6);
    }
    if (avg_delay < 50) {
      log_e("AVG_DELAY should be at least 50 seconds! Setting to 50...\n");
      drawTwoLines(5, "AVG_DELAY less than 50!", 15, "Setting to 50...", 5);
      avg_delay = 50; // must be at least 45 for PMS5003 compensation routine, 5 seconds extra for reading cycle messages
    }
  }
  // +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

  // SET logpath AND CHECK LOGFILE EXISTANCE +++++++++++++++++++++++++++++++++++++
  if (SD_ok) {
    logpath = "/log_" + deviceid + "_" + ver + ".csv";
    checkLogFile();
  }
  //+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

  //+++++++++++++ GET ESP32 INFO +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  String modelrev = String(ESP.getChipModel()) + " Rev " + String(ESP.getChipRevision());
  Serial.println("ESP32 Chip model = " + modelrev);
  uint32_t chipId = 0;
  for (int i = 0; i < 17; i = i + 8) {
    chipId |= ((ESP.getEfuseMac() >> (40 - i)) & 0xff) << i;
  }
  String idstring = "ID: " + String(chipId);
  Serial.println(idstring + "\n");
  drawTwoLines(8, modelrev.c_str(), 21, idstring.c_str(), 5);
  //++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

  //++++++++++++++++ DETECT AND INIT SENSORS ++++++++++++++++++++++++++++++
  Serial.println("Detecting and initializing sensors...\n");

  // BME680 +++++++++++++++++++++++++++++++++++++
  drawScrHead();
  u8g2.drawStr(5, 35, "Detecting sensors...");
  u8g2.sendBuffer();
  bme680.begin(BME680_I2C_ADDR_SECONDARY, Wire);
  if (checkBMESensor()) {
    log_i("BME680 sensor detected, initializing...\n");
    u8g2.drawStr(20, 55, "BME680 -> Ok!");
    u8g2.sendBuffer();
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
    u8g2.drawStr(20, 55, "BME680 -> Err!");
    u8g2.sendBuffer();
    BME_run = false;
  }
  //+++++++++++++++++++++++++++++++++++++++++++++

  // PMS5003 ++++++++++++++++++++++++++++++++++++
  // pmsSerial: with WROVER module don't use UART 2 mode on pins 16 and 17, it crashes!
  pmsSerial.begin(9600, SERIAL_8N1, 14, 12); // baud, type, ESP_RX, ESP_TX
  delay(1500);
  drawScrHead();
  u8g2.drawStr(5, 35, "Detecting sensors...");
  u8g2.sendBuffer();
  pms.wakeUp(); // Waking up sensor after sleep
  delay(1500);
  if (pms.readUntil(data)) {
    log_i("PMS5003 sensor detected, initializing...\n");
    u8g2.drawStr(20, 55, "PMS5003 -> Ok!");
    u8g2.sendBuffer();
    PMS_run = true;
    pms.sleep(); // Putting sensor to sleep
  } else {
    log_e("PMS5003 sensor not detected!\n");
    u8g2.drawStr(20, 55, "PMS5003 -> Err!");
    u8g2.sendBuffer();
    PMS_run = false;
  }
  delay(1500);
  //++++++++++++++++++++++++++++++++++++++++++++++

  // MICS6814 ++++++++++++++++++++++++++++++++++++
  drawScrHead();
  u8g2.drawStr(5, 35, "Detecting sensors...");
  u8g2.sendBuffer();
  if (gas.begin()) { // Connect to sensor using default I2C address (0x04)
    log_i("MICS6814 sensor detected, initializing...\n");
    u8g2.drawStr(20, 55, "MICS6814 -> Ok!");
    u8g2.sendBuffer();
    MICS_run = true;
    gas.powerOn(); // turn on heating element and led
    gas.ledOn();
    delay(1500);
    log_d("MICS6814 stored base resistance values:");
    log_d("OX: %d | RED: %d | NH3: %d\n", gas.getBaseResistance(CH_OX), gas.getBaseResistance(CH_RED), gas.getBaseResistance(CH_NH3));
    drawScrHead();
    u8g2.setCursor(2, 28); u8g2.print("MICS6814 Res0 values:");
    u8g2.setCursor(30, 39); u8g2.print("OX: " + String(gas.getBaseResistance(CH_OX)));
    u8g2.setCursor(30, 50); u8g2.print("RED: " + String(gas.getBaseResistance(CH_RED)));
    u8g2.setCursor(30, 61); u8g2.print("NH3: " + String(gas.getBaseResistance(CH_NH3)));
    u8g2.sendBuffer();
    delay(4000);
  } else {
    log_e("MICS6814 sensor not detected!\n");
    u8g2.drawStr(20, 55, "MICS6814 -> Err!");
    u8g2.sendBuffer();
    MICS_run = false;
  }
  delay(1500);
  //+++++++++++++++++++++++++++++++++++++++++++++++++++

  // ZE25-O3 ++++++++++++++++++++++++++++++++++++++++++
  drawScrHead();
  u8g2.drawStr(5, 35, "Detecting sensors...");
  u8g2.sendBuffer();
  if (!isAnalogO3Connected()) {
    log_e("ZE25-O3 sensor not detected!\n");
    u8g2.drawStr(20, 55, "ZE25-O3 -> Err!");
    u8g2.sendBuffer();
    O3_run = false;
  } else {
    log_i("ZE25-O3 sensor detected, running...\n");
    u8g2.drawStr(20, 55, "ZE25-O3 -> Ok!");
    u8g2.sendBuffer();
    O3_run = true;
  }
  delay(1500);
  //+++++++++++++++++++++++++++++++++++++++++++++++++++++

  //+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  // CONNECT TO WIFI AND GET DATE&TIME +++++++++++++++++++++++++++++++++++++++++++++++++++
  if (cfg_ok) {
    connAndGetTime();
  }
  //+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

}// end of SETUP
//*******************************************************************************************************************************
//*******************************************************************************************************************************
//*******************************************************************************************************************************



//*******************************************************************************************************************************
//********************************************  L O O P  ************************************************************************
//*******************************************************************************************************************************
void loop() {

  // DISCONNECTING AND TURNING OFF WIFI +++++++++++++++++++++++++++++++++++++
  Serial.println("Turning off WiFi...\n");
  WiFi.disconnect();
  WiFi.mode(WIFI_OFF);
  delay(1000); // Waiting a bit for Wifi mode set
  //++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

  // ZEROING OUT VARIABLES +++++++++++++++++++++++++++++++++++++
  connected_ok = false;
  datetime_ok = false;
  sent_ok = false;
  recordedAt = "";
  dayStamp = "";
  timeStamp = "";
  int errcount = 0;
  short BMEfails = 0;
  temp = 0.0;
  float currtemp = 0.0; // used for compensating gas measures
  pre = 0.0;
  hum = 0.0;
  VOC = 0.0;
  short PMSfails = 0;
  PM1 = 0;
  PM25 = 0;
  PM10 = 0;
  short MICSfails = 0;
  MICS_CO = 0.0;
  MICS_NO2 = 0.0;
  MICS_NH3 = 0.0;
  short O3fails = 0;
  ozone = 0.0;
  MSP = -1; // reset to -1 to distinguish from grey (0)
  //++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++


  //------------------------------------------------------------------------
  //++++++++++++++++ READING SENSORS FOR AVERAGE ++++++++++++++++++++++++++++++


  //++++++++++++++++ MAIN MEASUREMENTS LOOP ++++++++++++++++++++++++++++++
  for (int k = 0; k < avg_measurements; k++) {

    int curdelay = 0;
    if (PMS_run) {
      curdelay = avg_delay - 45; // 45 seconds compensation for PMS5003
    } else {
      curdelay = avg_delay;
    }

    //+++++++++ NEXT MEASUREMENTS CYCLE DELAY ++++++++++++
    Serial.printf("Wait %d min. and %d sec. for measurement cycle %d of %d\n\n", curdelay / 60, curdelay % 60, k + 1, avg_measurements);
    String cyclemsg = "Cycle " + String(k + 1) + " of " + String(avg_measurements);
    drawCountdown(curdelay, 25, cyclemsg.c_str());
    //+++++++++++++++++++++++++++++++++++++++++++++++++++++

    //+++++++++ WAKE UP AND PREHEAT PMS5003 ++++++++++++
    if (PMS_run) {
      Serial.println("Waking up and preheating PMS5003 sensor for 45 seconds...\n");
      pms.wakeUp();
      drawCountdown(45, 2, "Preheating PMS5003...");
    }
    //++++++++++++++++++++++++++++++++++++++++++++++++

    //+++++++++ MEASUREMENTS MESSAGE ++++++++++++
    log_i("Measurements in progress...\n");
    drawTwoLines(25, "Measurements", 25, "in progress...", 0);
    //+++++++++++++++++++++++++++++++++++++++++++++++++++++

    //+++++++++ READING BME680 ++++++++++++
    if (BME_run) {
      float bmeread = 0.0; // temporary read var
      log_i("Sampling BME680 sensor...");
      while (1) {
        if (!checkBMESensor()) {
          if (errcount > 2) {
            log_e("Error while sampling BME680 sensor!");
            BMEfails++;
            break;
          }
          delay(1000);
          errcount++;
          continue;
        }
        if (!bme680.run()) continue;

        bmeread = bme680.temperature;
        log_v("Temperature(*C): %.3f", bmeread);
        currtemp = bmeread;
        temp += bmeread;

        bmeread = bme680.pressure / 100.0;
        log_v("Pressure(hPa): %.3f", bmeread);
        pre += bmeread;

        bmeread = bme680.humidity;
        log_v("Humidity(perc.): %.3f", bmeread);
        hum += bmeread;

        bmeread = bme680.gasResistance / 1000.0;
        log_v("Gas resistance(kOhm): %.3f\n", bmeread);
        VOC += bmeread;

        break;
      }
    }
    //++++++++++++++++++++++++++++++++++++++++++++++++++++++

    //+++++++++ READING PMS5003 +++++++++++
    if (PMS_run) {
      log_i("Sampling PMS5003 sensor...");
      errcount = 0;
      while (1) {
        while (pmsSerial.available()) {
          pmsSerial.read(); // Clears buffer (removes potentially old data) before read.
        }
        if (!pms.readUntil(data)) {
          if (errcount > 2) {
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
    //+++++++++++++++++++++++++++++++++++++++++++++++++++++++

    //+++++++++ READING MICS6814 ++++++++++++
    if (MICS_run) {
      log_i("Sampling MICS6814 sensor...");
      errcount = 0;
      while (1) {

        float micsread[3]; // array for temporary readings

        micsread[0] = gas.measureCO();
        micsread[1] = gas.measureNO2();
        micsread[2] = gas.measureNH3();

        if (micsread[0] < 0 || micsread[0] > 50 || micsread[1] < 0 || micsread[1] > 0.6 || micsread[2] < 0 || micsread[2] > 50) {
          if (errcount > 2) {
            log_e("Error while sampling MICS6814 sensor!");
            MICSfails++;
            break;
          }
          errcount++;
          delay(1000);
          continue;
        }

        if (BME_run) micsread[0] = micsread[0] + ((0.04 * (currtemp - 25.0)) * micsread[0]); // compensation based on the current measured temperature
        log_v("CO(ppm): %.3f", micsread[0]);
        MICS_CO += micsread[0];

        log_v("NOx(ppm): %.3f", micsread[1]);
        MICS_NO2 += micsread[1];

        log_v("NH3(ppm): %.3f\n", micsread[2]);
        MICS_NH3 += micsread[2];

        break;
      }
    }
    //+++++++++++++++++++++++++++++++++++++++++++++++++++++

    //+++++++++ READING ZE25-O3 ++++++++++++
    if (O3_run) {
      float o3read = 0.0; // temporary read var
      log_i("Sampling ZE25-O3 sensor...");
      errcount = 0;
      while (1) {
        if (!isAnalogO3Connected()) {
          if (errcount > 2) {
            log_e("Error while sampling ZE25-O3 sensor!");
            O3fails++;
            break;
          }
          delay(1000);
          errcount++;
          continue;
        }
        o3read = analogUgM3O3Read(&currtemp); // based on the current measured temperature
        log_v("O3(ug/m3): %.3f", o3read);
        ozone += o3read;

        break;
      }
      Serial.println();
    }
    //+++++++++++++++++++++++++++++++++++++++++++++++++++++

    //+++++++++ PUT PMS5003 TO SLEEP ++++++++++++
    if (PMS_run) {
      log_i("Putting PMS5003 sensor to sleep...\n");
      pms.sleep();
      delay(1500);
    }
    //++++++++++++++++++++++++++++++++++++++++++++

  }
  //+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  //------------------------------------------------------------------------


  //+++++++++++ PERFORMING AVERAGES AND POST MEASUREMENTS TASKS ++++++++++++++++++++++++++++++++++++++++++

  log_i("Performing averages and converting to ug/m3...\n");

  short runs = avg_measurements - BMEfails;
  if (BME_run && runs > 0) {
    temp /= runs;
    pre /= runs;
    // Normalizing pressure based on sea level altitude and current temperature
    pre = (pre * pow(1 - (0.0065 * sealevelalt / (temp + 0.0065 * sealevelalt + 273.15)), -5.257));
    hum /= runs;
    VOC /= runs;
  } else if (BME_run) {
    temp = -1;
    pre = -1;
    hum = -1;
    VOC = -1;
  }

  runs = avg_measurements - PMSfails;
  if (PMS_run && runs > 0) {
    float b = 0.0;
    b = PM1 / runs;
    if (b - int(b) >= 0.5) {
      PM1 = int(b) + 1;
    } else {
      PM1 = int(b);
    }
    b = PM25 / runs;
    if (b - int(b) >= 0.5) {
      PM25 = int(b) + 1;
    } else {
      PM25 = int(b);
    }
    b = PM10 / runs;
    if (b - int(b) >= 0.5) {
      PM10 = int(b) + 1;
    } else {
      PM10 = int(b);
    }
  } else if (PMS_run) {
    PM1 = -1;
    PM25 = -1;
    PM10 = -1;
  }

  runs = avg_measurements - MICSfails;
  if (MICS_run && runs > 0) {
    MICS_CO /= runs;
    MICS_CO = convertPpmToUgM3(MICS_CO, 28.01);
    MICS_NO2 /= runs;
    MICS_NO2 = convertPpmToUgM3(MICS_NO2, 46.01);
    MICS_NH3 /= runs;
    MICS_NH3 = convertPpmToUgM3(MICS_NH3, 17.03);
  } else if (MICS_run) {
    MICS_CO = -1;
    MICS_NO2 = -1;
    MICS_NH3 = -1;
  }

  runs = avg_measurements - O3fails;
  if (O3_run && runs > 0) {
    ozone /= runs;
  } else if (O3_run) {
    ozone = -1;
  }

  // MSP# Index evaluation
  MSP = evaluateMSPIndex(PM25, MICS_NO2, ozone); // implicitly casting PM25 as float for MSP evaluation only

  //++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

  //+++++++++++ RECONNECTING AND UPDATING DATE/TIME +++++++++++++++++++++
  if (cfg_ok) {
    connAndGetTime();
  }
  //++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

  //+++++++++++++ SERIAL LOGGING  +++++++++++++++++++++++
  Serial.println("Measurements log:\n");
  Serial.println("Date&time: " + dayStamp + " " + timeStamp + "\n");
  if (BME_run) {
    Serial.println("Temperature: " + floatToComma(temp) + "°C");
    Serial.println("Humidity: " + floatToComma(hum) + "%");
    Serial.println("Pressure: " + floatToComma(pre) + "hPa");
    Serial.println("VOC: " + floatToComma(VOC) + "kOhm");
  }
  if (PMS_run) {
    Serial.println("PM10: " + String(PM10) + "ug/m3");
    Serial.println("PM2,5: " + String(PM25) + "ug/m3");
    Serial.println("PM1: " + String(PM1) + "ug/m3");
  }
  if (O3_run) {
    Serial.println("O3: " + floatToComma(ozone) + "ug/m3");
  }
  if (MICS_run) {
    Serial.println("NOx: " + floatToComma(MICS_NO2) + "ug/m3");
    Serial.println("CO: " + floatToComma(MICS_CO) + "ug/m3");
    Serial.println("NH3: " + floatToComma(MICS_NH3) + "ug/m3");
  }
  Serial.println();
  //++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

  //+++++++++++  UPDATING DISPLAY  ++++++++++++++++++++++++++++++++++++++++++
  drawMeasurements();
  //++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++


  //++++++  UPDATING SERVER VIA HTTPS ++++++++++++++++++++++++++++++++++++++++++++

  if (server_ok && connected_ok && datetime_ok) {

    auto start = millis(); // time for connection

    Serial.println("Uploading data to server through HTTPS in progress...\n");
    drawTwoLines(15, "Uploading data", 15, "to server...", 0);

    short retries = 0;

    while (retries < 4) {
      if (client.connect(server.c_str(), 443)) {

        auto contime = millis() - start;
        log_i("Connection to server made! Time: %d\n", contime);

        // Building the post string:

        String postStr = "";

        if (BME_run) {
          postStr += "&temp=";
          postStr += String(temp, 3);
          postStr += "&hum=";
          postStr += String(hum, 3);
          postStr += "&pre=";
          postStr += String(pre, 3);
          postStr += "&voc=";
          postStr += String(VOC, 3);
        }
        if (MICS_run) {
          postStr += "&cox=";
          postStr += String(MICS_CO, 3);
          postStr += "&nox=";
          postStr += String(MICS_NO2, 3);
          postStr += "&nh3=";
          postStr += String(MICS_NH3, 3);
        }
        if (PMS_run) {
          postStr += "&pm1=";
          postStr += String(PM1);
          postStr += "&pm25=";
          postStr += String(PM25);
          postStr += "&pm10=";
          postStr += String(PM10);
        }
        if (O3_run) {
          postStr += "&o3=";
          postStr += String(ozone, 3);
        }
        postStr += "&msp=";
        postStr += String(MSP);
        postStr += "&recordedAt=";
        postStr += recordedAt;

        // Sending client requests

        String postLine = "POST /api/v1/records HTTP/1.1\nHost: " + server + "\nAuthorization: Bearer " + api_secret_salt + ":" + deviceid + "\n";
        postLine += "Connection: close\nUser-Agent: MilanoSmartPark\nContent-Type: application/x-www-form-urlencoded\nContent-Length: " + String(postStr.length());

        log_d("Post line:\n%s\n", postLine.c_str());
        log_d("Post string: %s\n", postStr.c_str());

        client.print(postLine + "\n\n" + postStr);
        client.flush();

        // Get answer from server

        start = millis();
        String answLine = "";
        while (client.available()) {
          char c = client.read();
          answLine += c;
          auto timeout = millis() - start;
          if (timeout > 10000) break;
        }

        client.stop(); // Stopping the client

        // Check server answer

        if (answLine.startsWith("HTTP/1.1 201 Created", 0)) {
          log_i("Server answer ok! Data uploaded successfully!\n");
          drawTwoLines(25, "Data uploaded", 27, "successfully!", 2);
          sent_ok = true;
        } else {
          log_e("Server answered with an error! Data not uploaded!\n");
          log_e("The full answer is:\n%s\n", answLine.c_str());
          drawTwoLines(25, "Serv answ error!", 25, "Data not sent!", 10);
        }

        break; // exit

      } else {

        log_e("Error while connecting to server!");

        String mesg = "";
        if (retries == 3) {
          log_e("Data not uploaded!\n");
          mesg = "Data not sent!";
        } else {
          log_i("Trying again, %d retries left...\n", 3 - retries);
          mesg = String(3 - retries) + " retries left...";
        }
        drawTwoLines(25, "Serv conn error!", 20, mesg.c_str(), 10);

        retries++;

      }
    }
  }

  //++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++


  //+++++++++++++ SD CARD LOGGING  ++++++++++++++++++++++++++++++++++++++++++
  if (SD_ok) {
    logToSD();
  }
  //+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

}// end of LOOP
