/*
                        Milano Smart Park Firmware
                      Copyright (c) Norman Mulinacci

          This code is usable under the terms and conditions of the
             GNU GENERAL PUBLIC LICENSE Version 3, 29 June 2007

             Parts of this code are based on open source works
                 freely distributed by Luca Crotti @2019
*/

// Sensors and Data Management Functions

//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

String floatToComma(float value) { // converts float values in strings with the decimal part separated from the integer part by a comma

  String convert = String(value, 3);
  convert.replace(".", ",");
  return convert;

}

//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

float convertPpmToUgM3(float ppm, float mm) { // calculates ug/m3 from a gas ppm concentration

  // mm is molar mass and must be g/mol
  // using OSHA standard conditions to perform the conversion
  float T = 25.0;
  float P = 1013.25;
  const float R = 83.1446261815324; //gas constant (L * hPa * K^−1 * mol^−1)
  float Vm = (R * (T + 273.15)) / P; //molar volume (L * mol^-1)
  return (ppm * 1000) * (mm / Vm);

}

//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

bool checkBMESensor() { // checks BME680 status

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

bool isAnalogO3Connected() { // checks analog ozone sensor status

  int detect = analogReadMilliVolts(O3_ADC_PIN);
  log_d("Detect mV: %d", detect);
  if (detect > 360) return true;
  return false;

}

//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

float analogUgM3O3Read(float *intemp) { // reads and calculates ozone ppm value from analog ozone sensor

  int points = 0;
  float T = 25.0; // initialized at OSHA standard conditions for temperature compensation
  if (BME_run) {
    T = *intemp; // using current measured temperature
    log_d("Current measured temperature is %.3f", T);
  }
  const short readtimes = 10; // reading 10 times for good measure
  for (short i = 0; i < readtimes; i++) {
    int readnow = analogRead(O3_ADC_PIN);
    log_v("ADC Read is: %d", readnow);
    points += readnow;
    delay(10);
  }
  points /= readtimes;
  log_d("ADC Read averaged is: %d", points);
  points -= o3zeroval;
  if (points <= 0) return 0.0;
  return ((points * 2.03552924) * 12.187 * 48) / (273.15 + T); // temperature compensated

}

//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

void writeMicsValues() { // write firmware calibration values into MICS6814's EEPROM

  // Store new base resistance values in EEPROM
  Wire.beginTransmission(DATA_I2C_ADDR);
  Wire.write(CMD_V2_SET_R0);
  Wire.write(MICSCal[2] >> 8); // NH3
  Wire.write(MICSCal[2] & 0xFF);
  Wire.write(MICSCal[0] >> 8); // RED
  Wire.write(MICSCal[0] & 0xFF);
  Wire.write(MICSCal[1] >> 8); // OX
  Wire.write(MICSCal[1] & 0xFF);
  Wire.endTransmission();

}

//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

bool checkMicsValues() { // check if MICS6814 internal values are the same as firmware defaults

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

short evaluateMSPIndex(float pm25, float nox, float o3) { // evaluates the MPS# index from ug/m3 concentrations of specific gases using standard IAQ values (needs 1h averages)

  // possible returned values are: 0 -> n.d.(grey); 1 -> good(green); 2 -> acceptable(yellow); 3 -> bad(red); 4 -> really bad(black)
  log_i("Evaluating MSP# index...\n");

  short msp[3] = {0, 0, 0}; // msp[0] is for pm2.5, msp[1] is for nox, msp[2] is for o3

  if (PMS_run) {
    if (pm25 > 50) msp[0] = 4;
    else if (pm25 > 25) msp[0] = 3;
    else if (pm25 > 10) msp[0] = 2;
    else msp[0] = 1;
  }
  if (MICS_run) {
    if (nox > 400) msp[1] = 4;
    else if (nox > 200) msp[1] = 3;
    else if (nox > 100) msp[1] = 2;
    else msp[1] = 1;
  }
  if (O3_run) {
    if (o3 > 240) msp[2] = 4;
    else if (o3 > 180) msp[2] = 3;
    else if (o3 > 120) msp[2] = 2;
    else msp[2] = 1;
  }

  if (msp[0] > 0 && msp[1] > 0 && msp[2] > 0 && (msp[0] == msp [1] || msp[0] == msp[2] || msp[1] == msp[2])) { //return the most dominant
    if (msp[1] == msp[2]) return msp[1];
    else return msp[0];
  } else { // return the worst one
    if (msp[0] > msp[1] && msp[0] > msp[2]) return msp[0];
    else if (msp[1] > msp[2]) return msp[1];
    else return msp[2];
  }

}

//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
