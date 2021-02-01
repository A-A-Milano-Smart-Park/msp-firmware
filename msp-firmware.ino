/*
      Milano Smart Park project
      Firmware by Norman Mulinacci
*/

//Basic system libraries
#include <Arduino.h>
#include <FS.h>
#include <SD.h>
#include <Wire.h>

#ifdef VERSION_STRING
String ver = VERSION_STRING;
#else
String ver = "2.5beta"; //current firmware version
#endif


//++++++++++++++++++++++ DEBUG enable ++++++++++++++++++++++++++++++++
bool DEBDUG = false;
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

// WiFi Client, NTP time management and SSL libraries
#include <WiFi.h>
#include <time.h>
#include <SSLClient.h>
#include "api_smrtprk_ta.h" //Server Trust Anchor

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

// Server name for data upload
const char server[] = "fcub.fluidware.it";

// Analog pin 32 (Ozone sensor data) to get semi-random data from for SSL
// Pick a pin that's not connected or attached to a randomish voltage source
const int rand_pin = 32;

// Initialize the SSL client library
// We input a WiFi Client, our trust anchors, and the analog pin
WiFiClient base_client;
SSLClient client(base_client, TAs, (size_t)TAs_NUM, rand_pin, 1, SSLClient::SSL_ERROR);


// Software UART definitions
// modes (UART0=0; UART1=1; UART2=2)
// HardwareSerial sim800Serial(1);
HardwareSerial pmsSerial(2);


// BME680, PMS5003 and MICS6814 sensors instances
Bsec iaqSensor;
PMS pms(pmsSerial);
MiCS6814 gas;


// Instance for the OLED 1.3" display with the SH1106 controller
U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE, I2C_SCL_PIN, I2C_SDA_PIN);   // ESP32 Thing, HW I2C with pin remapping


// Network and system setup variables
bool SD_ok;
bool cfg_ok;
bool ssid_ok;
bool connected_ok;
bool dataora_ok;
String ssid = "";
String pwd = "";
String codice = "";
String logpath = "";
wifi_power_t wifipow;
int attesa = 0;
int avg_measurements = 0;
int avg_delay = 0;

// Variables for BME680
float hum = 0.0;
float temp = 0.0;
float pre = 0.0;
float VOC = 0.0;
float sealevelalt = 0.0;

// Variables and structure for PMS5003
PMS::DATA data;
int PM1 = 0;
int PM10 = 0;
int PM25 = 0;

// Variables for MICS6814
float MICS6814_NH3   = 0.0;
float MICS6814_CO   = 0.0;
float MICS6814_NO2   = 0.0;
float MICS6814_C3H8  = 0.0;
float MICS6814_C4H10 = 0.0;
float MICS6814_CH4   = 0.0;
float MICS6814_H2    = 0.0;
float MICS6814_C2H5OH = 0.0;

// Variables for ZE25-O3
float ozone = 0.0;

// Server time management
String dayStamp = "";
String timeStamp = "";

// Sensor active variables
bool BME_run;
bool PMS_run;
bool MICS6814_run;
bool O3_run;

//String to store the MAC Address
String macAdr = "";

// Including system icons
#include "system_icons.h"

// Including system functions
#include "system_functions.h"


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

  // FIRST SCREEN ++++++++++++++++++++++++++++++++++++++++++++++++++++++
  // serial hello
  Serial.println(); Serial.println("CENTRALINA MILANO SMART PARK");
  Serial.print("FIRMWARE v"); Serial.println(ver);
  Serial.println("by Norman Mulinacci, 2021"); Serial.println();
  // display hello
  u8g2.firstPage();
  u8g2.clearBuffer();
  u8g2.drawXBM(0, 0, 64, 64, msp_icon64x64);
  u8g2.setFont(u8g2_font_6x13_tf);
  u8g2.drawStr(74, 10, "Milano"); u8g2.drawStr(74, 23, "Smart"); u8g2.drawStr(74, 36, "Park");
  u8g2.setCursor(74, 62); u8g2.print("v" + ver);
  u8g2.sendBuffer();
  delay(5000);
  u8g2.clearBuffer();
  u8g2.drawXBM(0, 0, 64, 64, msp_icon64x64);
  u8g2.setFont(u8g2_font_6x13_tf);
  u8g2.drawStr(74, 23, "by"); u8g2.drawStr(74, 36, "Norman M."); u8g2.drawStr(74, 49, "2021");
  u8g2.sendBuffer();
  delay(2000);
  //+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++


  // SD CARD INIT AND READING CONFIG ++++++++++++++++++++++++++++++++++++++++++++++++++
  SD_ok = initializeSD();
  Serial.println();
  delay(100);
  if (SD_ok == true) {
    File cfgfile;
    if (SD.exists("/config_v2.cfg")) {
      cfgfile = SD.open("/config_v2.cfg", FILE_READ);// open read only
      Serial.println("File di configurazione aperto.\n");
      cfg_ok = parseConfig(cfgfile);
      if (cfg_ok) {
        Serial.println("File di configurazione ok!");
      } else {
        Serial.println("Errore durante la lettura del file di configurazione!");
        drawScrHead();
        u8g2.drawStr(5, 35, "ERRORE LETTURA FILE!");
        u8g2.drawStr(25, 55, "..NO WEB..");
        u8g2.sendBuffer();
      }
    } else {
      Serial.println("Nessun file di configurazione trovato!");
      drawScrHead();
      u8g2.drawStr(5, 35, "NESSUN CFG TROVATO!");
      u8g2.drawStr(25, 55, "..NO WEB..");
      u8g2.sendBuffer();
      Serial.println("Creo un file configurazione di base...");
      cfgfile = SD.open("/config_v2.cfg", FILE_WRITE);// open r/w
      if (cfgfile) {
        cfgfile.close();
        appendFile(SD, "/config_v2.cfg", "#ssid=;\n#password=;\n#codice=;\n#potenza_wifi=19.5dBm;\n#attesa(minuti)=20;\n#numero_misurazioni_media=5;\n#ritardo_media(secondi)=6;\n#attiva_MQ7=0;\n#abilita_IAQ=1;\n#altitudine_s.l.m.=122.0;\n\n//altitudine_s.l.m. influenza la misura della pressione atmosferica e va adattata a seconda della zona. 122m è l'altitudine di Milano\n//Valori possibili per potenza_wifi: -1, 2, 5, 7, 8.5, 11, 13, 15, 17, 18.5, 19, 19.5 dBm");
        Serial.println("File creato!");
        Serial.println();
        cfgfile = SD.open("/config_v2.cfg", FILE_READ);
        cfg_ok = parseConfig(cfgfile);
      } else {
        Serial.println("Errore nel creare il file cfg!");
        Serial.println();
        cfg_ok = false;
        setDefaults();
      }
    }
  } else {
    Serial.println("Errore: nessuna scheda SD inserita!");
    drawScrHead();
    u8g2.drawStr(5, 35, "NESSUNA SCHEDA SD!");
    u8g2.drawStr(25, 55, "..NO WEB..");
    u8g2.sendBuffer();
    SD_ok = false;
    cfg_ok = false;
    setDefaults();
  }
  Serial.println();
  delay(2000);
  // +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++


  //+++++++++++++ GET MAC ADDRESS ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  macAdr = WiFi.macAddress();
  Serial.println("Indirizzo MAC della centralina: " + macAdr + "\n");
  drawScrHead();
  u8g2.setCursor(28, 35); u8g2.print("MAC ADDRESS:");
  u8g2.setCursor(12, 55); u8g2.print(macAdr);
  u8g2.sendBuffer();
  delay(6000);
  //+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++


  // CHECK LOGFILE EXISTANCE +++++++++++++++++++++++++++++++++++++
  if (SD_ok == true) {
    logpath = "/LOG_N_" + codice + "_v" + ver + ".csv";
    if (!SD.exists(logpath)) {
      Serial.println("File di log non presente, lo creo...");
      File filecsv = SD.open(logpath, FILE_WRITE);
      if (filecsv) {
        filecsv.close();
        String headertext = "File di log della centralina: " + codice + " | Versione firmware: " + ver + " | MAC: " + macAdr;
        appendFile(SD, logpath, headertext);
        appendFile(SD, logpath, "Data;Ora;Temp(*C);Hum(%);Pre(hPa);PM10(ug/m3);PM2,5(ug/m3);PM1(ug/m3);NOx(ppm);CO(ppm);O3(ppm);VOC(kOhm);NH3(ppm);C3H8(ppm);C4H10(ppm);CH4(ppm);H2(ppm);C2H5OH(ppm)");
        Serial.println("File di log creato!");
        Serial.println();
      } else {
        Serial.println("Errore nel creare il file di log!");
        Serial.println();
      }
    } else {
      Serial.println("File di log presente!");
      Serial.println();
    }
  }
  //+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++


  // CHECK SECOND LOGFILE (for UG/M3) +++++++++++++++++++++++++++++++++++++
  if (SD_ok == true) {
    logpath = "/LOG_N_CONV_" + codice + "_v" + ver + ".csv";
    if (!SD.exists(logpath)) {
      Serial.println("File di log 2 non presente, lo creo...");
      File filecsv = SD.open(logpath, FILE_WRITE);
      if (filecsv) {
        filecsv.close();
        String headertext = "File di log 2 della centralina: " + codice + " | Versione firmware: " + ver + " | MAC: " + macAdr;
        appendFile(SD, logpath, headertext);
        appendFile(SD, logpath, "Data;Ora;Temp(*C);Hum(%);Pre(hPa);PM10(ug/m3);PM2,5(ug/m3);PM1(ug/m3);NOx(ug/m3);CO(ug/m3);O3(ug/m3);VOC(kOhm);NH3(ug/m3);C3H8(ug/m3);C4H10(ug/m3);CH4(ug/m3);H2(ug/m3);C2H5OH(ug/m3)");
        Serial.println("File di log 2 creato!");
        Serial.println();
      } else {
        Serial.println("Errore nel creare il file di log 2!");
        Serial.println();
      }
    } else {
      Serial.println("File di log 2 presente!");
      Serial.println();
    }
  }
  //+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++


  // +++++++ DETECT AND INIT SENSORS +++++++++++++++

  Serial.println("Rilevamento sensori in corso...\n");


  // BME680 ++++++++++++++++++++++++++++++++++++++++++
  drawScrHead();
  u8g2.drawStr(8, 35, "Rilevo i sensori...");
  u8g2.sendBuffer();
  iaqSensor.begin(BME680_I2C_ADDR_SECONDARY, Wire);
  if (CheckSensor()) {
    Serial.println("Sensore BME680 rilevato, inizializzo...");
    u8g2.drawStr(20, 55, "BME680 -> OK!");
    u8g2.sendBuffer();
    //iaqSensor.setConfig(bsec_config_iaq);
    //loadState();
    bsec_virtual_sensor_t sensor_list[] = {
      BSEC_OUTPUT_RAW_TEMPERATURE,
      BSEC_OUTPUT_RAW_PRESSURE,
      BSEC_OUTPUT_RAW_HUMIDITY,
      BSEC_OUTPUT_RAW_GAS,
      BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_TEMPERATURE,
      BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_HUMIDITY,
    };
    iaqSensor.updateSubscription(sensor_list, sizeof(sensor_list) / sizeof(sensor_list[0]), BSEC_SAMPLE_RATE_LP);
    BME_run = true;
    Serial.println();
  } else {
    Serial.println("Sensore BME680 non rilevato.\n");
    u8g2.drawStr(20, 55, "BME680 -> ERR!");
    u8g2.sendBuffer();
    BME_run = false;
  }
  //++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++


  // PMS5003 ++++++++++++++++++++++++++++++++++++++++++
  // pmsSerial: with WROVER module don't use UART 2 mode on pins 16 and 17, it crashes!
  pmsSerial.begin(9600, SERIAL_8N1, 14, 12); // baud, type, ESP_RX, ESP_TX
  delay(1500);
  drawScrHead();
  u8g2.drawStr(8, 35, "Rilevo i sensori...");
  u8g2.sendBuffer();
  pms.wakeUp(); // Waking up sensor after sleep
  delay(1500);
  if (pms.readUntil(data)) {
    Serial.println("Sensore PMS5003 rilevato, inizializzo...\n");
    u8g2.drawStr(20, 55, "PMS5003 -> OK!");
    u8g2.sendBuffer();
    PMS_run = true;
    pms.sleep(); // Putting sensor to sleep
  } else {
    Serial.println("Sensore PMS5003 non rilevato.\n");
    u8g2.drawStr(20, 55, "PMS5003 -> ERR!");
    u8g2.sendBuffer();
    PMS_run = false;
  }
  delay(1500);
  //++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++


  // MICS6814 ++++++++++++++++++++++++++++++++++++
  drawScrHead();
  u8g2.drawStr(8, 35, "Rilevo i sensori...");
  u8g2.sendBuffer();
  if (gas.begin()) { // Connect to sensor using default I2C address (0x04)
    Serial.println("Sensore MICS6814 rilevato, inizializzo...\n");
    u8g2.drawStr(20, 55, "MICS6814 -> OK!");
    u8g2.sendBuffer();
    MICS6814_run = true;
    // turn on heating element and led
    gas.powerOn();
    gas.ledOn();
    if (DEBDUG) {
      Serial.println("\nValori delle resistenze di base del MICS6814:");
      Serial.print("OX: "); Serial.print(gas.getBaseResistance(CH_OX));
      Serial.print(" ; RED: "); Serial.print(gas.getBaseResistance(CH_RED));
      Serial.print(" ; NH3: "); Serial.println(gas.getBaseResistance(CH_NH3));
    }
  } else {
    Serial.println("Sensore MICS6814 non rilevato.\n");
    u8g2.drawStr(20, 55, "MICS6814 -> ERR!");
    u8g2.sendBuffer();
    MICS6814_run = false;
  }
  delay(1500);
  //++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++


  // ZE25-O3 ++++++++++++++++++++++++++++++++++++++++++
  drawScrHead();
  u8g2.drawStr(8, 35, "Rilevo i sensori...");
  u8g2.sendBuffer();
  if (!isAnalogO3Connected()) {
    Serial.println("Sensore ZE25-O3 non rilevato.\n");
    u8g2.drawStr(20, 55, "ZE25-O3 -> ERR!");
    u8g2.sendBuffer();
    O3_run = false;
  } else {
    Serial.println("Sensore ZE25-O3 rilevato, inizializzo...\n");
    u8g2.drawStr(20, 55, "ZE25-O3 -> OK!");
    u8g2.sendBuffer();
    O3_run = true;
  }
  delay(1500);
  //++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++


  // CONNECT TO WIFI AND GET DATE&TIME +++++++++++++++++++++++++++++++++++++++++++++++++++
  initWifi();
  //+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

  Serial.println();

}// end of SETUP
//*******************************************************************************************************************************
//*******************************************************************************************************************************
//*******************************************************************************************************************************





//*******************************************************************************************************************************
//********************************************  L O O P  ************************************************************************
//*******************************************************************************************************************************
void loop() {

  // DISCONNECTING AND TURNING OFF WIFI +++++++++++++++++++++++++++++++++++++
  //------------------------------------------------------------------------
  Serial.println("Spengo il WiFi...\n");
  WiFi.disconnect();
  WiFi.mode(WIFI_OFF);
  delay(1000); // Waiting a bit for Wifi mode set
  connected_ok = false;
  dataora_ok = false;
  u8g2.drawXBMP(52, 0, 16, 16, blank_icon16x16);
  u8g2.drawXBMP(112, 0, 16, 16, nocon_icon16x16);
  u8g2.sendBuffer();
  //------------------------------------------------------------------------
  //++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++


  if (DEBDUG) {
    Serial.print("codice: --> "); Serial.println(codice);
    Serial.print("SD_ok: --> "); Serial.println(SD_ok);
    Serial.print("cfg_ok: --> "); Serial.println(cfg_ok);
    Serial.print("connected_ok: --> "); Serial.println(connected_ok);
    Serial.print("dataora_ok: --> "); Serial.println(dataora_ok);
  }


  // WAIT BEFORE MEASURING +++++++++++++++++++++++++++++++++++++++++++
  for (int i = 60 * attesa; i > 0; i--) {
    String output = "";
    output = String(i / 60) + ":";
    if (i % 60 >= 0 && i % 60 <= 9) {
      output += "0";
    }
    output += String(i % 60);
    Serial.println("Attendi " + output + " min. per il ciclo di misure");
    drawScrHead();
    u8g2.setCursor(5, 35); u8g2.print("Attesa per misure...");
    u8g2.setCursor(8, 55); u8g2.print("ATTENDI " + output + " MIN.");
    u8g2.sendBuffer();
    delay(1000);
  }
  //+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++


  //------------------------------------------------------------------------
  //++++++++++++++++ READING SENSORS FOR AVERAGE ++++++++++++++++++++++++++++++

  // Zeroing out the variables
  temp = 0.0;
  pre = 0.0;
  hum = 0.0;
  VOC = 0.0;
  PM1 = 0.0;
  PM25 = 0.0;
  PM10 = 0.0;
  ozone = 0.0;
  MICS6814_CO = 0.0;
  MICS6814_NO2 = 0.0;
  MICS6814_NH3 = 0.0;
  MICS6814_C3H8 = 0.0;
  MICS6814_C4H10 = 0.0;
  MICS6814_CH4 = 0.0;
  MICS6814_H2 = 0.0;
  MICS6814_C2H5OH = 0.0;

  //Preheating PMS5003 sensor for accurate measurements, 30 seconds is fine, 1 minute for good measure
  //Only if avg_delay is less than 1 min.
  if (PMS_run && avg_delay < 60) {
    Serial.println("Waking up PMS5003 sensor after sleep...\n");
    pms.wakeUp();
    Serial.println("Attendi 1:00 min. per riscaldamento PMS5003\n");
    for (int i = 60; i > 0; i--) {
      String output = "";
      output = String(i / 60) + ":";
      if (i % 60 >= 0 && i % 60 <= 9) {
        output += "0";
      }
      output += String(i % 60);
      drawScrHead();
      u8g2.setCursor(5, 35); u8g2.print("Riscaldo PMS5003...");
      u8g2.setCursor(8, 55); u8g2.print("ATTENDI " + output + " MIN.");
      u8g2.sendBuffer();
      delay(1000);
    }
  }

  //++++++++++++++++ MAIN MEASUREMENTS LOOP ++++++++++++++++++++++++++++++
  for (int k = 0; k < avg_measurements; k++) {

    //+++++++++ NEXT MEASUREMENTS CYCLE DELAY ++++++++++++
    Serial.printf("Attesa di %d:%d min. per il ciclo %d di %d\n\n", avg_delay / 60, avg_delay % 60, k + 1, avg_measurements);
    for (int i = avg_delay; i > 0; i--) {
      if (i % 60 == 0 && i / 60 == 1 && PMS_run) {
        Serial.println("Waking up PMS5003 sensor after sleep...");
        pms.wakeUp();
      }
      String output = "";
      output = String(i / 60) + ":";
      if (i % 60 >= 0 && i % 60 <= 9) {
        output += "0";
      }
      output += String(i % 60);
      String cyclemsg = "Ciclo " + String(k + 1) + " di " + String(avg_measurements);
      drawScrHead();
      u8g2.setCursor(15, 35); u8g2.print(cyclemsg);
      u8g2.setCursor(8, 55); u8g2.print("ATTENDI " + output + " MIN.");
      u8g2.sendBuffer();
      delay(1000);
    }
    //+++++++++++++++++++++++++++++++++++++++++++++++++++++

    //serial
    Serial.println("Sto effettuando le misurazioni...");
    //display
    drawScrHead();
    u8g2.drawStr(15, 35, "Sto effettuando");
    u8g2.drawStr(15, 55, "le misurazioni...");
    u8g2.sendBuffer();

    int errcount = 0;

    //+++++++++ READING BME680 ++++++++++++
    if (BME_run) {
      Serial.println("Campiono BME680...");
      errcount = 0;
      while (1) {
        if (!iaqSensor.run()) {
          if (errcount > 2) {
            Serial.println("ERRORE durante misura BME680!");
            break;
          }
          delay(1000);
          errcount++;
          continue;
        }
        temp += iaqSensor.temperature;
        pre += iaqSensor.pressure / 100.0;
        hum += iaqSensor.humidity;
        VOC += iaqSensor.gasResistance / 1000.0;
        if (DEBDUG) {
          Serial.printf("Temperatura(*C): %.3f\n", iaqSensor.temperature);
          Serial.printf("Pressione(hPa): %.3f\n", iaqSensor.pressure / 100.0);
          Serial.printf("Umidità(perc.): %.3f\n", iaqSensor.humidity);
          Serial.printf("Resistenza Gas(kOhm): %.3f\n", iaqSensor.gasResistance / 1000.0);
          Serial.println();
        }
        break;
      }
    }
    //++++++++++++++++++++++++++++++++++++++++++++++++++++++

    //+++++++++ READING PMS5003 +++++++++++
    if (PMS_run) {
      Serial.println("Campiono PMS5003...");
      errcount = 0;
      while (1) {
        while (pmsSerial.available()) {
          pmsSerial.read(); // Clears buffer (removes potentially old data) before read.
        }
        if (!pms.readUntil(data)) {
          if (errcount > 2) {
            Serial.println("ERRORE durante misura PMS5003!");
            break;
          }
          delay(1000);
          errcount++;
          continue;
        }
        PM1 += data.PM_AE_UG_1_0;
        PM25 += data.PM_AE_UG_2_5;
        PM10 += data.PM_AE_UG_10_0;
        if (DEBDUG) {
          Serial.printf("PM1(ug/m3): %d\n", data.PM_AE_UG_1_0);
          Serial.printf("PM2,5(ug/m3): %d\n", data.PM_AE_UG_2_5);
          Serial.printf("PM10(ug/m3): %d\n", data.PM_AE_UG_10_0);
          Serial.println();
        }
        break;
      }
    }
    //+++++++++++++++++++++++++++++++++++++++++++++++++++++++

    //+++++++++ READING MICS6814 ++++++++++++
    if (MICS6814_run) {
      Serial.println("Campiono MICS6814...");
      errcount = 0;
      while (1) {
        if (gas.measureCO() < 0 || gas.measureNO2() < 0 || gas.measureNH3() < 0) {
          if (errcount > 2) {
            Serial.println("ERRORE durante misura MICS6814!");
            break;
          }
          delay(1000);
          errcount++;
          continue;
        }
        MICS6814_CO += gas.measureCO();
        MICS6814_NO2 += gas.measureNO2();
        MICS6814_NH3 += gas.measureNH3();
        MICS6814_C3H8 += gas.measureC3H8();
        MICS6814_C4H10 += gas.measureC4H10();
        MICS6814_CH4 += gas.measureCH4();
        MICS6814_H2 += gas.measureH2();
        MICS6814_C2H5OH += gas.measureC2H5OH();
        if (DEBDUG) {
          Serial.printf("CO(ppm): %.3f\n", gas.measureCO());
          Serial.printf("NO2(ppm): %.3f\n", gas.measureNO2());
          Serial.printf("NH3(ppm): %.3f\n", gas.measureNH3());
          Serial.printf("C3H8(ppm): %.3f\n", gas.measureC3H8());
          Serial.printf("C4H10(ppm): %.3f\n", gas.measureC4H10());
          Serial.printf("CH4(ppm): %.3f\n", gas.measureCH4());
          Serial.printf("H2(ppm): %.3f\n", gas.measureH2());
          Serial.printf("C2H5OH(ppm): %.3f\n", gas.measureC2H5OH());
          Serial.println();
        }
        break;
      }
    }
    //+++++++++++++++++++++++++++++++++++++++++++++++++++++

    //+++++++++ READING ZE25-O3 ++++++++++++
    if (O3_run) {
      Serial.println("Campiono ZE25-O3...");
      errcount = 0;
      int punti = 0;
      while (1) {
        if (isAnalogO3Connected()) {
          if (errcount > 2) {
            Serial.println("ERRORE durante misura ZE25-O3!");
            break;
          }
          delay(1000);
          errcount++;
          continue;
        }
        ozone += analogPpmO3Read(&punti);
        break;
      }
    }
    //+++++++++++++++++++++++++++++++++++++++++++++++++++++


    if (PMS_run && avg_delay >= 60) {
      Serial.println("Putting PMS5003 sensor to sleep...\n");
      pms.sleep();
      delay(1500);
    }

  }
  //+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  //------------------------------------------------------------------------



  //------------------------------------------------------------------------
  //+++++++++++ PERFORMING AVERAGES AND POST MEASUREMENTS TASKS ++++++++++++++++++++++++++++++++++++++++++
  
  //Only if avg_delay is less than 1 min.
  if (PMS_run && avg_delay < 60) {
    Serial.println("Putting PMS5003 sensor to sleep...\n");
    pms.sleep();
    delay(1500);
  }

  if (DEBDUG) Serial.println("Performing averages...");

  if (BME_run) {
    temp /= avg_measurements;
    pre /= avg_measurements;
    // Normalizing pressure based on sea level altitude and current temperature
    pre = (pre * pow(1 - (0.0065 * sealevelalt / (temp + 0.0065 * sealevelalt + 273.15)), -5.257));
    hum /= avg_measurements;
    VOC /= avg_measurements;
  }
  if (PMS_run) {
    float b = 0.0;
    b = PM1 / avg_measurements;
    if (b - int(b) >= 0.5) {
      PM1 = int(b) + 1;
    } else {
      PM1 = int(b);
    }
    b = PM25 / avg_measurements;
    if (b - int(b) >= 0.5) {
      PM25 = int(b) + 1;
    } else {
      PM25 = int(b);
    }
    b = PM10 / avg_measurements;
    if (b - int(b) >= 0.5) {
      PM10 = int(b) + 1;
    } else {
      PM10 = int(b);
    }
  }
  if (MICS6814_run) {
    MICS6814_CO /= avg_measurements;
    MICS6814_NO2 /= avg_measurements;
    MICS6814_NH3 /= avg_measurements;
    MICS6814_C3H8 /= avg_measurements;
    MICS6814_C4H10 /= avg_measurements;
    MICS6814_CH4 /= avg_measurements;
    MICS6814_H2 /= avg_measurements;
    MICS6814_C2H5OH /= avg_measurements;
  }
  if (O3_run) {
    ozone /= avg_measurements;
  }

  //++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  //------------------------------------------------------------------------


  //------------------------------------------------------------------------
  //+++++++++++++ SERIAL LOGGING  +++++++++++++++++++++++
  if (BME_run) {
    Serial.println("Temperatura: " + floatToComma(temp) + "°C");
    Serial.println("Umidita': " + floatToComma(hum) + "%");
    Serial.println("Pressione: " + floatToComma(pre) + "hPa");
    Serial.println("VOC: " + floatToComma(VOC) + "kOhm");
  }
  if (PMS_run) {
    Serial.println("PM10: " + String(PM10) + "ug/m3");
    Serial.println("PM2,5: " + String(PM25) + "ug/m3");
    Serial.println("PM1: " + String(PM1) + "ug/m3");
  }
  if (MICS6814_run) {
    Serial.println("NOx: " + floatToComma(MICS6814_NO2) + "ppm");
    Serial.println("CO: " + floatToComma(MICS6814_CO) + "ppm");
    Serial.println("NH3: " + floatToComma(MICS6814_NH3) + "ppm");
    Serial.println("C3H8: " + floatToComma(MICS6814_C3H8) + "ppm");
    Serial.println("C4H10: " + floatToComma(MICS6814_C4H10) + "ppm");
    Serial.println("CH4: " + floatToComma(MICS6814_CH4) + "ppm");
    Serial.println("H2: " + floatToComma(MICS6814_H2) + "ppm");
    Serial.println("C2H5OH: " + floatToComma(MICS6814_C2H5OH) + "ppm");
  }
  if (O3_run) {
    Serial.println("O3: " + floatToComma(ozone) + "ppm");
  }
  Serial.println();
  //++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  //------------------------------------------------------------------------


  //------------------------------------------------------------------------
  //+++++++++++  UPDATING DISPLAY  ++++++++++++++++++++++++++++++++++++++++++
  displayMeasures();
  //++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  //------------------------------------------------------------------------


  //------------------------------------------------------------------------
  //+++++++++++ RECONNECTING AND UPDATING DATE/TIME +++++++++++++++++++++
  if (cfg_ok) {
    if (DEBDUG) Serial.println("Reconnecting and updating date/time...\n");
    initWifi();
  }
  //++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  //------------------------------------------------------------------------


  //------------------------------------------------------------------------
  //++++++  UPDATING SERVER VIA HTTPS ++++++++++++++++++++++++++++++++++++++++++++
  if (connected_ok && dataora_ok) { // in only if date and time are ok

    // time for connection:
    auto start = millis();

    Serial.println("Avvio dell'upload dei dati sul server " + String(server) + " tramite HTTPS in corso...\n");

    drawScrHead();
    u8g2.setCursor(15, 35); u8g2.print("Invio i dati");
    u8g2.setCursor(15, 55); u8g2.print("al server...");
    u8g2.sendBuffer();

    if (client.connect(server, 443)) {

      auto contime = millis() - start;
      Serial.println("Connessione al server effettuata! Tempo: " + String(contime) + "\n");

      // Building the post string:

      String postStr = "apikey=" + codice;

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
      if (MICS6814_run) {
        postStr += "&cox=";
        postStr += String(MICS6814_CO, 3);
        postStr += "&nox=";
        postStr += String(MICS6814_NO2, 3);
        postStr += "&nh3=";
        postStr += String(MICS6814_NH3, 3);
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

      if (DEBDUG) Serial.println("POST STRING: " + postStr + "\n");

      // Sending client requests

      client.print("POST /api/v1/records HTTP/1.1\r\n");
      client.print("Host: ");
      client.print(server);
      client.print("\r\n");
      client.print("Authorization: Bearer ");
      client.print(api_secret_salt);
      client.print("\r\n");
      client.print("Connection: close\r\n");
      client.print("User-Agent: MilanoSmartPark\r\n");
      client.print("Content-Type: application/x-www-form-urlencoded\r\n");
      client.print("Content-Length: ");
      client.print(postStr.length());
      client.print("\r\n\r\n");
      client.print(postStr);
      client.flush();

      Serial.println("Risposta del server:\n");

      start = millis();

      while (client.available()) {
        Serial.write(client.read());
        auto timeout = millis() - start;
        if (timeout > 10000) break;
      }

      client.stop();

      Serial.println("Dati al server inviati con successo!\n");

      drawScrHead();
      u8g2.setCursor(17, 35); u8g2.print("Dati inviati");
      u8g2.setCursor(15, 55); u8g2.print("con successo!");
      u8g2.sendBuffer();

      postStr = "";

    } else {

      Serial.println("ERRORE durante la connessione al server. Invio non effettuato!\n");

      drawScrHead();
      u8g2.setCursor(15, 35); u8g2.print("ERRORE durante");
      u8g2.setCursor(12, 55); u8g2.print("l'invio dei dati!");
      u8g2.sendBuffer();

    }

  }
  //++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  //------------------------------------------------------------------------


  //------------------------------------------------------------------------
  //+++++++++++++ SD CARD LOGGING  ++++++++++++++++++++++++++++++++++++++++++

  if (SD_ok) {
    if (DEBDUG) Serial.println("...log su scheda SD...");
    logpath = "/LOG_N_" + codice + "_v" + ver + ".csv";
    //"Data;Ora;Temp(*C);Hum(%);Pre(hPa);PM10(ug/m3);PM2,5(ug/m3);PM1(ug/m3);NOx(ppm);CO(ppm);O3(ppm);VOC(kOhm);NH3(ppm);C3H8(ppm);C4H10(ppm);CH4(ppm);H2(ppm);C2H5OH(ppm)"
    String logvalue = "";
    logvalue += dayStamp; logvalue += ";";
    logvalue += timeStamp; logvalue += ";";
    logvalue += floatToComma(temp); logvalue += ";";
    logvalue += floatToComma(hum); logvalue += ";";
    logvalue += floatToComma(pre); logvalue += ";";
    logvalue += String(PM10); logvalue += ";";
    logvalue += String(PM25); logvalue += ";";
    logvalue += String(PM1); logvalue += ";";
    logvalue += floatToComma(MICS6814_NO2); logvalue += ";";
    logvalue += floatToComma(MICS6814_CO); logvalue += ";";
    logvalue += floatToComma(ozone); logvalue += ";";
    logvalue += floatToComma(VOC); logvalue += ";";
    logvalue += floatToComma(MICS6814_NH3); logvalue += ";";
    logvalue += floatToComma(MICS6814_C3H8); logvalue += ";";
    logvalue += floatToComma(MICS6814_C4H10); logvalue += ";";
    logvalue += floatToComma(MICS6814_CH4); logvalue += ";";
    logvalue += floatToComma(MICS6814_H2); logvalue += ";";
    logvalue += floatToComma(MICS6814_C2H5OH);
    if (addToLog(SD, logpath, logvalue)) {
      Serial.println("Log su SD aggiornato con successo!\n");
    } else {
      Serial.println("ERRORE aggiornamento log su SD!\n");
    }
  }
  //+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  //------------------------------------------------------------------------


  //------------------------------------------------------------------------
  //+++++++++++++ SECOND SD CARD LOG UG/M3 ++++++++++++++++++++++++++++++++++++++++++

  if (SD_ok) {
    if (DEBDUG) Serial.println("...log 2 su scheda SD...");
    logpath = "/LOG_N_CONV_" + codice + "_v" + ver + ".csv";
    //"Data;Ora;Temp(*C);Hum(%);Pre(hPa);PM10(ug/m3);PM2,5(ug/m3);PM1(ug/m3);NOx(ug/m3);CO(ug/m3);O3(ug/m3);VOC(kOhm);NH3(ug/m3);C3H8(ug/m3);C4H10(ug/m3);CH4(ug/m3);H2(ug/m3);C2H5OH(ug/m3)"
    String logvalue = "";
    logvalue += dayStamp; logvalue += ";";
    logvalue += timeStamp; logvalue += ";";
    logvalue += floatToComma(temp); logvalue += ";";
    logvalue += floatToComma(hum); logvalue += ";";
    logvalue += floatToComma(pre); logvalue += ";";
    logvalue += String(PM10); logvalue += ";";
    logvalue += String(PM25); logvalue += ";";
    logvalue += String(PM1); logvalue += ";";
    logvalue += floatToComma(convertPpmToUgM3(MICS6814_NO2, 46.01, temp, pre)); logvalue += ";";
    logvalue += floatToComma(convertPpmToUgM3(MICS6814_CO, 28.01, temp, pre)); logvalue += ";";
    logvalue += floatToComma(convertPpmToUgM3(ozone, 48.00, temp, pre)); logvalue += ";";
    logvalue += floatToComma(VOC); logvalue += ";";
    logvalue += floatToComma(convertPpmToUgM3(MICS6814_NH3, 17.03, temp, pre)); logvalue += ";";
    logvalue += floatToComma(convertPpmToUgM3(MICS6814_C3H8, 44.10, temp, pre)); logvalue += ";";
    logvalue += floatToComma(convertPpmToUgM3(MICS6814_C4H10, 58.12, temp, pre)); logvalue += ";";
    logvalue += floatToComma(convertPpmToUgM3(MICS6814_CH4, 16.04, temp, pre)); logvalue += ";";
    logvalue += floatToComma(convertPpmToUgM3(MICS6814_H2, 2.02, temp, pre)); logvalue += ";";
    logvalue += floatToComma(convertPpmToUgM3(MICS6814_C2H5OH, 46.07, temp, pre));
    if (addToLog(SD, logpath, logvalue)) {
      Serial.println("Log 2 su SD aggiornato con successo!\n");
    } else {
      Serial.println("ERRORE aggiornamento log 2 su SD!\n");
    }
  }
  //+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  //------------------------------------------------------------------------



}// end of LOOP
