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

#ifdef VERSION_STRING
String ver = VERSION_STRING;
#else
String ver = "3.0rc3"; //current firmware version
#endif

// WiFi Client, NTP time management and SSL libraries
#include <WiFi.h>
#include <time.h>
#include <SSLClient.h>
#include "libs/trust_anchor.h" //Server Trust Anchor

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
int waittime = 0;
int avg_measurements = 6;
int avg_delay = 273;
float sealevelalt = 122.0; //default value in Milan, Italy

// Variables for BME680
float hum = 0.0;
float temp = 0.0;
float pre = 0.0;
float VOC = 0.0;

// Variables and structure for PMS5003
PMS::DATA data;
int PM1 = 0;
int PM10 = 0;
int PM25 = 0;

// Variables for MICS6814
float MICS_NH3   = 0.0;
float MICS_CO   = 0.0;
float MICS_NO2   = 0.0;
float MICS_C3H8  = 0.0;
float MICS_C4H10 = 0.0;
float MICS_CH4   = 0.0;
float MICS_H2    = 0.0;
float MICS_C2H5OH = 0.0;

// Variables for ZE25-O3
float ozone = 0.0;

// Server time management vars
String dayStamp = "";
String timeStamp = "";

// Sensor active variables
bool BME_run;
bool PMS_run;
bool MICS_run;
bool O3_run;

// String to store the MAC Address
String macAdr = "";

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

  // SET UNUSED PINS TO OUTPUT ++++++++++++++++++++++++++++++++++++
  pinMode(33, OUTPUT);
  pinMode(25, OUTPUT);
  pinMode(26, OUTPUT);
  pinMode(27, OUTPUT);
  pinMode(13, OUTPUT);
  pinMode(4, OUTPUT);
  pinMode(0, OUTPUT);
  pinMode(2, OUTPUT);
  pinMode(15, OUTPUT);

  // BOOT SCREEN ++++++++++++++++++++++++++++++++++++++++++++++++++++++
  short buildyear = 2021;
  Serial.println("\nMILANO SMART PARK");
  Serial.println("FIRMWARE v" + ver);
  Serial.printf("by Norman Mulinacci, %d\n\n", buildyear);
  drawBoot(&ver, buildyear);
  //+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

  //+++++++++++++ GET ESP32 MAC ADDRESS ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  macAdr = WiFi.macAddress();
  log_i("MAC Address: %s\n", macAdr.c_str());
  drawTwoLines(28, "MAC ADDRESS:", 12, macAdr.c_str(), 6);
  //+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

  // SD CARD INIT, CHECK AND PARSE CONFIGURATION ++++++++++++++++++++++++++++++++++++++++++++++++++
  Serial.println("Initializing SD Card...\n");
  SD_ok = initializeSD();
  if (SD_ok) {
    Serial.println("Reading configuration...\n");
    cfg_ok = checkConfig();
    if (!server_ok) {
      log_e("No server URL defined. Can't upload data!\n");
      drawTwoLines(20, "No URL defined!", 35, "No upload!", 6);
    }
  }
  // +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

  // SET logpath AND CHECK LOGFILE EXISTANCE +++++++++++++++++++++++++++++++++++++
  if (SD_ok) {
    logpath = "/log_" + deviceid + "_v" + ver + ".csv";
    checkLogFile();
  }
  //+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

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
    log_i("BME680 sensor not detected!\n");
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
    log_i("PMS5003 sensor not detected!\n");
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
    log_v("MICS6814 stored base resistance values:");
    log_v("OX: %d | RED: %d | NH3: %d\n", gas.getBaseResistance(CH_OX), gas.getBaseResistance(CH_RED), gas.getBaseResistance(CH_NH3));
  } else {
    log_i("MICS6814 sensor not detected!\n");
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
    log_i("ZE25-O3 sensor not detected!\n");
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
    Serial.println("Connecting to WiFi...\n");
    connected_ok = connectWiFi();
    if (connected_ok) {
      Serial.println("Done! Retrieving date&time from NTP server...\n");
      datetime_ok = syncNTPTime(&dayStamp, &timeStamp); // Connecting with NTP server and retrieving date&time
    }
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
  connected_ok = false;
  datetime_ok = false;
  //++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

  // WAIT BEFORE MEASURING +++++++++++++++++++++++++++++++++++++++++++
  if (waittime > 0) {
    Serial.printf("Wait %d min. to begin measuring\n\n", waittime);
    drawCountdown(waittime * 60, 5, "Pre-wait time...");
  }
  //+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++


  //------------------------------------------------------------------------
  //++++++++++++++++ READING SENSORS FOR AVERAGE ++++++++++++++++++++++++++++++

  // Zeroing out the variables
  int errcount = 0;
  short BMEfails = 0;
  temp = 0.0;
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
  MICS_C3H8 = 0.0;
  MICS_C4H10 = 0.0;
  MICS_CH4 = 0.0;
  MICS_H2 = 0.0;
  MICS_C2H5OH = 0.0;
  short O3fails = 0;
  ozone = 0.0;

  //++++++++++++++++ PREHEAT PMS5003 (IF AVG_DELAY < 60 SEC.) +++++
  if (PMS_run && avg_delay < 60) {
    log_i("Waking up PMS5003 sensor after sleep...\n");
    pms.wakeUp();
    Serial.println("Wait 1 min. for PMS5003 sensor preheat\n");
    drawCountdown(60, 2, "Preheating PMS5003..."); //30 seconds is fine, 1 minute is better
  }
  //+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

  //++++++++++++++++ MAIN MEASUREMENTS LOOP ++++++++++++++++++++++++++++++
  for (int k = 0; k < avg_measurements; k++) {

    //+++++++++ NEXT MEASUREMENTS CYCLE DELAY ++++++++++++
    Serial.printf("Wait %d min. and %d sec. for measurement cycle %d of %d\n\n", avg_delay / 60, avg_delay % 60, k + 1, avg_measurements);
    String cyclemsg = "Cycle " + String(k + 1) + " of " + String(avg_measurements);
    int cdown = avg_delay;
    if (PMS_run && avg_delay >= 60) {
      cdown -= 60;
      drawCountdown(cdown, 25, cyclemsg.c_str());
      log_i("Waking up PMS5003 sensor after sleep...");
      pms.wakeUp();
      cdown = 60;
    }
    drawCountdown(cdown, 25, cyclemsg.c_str());
    //+++++++++++++++++++++++++++++++++++++++++++++++++++++

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
      float micsread = 0.0; // temporary read var
      log_i("Sampling MICS6814 sensor...");
      errcount = 0;
      while (1) {
        if (gas.measureCO() < 0) {
          if (errcount > 2) {
            log_e("Error while sampling MICS6814 sensor!");
            MICSfails++;
            break;
          }
          delay(1000);
          errcount++;
          continue;
        }
        micsread = gas.measureCO();
        log_v("Carbon Monoxide(ppm): %.3f", micsread);
        MICS_CO += micsread;

        micsread = gas.measureNO2();
        log_v("Nitrogen Dioxide(ppm): %.3f", micsread);
        MICS_NO2 += micsread;

        micsread = gas.measureNH3();
        log_v("Ammonia(ppm): %.3f", micsread);
        MICS_NH3 += micsread;

        micsread = gas.measureC3H8();
        log_v("Propane(ppm): %.3f", micsread);
        MICS_C3H8 += micsread;

        micsread = gas.measureC4H10();
        log_v("Butane(ppm): %.3f", micsread);
        MICS_C4H10 += micsread;

        micsread = gas.measureCH4();
        log_v("Methane(ppm): %.3f", micsread);
        MICS_CH4 += micsread;

        micsread = gas.measureH2();
        log_v("Hydrogen(ppm): %.3f", micsread);
        MICS_H2 += micsread;

        micsread = gas.measureC2H5OH();
        log_v("Ethanol(ppm): %.3f\n", micsread);
        MICS_C2H5OH += micsread;

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
        o3read = analogPpmO3Read();
        log_v("Ozone(ppm): %.3f", o3read);
        ozone += o3read;

        break;
      }
      Serial.println();
    }
    //+++++++++++++++++++++++++++++++++++++++++++++++++++++

    if (PMS_run && avg_delay >= 60) {
      log_i("Putting PMS5003 sensor to sleep...\n");
      pms.sleep();
      delay(1500);
    }

  }
  //+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  //------------------------------------------------------------------------


  //+++++++++++ PERFORMING AVERAGES AND POST MEASUREMENTS TASKS ++++++++++++++++++++++++++++++++++++++++++
  if (PMS_run && avg_delay < 60) { //Only if avg_delay is less than 1 min.
    log_i("Putting PMS5003 sensor to sleep...\n");
    pms.sleep();
    delay(1500);
  }
  log_i("Performing averages and converting to ug/m3...\n");
  short runs = avg_measurements - BMEfails;
  if (BME_run && runs > 0) {
    temp /= runs;
    pre /= runs;
    // Normalizing pressure based on sea level altitude and current temperature
    pre = (pre * pow(1 - (0.0065 * sealevelalt / (temp + 0.0065 * sealevelalt + 273.15)), -5.257));
    hum /= runs;
    VOC /= runs;
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
  }
  runs = avg_measurements - MICSfails;
  if (MICS_run && runs > 0) {
    MICS_CO /= runs;
    MICS_CO = convertPpmToUgM3(MICS_CO, 28.01);
    MICS_NO2 /= runs;
    MICS_NO2 = convertPpmToUgM3(MICS_NO2, 46.01);
    MICS_NH3 /= runs;
    MICS_NH3 = convertPpmToUgM3(MICS_NH3, 17.03);
    MICS_C3H8 /= runs;
    MICS_C3H8 = convertPpmToUgM3(MICS_C3H8, 44.10);
    MICS_C4H10 /= runs;
    MICS_C4H10 = convertPpmToUgM3(MICS_C4H10, 58.12);
    MICS_CH4 /= runs;
    MICS_CH4 = convertPpmToUgM3(MICS_CH4, 16.04);
    MICS_H2 /= runs;
    MICS_H2 = convertPpmToUgM3(MICS_H2, 2.02);
    MICS_C2H5OH /= runs;
    MICS_C2H5OH = convertPpmToUgM3(MICS_C2H5OH, 46.07);
  }
  runs = avg_measurements - O3fails;
  if (O3_run && runs > 0) {
    ozone /= runs;
    ozone = convertPpmToUgM3(ozone, 48.00);
  }
  //++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

  //+++++++++++++ SERIAL LOGGING  +++++++++++++++++++++++
  Serial.println("Measurements log:\n");
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
  if (MICS_run) {
    Serial.println("Nitrogen Dioxide: " + floatToComma(MICS_NO2) + "ug/m3");
    Serial.println("Carbon Monoxide: " + floatToComma(MICS_CO) + "ug/m3");
    Serial.println("Ammonia: " + floatToComma(MICS_NH3) + "ug/m3");
    Serial.println("Propane: " + floatToComma(MICS_C3H8) + "ug/m3");
    Serial.println("Butane: " + floatToComma(MICS_C4H10) + "ug/m3");
    Serial.println("Methane: " + floatToComma(MICS_CH4) + "ug/m3");
    Serial.println("Hydrogen: " + floatToComma(MICS_H2) + "ug/m3");
    Serial.println("Ethanol: " + floatToComma(MICS_C2H5OH) + "ug/m3");
  }
  if (O3_run) {
    Serial.println("Ozone: " + floatToComma(ozone) + "ug/m3");
  }
  Serial.println();
  //++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

  //+++++++++++  UPDATING DISPLAY  ++++++++++++++++++++++++++++++++++++++++++
  drawMeasurements();
  //++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

  //+++++++++++ RECONNECTING AND UPDATING DATE/TIME +++++++++++++++++++++
  if (cfg_ok) {
    Serial.println("Reconnecting to WiFi...\n");
    connected_ok = connectWiFi();
    if (connected_ok) {
      Serial.println("Done! Updating date&time from NTP server...\n");
      datetime_ok = syncNTPTime(&dayStamp, &timeStamp); // Connecting with NTP server and retrieving date&time
    }
  }
  //++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

  //++++++  UPDATING SERVER VIA HTTPS ++++++++++++++++++++++++++++++++++++++++++++
  if (server_ok && connected_ok && datetime_ok) {
    auto start = millis(); // time for connection:
    Serial.println("Uploading data to " + server + " through HTTPS in progress...\n");
    drawTwoLines(15, "Uploading data to", 15, server.c_str(), 0);
    short retries = 0;

    while (retries < 3) {

      if (client.connect(server.c_str(), 443)) {
        auto contime = millis() - start;
        log_i("Connection to server made! Time: %d\n", contime);

        String postStr = ""; // Building the post string:
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
        postStr += "&mac=";
        postStr += macAdr;
        postStr += "&data=";
        postStr += dayStamp;
        postStr += "&ora=";
        postStr += timeStamp;
        postStr += "&recordedAt=";
        postStr += String(time(NULL));

        log_v("Post string: %s\n", postStr.c_str());

        String postLine = ""; // Sending client requests
        client.println("POST /api/v1/records HTTP/1.1");
        postLine = "Host: " + server;
        client.println(postLine);
        postLine = "Authorization: Bearer " + api_secret_salt + ":" + deviceid;
        client.println(postLine);
        client.println("Connection: close");
        client.println("User-Agent: MilanoSmartPark");
        client.println("Content-Type: application/x-www-form-urlencoded");
        postLine = "Content-Length: " + String(postStr.length());
        client.println(postLine);
        client.println();
        client.print(postStr);
        client.flush();

        log_v("Printing server answer:"); // Get answer
        start = millis();
        String answLine = "";
        while (client.available()) {
          char c = client.read();
          answLine += c;
          auto timeout = millis() - start;
          if (timeout > 10000) break;
        }
        log_v("\n\n%s\n", answLine.c_str());

        client.stop(); // Stopping the client

        log_i("Data uploaded successfully!\n"); // messages
        drawTwoLines(25, "Data uploaded", 27, "successfully!", 2);

        break; // exit
      } else {
        log_e("Error while connecting to server!");
        
        if (retries == 2) log_e("Data not uploaded!\n");
        else log_i("Trying again, %d retries left...\n", 2 - retries);
        
        String mesg = "";
        if (retries == 2) mesg = "Data not sent!";
        else mesg = String(2 - retries) + " retries left...";
        drawTwoLines(25, "Upload error!", 20, mesg.c_str(), 2);

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
