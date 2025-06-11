/*
                        Milano Smart Park Firmware
                       Developed by Norman Mulinacci

          This code is usable under the terms and conditions of the
             GNU GENERAL PUBLIC LICENSE Version 3, 29 June 2007
*/

// Sensors and Data Management Functions

#ifndef SENSORS_H
#define SENSORS_H

#include "network.h"

typedef enum __SENSOR_STATUS_ARRAY__ {
  SENS_STAT_BME680    = 0,  // BME680 sensor status
  SENS_STAT_PMS5003   = 1,    // PMS5003 sensor status
  SENS_STAT_MICS6814  = 2,   // MICS6814 sensor status
  SENS_STAT_O3        = 3,         // Ozone sensor status
  SENS_STAT_MAX       = 4
} sens_status_t;

typedef enum __MSP_ELEMENT_INDEX__ {
  MSP_INDEX_PM25  = 0,  // PM2.5 index
  MSP_INDEX_NO2   = 1,       // NO2 index
  MSP_INDEX_O3    = 2,        // O3 index
  MSP_INDEX_MAX   = 3
} msp_index_t;

//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

String floatToComma(float value) {  // converts float values in strings with the decimal part separated from the integer part by a comma

  String convert = String(value, 3);
  convert.replace(".", ",");
  return convert;
}

//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

float convertPpmToUgM3(float ppm, float mm) {  // calculates ug/m3 from a gas ppm concentration

  // mm is molar mass and must be g/mol
  // using OSHA standard conditions to perform the conversion
  float T = 25.0;
  float P = 1013.25;
  const float R = 83.1446261815324;   //gas constant (L * hPa * K^−1 * mol^−1)
  float Vm = (R * (T + 273.15)) / P;  //molar volume (L * mol^-1)
  return (ppm * 1000) * (mm / Vm);
}

//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

bool checkBMESensor() {  // checks BME680 status

  if (bme680.bsecStatus < BSEC_OK) {
    log_e("BSEC error, status %d!", bme680.bsecStatus);
    return false;
  } else if (bme680.bsecStatus > BSEC_OK) {
    log_w("BSEC warning, status %d!", bme680.bsecStatus);
  }

  if (bme680.bme68xStatus < BME68X_OK) {
    log_e("Sensor error, bme680_status %d!", bme680.bme68xStatus);
    return false;
  } else if (bme680.bme68xStatus > BME68X_OK) {
    log_w("Sensor warning, status %d!", bme680.bme68xStatus);
  }

  bme680.bsecStatus = BSEC_OK;
  return true;
}

//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

bool isAnalogO3Connected() {  // checks analog ozone sensor status

  int detect = analogRead(O3_ADC_PIN);
  pinMode(O3_ADC_PIN, INPUT_PULLDOWN);  // must invoke after every analogRead
  log_d("Detected points: %d", detect);
  if (detect == 0) return false;
  return true;
}

//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

float no2AndVocCompensation(float inputGas, float currtemp, float currpre, float currhum) {  // for NO2 and VOC gas compensations

  return (inputGas * (((currhum + 50) / 100) * compH)) + ((currtemp - 25) * compT) - ((currpre - 1013.25) * compP);
}

//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

float analogUgM3O3Read(float *intemp) {  // reads and calculates ozone ppm value from analog ozone sensor

  int points = 0;
  float T = 25.0;  // initialized at OSHA standard conditions for temperature compensation
  if (BME_run) {
    T = *intemp;  // using current measured temperature
    log_d("Current measured temperature is %.3f", T);
  }
  const short readtimes = 10;  // reading 10 times for good measure
  for (short i = 0; i < readtimes; i++) {
    int readnow = analogRead(O3_ADC_PIN);
    pinMode(O3_ADC_PIN, INPUT_PULLDOWN);  // must invoke after every analogRead
    log_v("ADC Read is: %d", readnow);
    points += readnow;
    delay(10);
  }
  points /= readtimes;
  log_d("ADC Read averaged is: %d", points);
  points -= o3zeroval;
  if (points <= 0) return 0.0;
  return ((points * 2.03552924) * 12.187 * 48) / (273.15 + T);  // temperature compensated
}

//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

void writeMicsValues() {  // write firmware calibration values into MICS6814's EEPROM

  // Store new base resistance values in EEPROM
  Wire.beginTransmission(DATA_I2C_ADDR);
  Wire.write(CMD_V2_SET_R0);
  Wire.write(MICSCal[2] >> 8);  // NH3
  Wire.write(MICSCal[2] & 0xFF);
  Wire.write(MICSCal[0] >> 8);  // RED
  Wire.write(MICSCal[0] & 0xFF);
  Wire.write(MICSCal[1] >> 8);  // OX
  Wire.write(MICSCal[1] & 0xFF);
  Wire.endTransmission();
}

//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

bool checkMicsValues() {  // check if MICS6814 internal values are the same as firmware defaults

  uint16_t redR0, oxR0, nh3R0;
  redR0 = gas.getBaseResistance(CH_RED);
  oxR0 = gas.getBaseResistance(CH_OX);
  nh3R0 = gas.getBaseResistance(CH_NH3);
  if (redR0 == MICSCal[0] && oxR0 == MICSCal[1] && nh3R0 == MICSCal[2]) {
    return true;
  }
  return false;
}

//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

void printMeasurementsOnSerial(send_data_t *data) {

  char locDate[11] = {0};
  char locTime[9] = {0};

  strftime(locDate, sizeof(locDate), "%d/%m/%Y", &data->sendTimeInfo);  // Formatting date as DD/MM/YYYY
  strftime(locTime, sizeof(locDate), "%T", &data->sendTimeInfo);        // Formatting time as HH:MM:SS

  Serial.println("Measurements log:\n");  // Log measurements to serial output
  Serial.println("Date&time: " + String(locDate) + " " + String(locTime) + "\n");
  if (BME_run) {
    Serial.println("Temperature: " + floatToComma(data->temp) + "°C");
    Serial.println("Humidity: " + floatToComma(data->hum) + "%");
    Serial.println("Pressure: " + floatToComma(data->pre) + "hPa");
    Serial.println("VOC: " + floatToComma(data->VOC) + "kOhm");
  }
  if (PMS_run) {
    Serial.println("PM10: " + String(data->PM10) + "ug/m3");
    Serial.println("PM2,5: " + String(data->PM25) + "ug/m3");
    Serial.println("PM1: " + String(data->PM1) + "ug/m3");
  }
  if (O3_run) {
    Serial.println("O3: " + floatToComma(data->ozone) + "ug/m3");
  }
  if (MICS_run) {
    Serial.println("NOx: " + floatToComma(data->MICS_NO2) + "ug/m3");
    Serial.println("CO: " + floatToComma(data->MICS_CO) + "ug/m3");
    Serial.println("NH3: " + floatToComma(data->MICS_NH3) + "ug/m3");
  }
  Serial.println();
}

//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

void performAverages(const short BMEfails, const short PMSfails, const short MICSfails, const short O3fails, bool *senserrs, const short number_of_measurements) {

  log_i("Performing averages...\n");

  short runs = number_of_measurements - BMEfails;
  if (BME_run && runs > 0) {
    temp /= runs;
    pre /= runs;
    hum /= runs;
    VOC /= runs;
  } else if (BME_run) {
    BME_run = false;
    senserrs[SENS_STAT_BME680] = true;
  }

  runs = number_of_measurements - PMSfails;
  if (PMS_run && runs > 0) {
    float b = 0.0f;
    b = PM1 / runs;
    if (b - int(b) >= 0.5f) {
      PM1 = int(b) + 1;
    } else {
      PM1 = int(b);
    }
    b = PM25 / runs;
    if (b - int(b) >= 0.5f) {
      PM25 = int(b) + 1;
    } else {
      PM25 = int(b);
    }
    b = PM10 / runs;
    if (b - int(b) >= 0.5f) {
      PM10 = int(b) + 1;
    } else {
      PM10 = int(b);
    }
  } else if (PMS_run) {
    PMS_run = false;
    senserrs[SENS_STAT_PMS5003] = true;
  }

  runs = number_of_measurements - MICSfails;
  if (MICS_run && runs > 0) {
    MICS_CO /= runs;
    MICS_NO2 /= runs;
    MICS_NH3 /= runs;
  } else if (MICS_run) {
    MICS_run = false;
    senserrs[SENS_STAT_MICS6814] = true;
  }

  runs = number_of_measurements - O3fails;
  if (O3_run && runs > 0) {
    ozone /= runs;
  } else if (O3_run) {
    O3_run = false;
    senserrs[SENS_STAT_O3] = true;
  }

}

//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

short evaluateMSPIndex(const float pm25, const float nox, const float o3) {  // evaluates the MSP# index from ug/m3 concentrations of specific gases using standard IAQ values (needs 1h averages)

  // possible returned values are: 0 -> n.d.(grey); 1 -> good(green); 2 -> acceptable(yellow); 3 -> bad(red); 4 -> really bad(black)
  log_i("Evaluating MSP# index...\n");

  short msp[MSP_INDEX_MAX] = { 0, 0, 0 };  // msp[0] is for pm2.5, msp[1] is for nox, msp[2] is for o3

  if (PMS_run) {
    if (pm25 > 50) msp[MSP_INDEX_PM25] = 4;
    else if (pm25 > 25) msp[MSP_INDEX_PM25] = 3;
    else if (pm25 > 10) msp[MSP_INDEX_PM25] = 2;
    else msp[MSP_INDEX_PM25] = 1;
  }
  if (MICS_run) {
    if (nox > 400) msp[MSP_INDEX_NO2] = 4;
    else if (nox > 200) msp[MSP_INDEX_NO2] = 3;
    else if (nox > 100) msp[MSP_INDEX_NO2] = 2;
    else msp[MSP_INDEX_NO2] = 1;
  }
  if (O3_run) {
    if (o3 > 240) msp[MSP_INDEX_O3] = 4;
    else if (o3 > 180) msp[MSP_INDEX_O3] = 3;
    else if (o3 > 120) msp[MSP_INDEX_O3] = 2;
    else msp[MSP_INDEX_O3] = 1;
  }

  if ((msp[MSP_INDEX_PM25] > 0) && (msp[MSP_INDEX_NO2] > 0) && (msp[MSP_INDEX_O3] > 0) && ((msp[MSP_INDEX_PM25] == msp[MSP_INDEX_NO2]) || (msp[MSP_INDEX_PM25] == msp[MSP_INDEX_O3]) || (msp[MSP_INDEX_NO2] == msp[MSP_INDEX_O3]))) {  //return the most dominant
    if (msp[MSP_INDEX_NO2] == msp[MSP_INDEX_O3]) return msp[MSP_INDEX_NO2];
    else return msp[MSP_INDEX_PM25];
  } else {  // return the worst one
    if ((msp[MSP_INDEX_PM25] > msp[MSP_INDEX_NO2]) && (msp[MSP_INDEX_PM25] > msp[MSP_INDEX_O3])) return msp[MSP_INDEX_PM25];
    else if (msp[MSP_INDEX_NO2] > msp[MSP_INDEX_O3]) return msp[MSP_INDEX_NO2];
    else return msp[MSP_INDEX_O3];
  }
}

//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

#endif