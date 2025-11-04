/****************************************************************************
 * @file    sensors.cpp
 * @author  Refactored by AB-Engineering - https://ab-engineering.it
 * @brief   functions to fetch the data from sensors and process
 * @version 0.1
 * @date    2025-07-26
 *
 * @copyright Copyright (c) 2025
 *
 ***************************************************************************/

// -- includes
#include <stdint.h>
#include <stdio.h>
#include "config.h"
#include "generic_functions.h"
#include <MiCS6814-I2C.h>
#include <DFRobot_MICS.h>
#include "sensors.h"
#include <stdbool.h>

// PM25 THRESHOLDS
#define PM25_HIGH_LEVEL 50
#define PM25_MID_LEVEL 25
#define PM25_LOW_LEVEL 10
// NO THRESHOLDS
#define NO_HIGH_LEVEL 400
#define NO_MID_LEVEL 200
#define NO_LOW_LEVEL 100
// O3 THRESHOLDS
#define O3_HIGH_LEVEL 240
#define O3_MID_LEVEL 180
#define O3_LOW_LEVEL 120

/***************************************************
 * @brief checks BME680 status
 *
 * @return true
 * @return false
 ***************************************************/
mspStatus_t tHalSensor_checkBMESensor(Bsec *ptr)
{
  if (ptr->bsecStatus < BSEC_OK)
  {
    log_e("BSEC error, status %d!", ptr->bsecStatus);
    return STATUS_ERR;
  }
  else if (ptr->bsecStatus > BSEC_OK)
  {
    log_w("BSEC warning, status %d!", ptr->bsecStatus);
  }

  if (ptr->bme68xStatus < BME68X_OK)
  {
    log_e("Sensor error, bme680_status %d!", ptr->bme68xStatus);
    return STATUS_ERR;
  }
  else if (ptr->bme68xStatus > BME68X_OK)
  {
    log_w("Sensor warning, status %d!", ptr->bme68xStatus);
  }

  ptr->bsecStatus = BSEC_OK;
  return STATUS_OK;
}

/***************************************************
 * @brief checks analog ozone sensor status
 *
 * @return mspStatus_t
 ***************************************************/
mspStatus_t tHalSensor_isAnalogO3Connected()
{
  u_int16_t detect = 0;
  detect = analogRead(O3_ADC_PIN);
  pinMode(O3_ADC_PIN, INPUT_PULLDOWN); // must invoke after every analogRead
  log_d("Detected points: %d", detect);
  if (detect == STATUS_ERR)
  {
    return STATUS_ERR;
  }
  return STATUS_OK;
}

/********************************************************************
 * @brief write firmware calibration values into MICS6814's EEPROM
 *        Store new base resistance values in EEPROM
 *
 * @param p_tData
 ********************************************************************/
void vHalSensor_writeMicsValues(sensorData_t *p_tData)
{
  Wire.beginTransmission(DATA_I2C_ADDR);
  Wire.write(CMD_V2_SET_R0);
  Wire.write(p_tData->micsTuningData.sensingResInAir.nh3Sensor >> 8); // NH3
  Wire.write(p_tData->micsTuningData.sensingResInAir.nh3Sensor & 0xFF);
  Wire.write(p_tData->micsTuningData.sensingResInAir.redSensor >> 8); // RED
  Wire.write(p_tData->micsTuningData.sensingResInAir.redSensor & 0xFF);
  Wire.write(p_tData->micsTuningData.sensingResInAir.oxSensor >> 8); // OX
  Wire.write(p_tData->micsTuningData.sensingResInAir.oxSensor & 0xFF);
  Wire.endTransmission();
}

/**********************************************************
 * @brief   check if MICS6814 internal values are
 *          the same as firmware defaults
 *
 * @param p_tData
 * @return mspStatus_t
 **********************************************************/
mspStatus_t tHalSensor_checkMicsValues(sensorData_t *p_tData, sensorR0Value_t *ptr)
{
  if ((ptr->redSensor == p_tData->micsTuningData.sensingResInAir.redSensor) && (ptr->oxSensor == p_tData->micsTuningData.sensingResInAir.oxSensor) && (ptr->nh3Sensor == p_tData->micsTuningData.sensingResInAir.nh3Sensor))
  {
    return STATUS_OK;
  }
  return STATUS_ERR;
}

/*********************************************************************************
 * @brief compensate gas sensor readings (specifically for NO2 and VOCs)
 *        based on environmental conditions: temperature, pressure, and humidity.
 *        for NO2 and VOC gas compensations
 *
 * @param inputGas
 * @param p_tcurrData
 * @param p_tData
 * @return float
 **********************************************************************************/
float fHalSensor_no2AndVocCompensation(float inputGas, bme680Data_t *p_tcurrData, sensorData_t *p_tData)
{
  return (inputGas * (((p_tcurrData->humidity + HUMIDITY_OFFSET) / PERCENT_DIVISOR) * p_tData->compParams.currentHumidity)) +
         ((p_tcurrData->temperature - REFERENCE_TEMP_C) * p_tData->compParams.currentTemperature) -
         ((p_tcurrData->pressure - REFERENCE_PRESSURE_HPA) * p_tData->compParams.currentPressure);
}

/********************************************************************************
 * @brief reads and calculates ozone ppm value from analog ozone sensor
 *
 * @param intemp
 * @param p_tData
 * @return float
 *******************************************************************************/
float fHalSensor_analogUgM3O3Read(float *intemp, sensorData_t *p_tData)
{

  int points = 0;
  float currTemp = REFERENCE_TEMP_C; // initialized at OSHA standard conditions for temperature compensation
  if (p_tData->status.BME680Sensor)
  {
    currTemp = *intemp; // using current measured temperature
    log_d("Current measured temperature is %.3f", currTemp);
  }
  const short readtimes = 10; // reading 10 times for good measure
  for (short i = 0; i < readtimes; i++)
  {
    int readnow = analogRead(O3_ADC_PIN);
    pinMode(O3_ADC_PIN, INPUT_PULLDOWN); // must invoke after every analogRead
    log_v("ADC Read is: %d", readnow);
    points += readnow;
    delay(10);
  }
  points /= readtimes;
  log_d("ADC Read averaged is: %d", points);
  points -= p_tData->ozoneData.o3ZeroOffset;
  if (points <= 0)
    return 0.0;
  return (((points * O3_CALC_FACTOR_1) * O3_CALC_FACTOR_2 * O3_CALC_FACTOR_3) / (CELIUS_TO_KELVIN + currTemp)); // temperature compensated
}

/******************************************************************************
 * @brief   print measurements to serial output
 *
 * @param data
 *****************************************************************************/
void vHalSensor_printMeasurementsOnSerial(send_data_t *data, sensorData_t *p_tPtr)
{

  char locDate[DATE_LEN] = {0};
  char locTime[TIME_LEN] = {0};

  strftime(locDate, sizeof(locDate), "%d/%m/%Y", &data->sendTimeInfo); // Formatting date as DD/MM/YYYY
  strftime(locTime, sizeof(locDate), "%T", &data->sendTimeInfo);       // Formatting time as HH:MM:SS

  log_i("Measurements log:"); // Log measurements to serial output
  log_i("Date&time: %s %s", locDate, locTime);
  if (p_tPtr->status.BME680Sensor)
  {
    log_i("Temperature: %.2f C", data->temp);
    log_i("Humidity: %.2f%%", data->hum);
    log_i("Pressure: %.2f hPa", data->pre);
    log_i("VOC: %.2f kOhm", data->VOC);
  }
  if (p_tPtr->status.PMS5003Sensor)
  {
    log_i("PM10: %d ug/m3", data->PM10);
    log_i("PM2.5: %d ug/m3", data->PM25);
    log_i("PM1: %d ug/m3", data->PM1);
  }
  if (p_tPtr->status.O3Sensor)
  {
    log_i("O3: %.2f ug/m3", data->ozone);
  }
  if (p_tPtr->status.MICS6814Sensor)
  {
    log_i("NOx: %.2f ug/m3", data->MICS_NO2);
    log_i("CO: %.2f ug/m3", data->MICS_CO);
    log_i("NH3: %.2f ug/m3", data->MICS_NH3);
  }
  log_i("Measurements logged successfully");
}

/*****************************************************************************************************
 * @brief
 *
 * @param p_tErr
 * @param p_tData
 * @param number_of_measurements
 *****************************************************************************************************/
void vHalSensor_performAverages(errorVars_t *p_tErr, sensorData_t *p_tData, deviceMeasurement_t *p_tMeas)
{

  log_i("=== AVERAGING CALCULATION ===");
  log_i("Total measurement_count: %d", p_tMeas->measurement_count);
  log_i("Error counts: BME=%d, PMS=%d, MICS=%d, O3=%d", p_tErr->BMEfails, p_tErr->PMSfails, p_tErr->MICSfails, p_tErr->O3fails);

  short runs = p_tMeas->measurement_count - p_tErr->BMEfails;
  log_i("BME680: runs = %d - %d = %d", p_tMeas->measurement_count, p_tErr->BMEfails, runs);
  if (p_tData->status.BME680Sensor && (runs > 0))
  {
    log_i("BME680 BEFORE: temp=%.3f, pressure=%.3f, humidity=%.3f", 
          p_tData->gasData.temperature, p_tData->gasData.pressure, p_tData->gasData.humidity);
    p_tData->gasData.temperature /= runs;
    p_tData->gasData.pressure /= runs;
    p_tData->gasData.humidity /= runs;
    p_tData->gasData.volatileOrganicCompounds /= runs;
    log_i("BME680 AFTER: temp=%.3f, pressure=%.3f, humidity=%.3f (divided by %d)", 
          p_tData->gasData.temperature, p_tData->gasData.pressure, p_tData->gasData.humidity, runs);
  }
  else if (p_tData->status.BME680Sensor)
  {
    p_tData->status.BME680Sensor = false;
    p_tErr->senserrs[SENS_STAT_BME680] = true;
  }

  runs = p_tMeas->measurement_count - p_tErr->PMSfails;
  if (p_tData->status.PMS5003Sensor && (runs > 0))
  {
    float pmValue = 0.0f;

    pmValue = p_tData->airQualityData.particleMicron1 / runs;

    if ((pmValue - (int32_t) pmValue) >= ROUNDING_THRESHOLD)
    {
      p_tData->airQualityData.particleMicron1 = (int32_t) pmValue + 1;
    }
    else
    {
      p_tData->airQualityData.particleMicron1 = (int32_t) pmValue;
    }

    pmValue = p_tData->airQualityData.particleMicron25 / runs;

    if ((pmValue - (int32_t) pmValue) >= ROUNDING_THRESHOLD)
    {
      p_tData->airQualityData.particleMicron25 = (int32_t) pmValue + 1;
    }
    else
    {
      p_tData->airQualityData.particleMicron25 = (int32_t) pmValue;
    }

    pmValue = p_tData->airQualityData.particleMicron10 / runs;
    if ((pmValue - (int32_t) pmValue) >= ROUNDING_THRESHOLD)
    {
      p_tData->airQualityData.particleMicron10 = (int32_t) pmValue + 1;
    }
    else
    {
      p_tData->airQualityData.particleMicron10 = (int32_t) pmValue;
    }
  }
  else if (p_tData->status.PMS5003Sensor)
  {
    p_tData->status.PMS5003Sensor = false;
    p_tErr->senserrs[SENS_STAT_PMS5003] = true;
  }

  runs = p_tMeas->measurement_count - p_tErr->MICSfails;
  if (p_tData->status.MICS6814Sensor && (runs > 0))
  {
    p_tData->pollutionData.carbonMonoxide /= runs;
    p_tData->pollutionData.nitrogenDioxide /= runs;
    p_tData->pollutionData.ammonia /= runs;
  }
  else if (p_tData->status.MICS6814Sensor)
  {
    p_tData->status.MICS6814Sensor = false;
    p_tErr->senserrs[SENS_STAT_MICSxxxx] = true;
  }

  // MICS4514 sensor averaging - calculate gas concentrations from averaged ADC values
  if (p_tData->status.MICS4514Sensor && (runs > 0))
  {
    MICS4514SensorReading_t micsReading;
    if (tHalSensor_calculateMICS4514_Gases(p_tData, &micsReading, runs) == STATUS_OK)
    {
      // Convert PPM values to ug/m3 (same as done in main loop)
      micsReading.carbonMonoxide = vGeneric_convertPpmToUgM3(micsReading.carbonMonoxide, p_tData->molarMass.carbonMonoxide);
      log_i("MICS4514 CO converted: %.2f ug/m3", micsReading.carbonMonoxide);

      // Convert NO2 from PPM to ug/m3 and apply environmental compensation if BME680 is available
      micsReading.nitrogenDioxide = vGeneric_convertPpmToUgM3(micsReading.nitrogenDioxide, p_tData->molarMass.nitrogenDioxide);
      if (p_tData->status.BME680Sensor)
      {
        // Create BME680 data structure for compensation using averaged values
        bme680Data_t avgBmeData;
        avgBmeData.temperature = p_tData->gasData.temperature / runs;
        avgBmeData.pressure = p_tData->gasData.pressure / runs;
        avgBmeData.humidity = p_tData->gasData.humidity / runs;
        avgBmeData.volatileOrganicCompounds = p_tData->gasData.volatileOrganicCompounds / runs;

        micsReading.nitrogenDioxide = fHalSensor_no2AndVocCompensation(micsReading.nitrogenDioxide, &avgBmeData, p_tData);
        log_i("MICS4514 NO2 compensated: %.2f ug/m3", micsReading.nitrogenDioxide);
      }
      else
      {
        log_i("MICS4514 NO2 converted: %.2f ug/m3", micsReading.nitrogenDioxide);
      }

      // Convert NH3 from PPM to ug/m3
      micsReading.ammonia = vGeneric_convertPpmToUgM3(micsReading.ammonia, p_tData->molarMass.ammonia);
      log_i("MICS4514 NH3 converted: %.2f ug/m3", micsReading.ammonia);

      // Store final calculated values in pollution data structure
      p_tData->pollutionData.carbonMonoxide = micsReading.carbonMonoxide;
      p_tData->pollutionData.nitrogenDioxide = micsReading.nitrogenDioxide;
      p_tData->pollutionData.ammonia = micsReading.ammonia;

      log_i("MICS4514 averaging complete: CO=%.2f, NO2=%.2f, NH3=%.2f ug/m3",
            micsReading.carbonMonoxide, micsReading.nitrogenDioxide, micsReading.ammonia);
    }
    else
    {
      log_e("MICS4514 gas calculation failed during averaging");
      p_tData->status.MICS4514Sensor = false;
      p_tErr->senserrs[SENS_STAT_MICSxxxx] = true;
    }
  }
  else if (p_tData->status.MICS4514Sensor)
  {
    p_tData->status.MICS4514Sensor = false;
    p_tErr->senserrs[SENS_STAT_MICSxxxx] = true;
  }

  runs = p_tMeas->measurement_count - p_tErr->O3fails;
  if (p_tData->status.O3Sensor && runs > 0)
  {
    p_tData->ozoneData.ozone /= runs;
  }
  else if (p_tData->status.O3Sensor)
  {
    p_tData->status.O3Sensor = false;
    p_tErr->senserrs[SENS_STAT_O3] = true;
  }
}

/*****************************************************************************************************
 * @brief   evaluates the MSP# index from ug/m3 concentrations of specific gases using standard
 *          IAQ values (needs 1h averages)
 *
 *          possible returned values are:
 *          0 -> n.d.(grey);
 *          1 -> good(green);
 *          2 -> acceptable(yellow);
 *          3 -> bad(red);
 *          4 -> really bad(black)
 *
 * @param  p_tData  pointer to sensor data
 * @return short
 *******************************************************************************************************/
short sHalSensor_evaluateMSPIndex(sensorData_t *p_tData)
{
  log_i("Evaluating MSP# index...\n");

  short msp[MSP_INDEX_MAX] = {0, 0, 0}; // msp[0] is for pm2.5, msp[1] is for nox, msp[2] is for o3

  if (p_tData->status.PMS5003Sensor)
  {
    if (p_tData->airQualityData.particleMicron25 > PM25_HIGH_LEVEL)
      msp[MSP_INDEX_PM25] = 4;
    else if (p_tData->airQualityData.particleMicron25 > PM25_MID_LEVEL)
      msp[MSP_INDEX_PM25] = 3;
    else if (p_tData->airQualityData.particleMicron25 > PM25_LOW_LEVEL)
      msp[MSP_INDEX_PM25] = 2;
    else
      msp[MSP_INDEX_PM25] = 1;
  }
  if (p_tData->status.MICS6814Sensor)
  {
    if (p_tData->pollutionData.nitrogenDioxide > NO_HIGH_LEVEL)
      msp[MSP_INDEX_NO2] = 4;
    else if (p_tData->pollutionData.nitrogenDioxide > NO_MID_LEVEL)
      msp[MSP_INDEX_NO2] = 3;
    else if (p_tData->pollutionData.nitrogenDioxide > NO_LOW_LEVEL)
      msp[MSP_INDEX_NO2] = 2;
    else
      msp[MSP_INDEX_NO2] = 1;
  }
  if (p_tData->status.O3Sensor)
  {
    if (p_tData->ozoneData.ozone > O3_HIGH_LEVEL)
      msp[MSP_INDEX_O3] = 4;
    else if (p_tData->ozoneData.ozone > O3_MID_LEVEL)
      msp[MSP_INDEX_O3] = 3;
    else if (p_tData->ozoneData.ozone > O3_LOW_LEVEL)
      msp[MSP_INDEX_O3] = 2;
    else
      msp[MSP_INDEX_O3] = 1;
  }

  if ((msp[MSP_INDEX_PM25] > 0) && (msp[MSP_INDEX_NO2] > 0) && (msp[MSP_INDEX_O3] > 0) && ((msp[MSP_INDEX_PM25] == msp[MSP_INDEX_NO2]) || (msp[MSP_INDEX_PM25] == msp[MSP_INDEX_O3]) || (msp[MSP_INDEX_NO2] == msp[MSP_INDEX_O3])))
  { // return the most dominant
    if (msp[MSP_INDEX_NO2] == msp[MSP_INDEX_O3])
      return msp[MSP_INDEX_NO2];
    else
      return msp[MSP_INDEX_PM25];
  }
  else
  { // return the worst one
    if ((msp[MSP_INDEX_PM25] > msp[MSP_INDEX_NO2]) && (msp[MSP_INDEX_PM25] > msp[MSP_INDEX_O3]))
      return msp[MSP_INDEX_PM25];
    else if (msp[MSP_INDEX_NO2] > msp[MSP_INDEX_O3])
      return msp[MSP_INDEX_NO2];
    else
      return msp[MSP_INDEX_O3];
  }
}

/*****************************************************************************************************
 * @brief   Custom MICS4514 gas calculation functions using calibration parameters from config
 *          These functions use ADC readings and calibration values instead of DFRobot's
 *          hardcoded coefficients
 *******************************************************************************************************/

/**
 * @brief Read raw ADC values from MICS4514 sensor and accumulate for averaging
 * @param mics4514          Reference to DFRobot MICS4514 sensor object
 * @param p_tData           Pointer to sensor data structure with accumulator
 * @return mspStatus_t      STATUS_OK on success, STATUS_ERR on failure
 */
mspStatus_t tHalSensor_readMICS4514_ADC(DFRobot_MICS& mics4514, sensorData_t* p_tData)
{
  // Get voltage readings from MICS4514 sensor using standard DFRobot API
  // getADCData() returns voltage in ADC counts (10-bit ADC: 0-1023)
  // This represents the voltage on the sensing resistor
  int16_t ox_voltage_counts = mics4514.getADCData(OX_MODE);
  int16_t red_voltage_counts = mics4514.getADCData(RED_MODE);

  log_d("MICS4514 ADC readings - OX: %d counts, RED: %d counts", ox_voltage_counts, red_voltage_counts);

  // Validate voltage readings
  if (ox_voltage_counts < 0 || red_voltage_counts < 0) {
    log_e("Invalid MICS4514 voltage readings: OX=%d, RED=%d", ox_voltage_counts, red_voltage_counts);
    return STATUS_ERR;
  }

  // Accumulate raw ADC values for later averaging
  p_tData->mics4514AdcAccumulator.oxVoltageSum += (uint32_t)ox_voltage_counts;
  p_tData->mics4514AdcAccumulator.redVoltageSum += (uint32_t)red_voltage_counts;

  log_d("MICS4514 accumulated - OX sum: %u, RED sum: %u",
        p_tData->mics4514AdcAccumulator.oxVoltageSum,
        p_tData->mics4514AdcAccumulator.redVoltageSum);

  return STATUS_OK;
}

/**
 * @brief Calculate gas concentrations from averaged MICS4514 ADC values
 * @param p_tData           Pointer to sensor data structure with calibration values and accumulated ADC
 * @param p_tMicsReading    Pointer to structure to store the calculated gas concentrations
 * @param measurement_count Number of measurements accumulated
 * @return mspStatus_t      STATUS_OK on success, STATUS_ERR on failure
 */
mspStatus_t tHalSensor_calculateMICS4514_Gases(sensorData_t* p_tData, MICS4514SensorReading_t* p_tMicsReading, int measurement_count)
{
  if (measurement_count <= 0) {
    log_e("Invalid measurement count: %d", measurement_count);
    return STATUS_ERR;
  }

  // Calculate average ADC values from accumulated sums
  float avg_ox_voltage = (float)p_tData->mics4514AdcAccumulator.oxVoltageSum / (float)measurement_count;
  float avg_red_voltage = (float)p_tData->mics4514AdcAccumulator.redVoltageSum / (float)measurement_count;

  log_i("MICS4514 averaged ADC values - OX: %.1f counts, RED: %.1f counts (from %d measurements)",
        avg_ox_voltage, avg_red_voltage, measurement_count);

  // Calculate Rs/R0 ratios from averaged ADC values using config calibration values
  // Since both averaged readings and calibration values use the same getADCData() function,
  // all scaling factors (voltage, ADC bits, etc.) cancel out in the ratio
  float rs_r0_red = avg_red_voltage / (float)p_tData->micsTuningData.sensingResInAir.redSensor;
  float rs_r0_ox = avg_ox_voltage / (float)p_tData->micsTuningData.sensingResInAir.oxSensor;

  log_i("MICS4514 Rs/R0 ratios from averaged data - RED: %.3f, OX: %.3f", rs_r0_red, rs_r0_ox);

  // Calculate gas concentrations using common helper function
  if (tHalSensor_calculateGasFromRsR0(rs_r0_red, rs_r0_ox, p_tMicsReading) != STATUS_OK) {
    log_e("MICS4514 gas calculation from Rs/R0 failed");
    return STATUS_ERR;
  }

  log_i("MICS4514 calculated from %d averaged measurements: CO: %.2f ppm - NO2: %.2f ppm - NH3: %.2f ppm",
        measurement_count, p_tMicsReading->carbonMonoxide, p_tMicsReading->nitrogenDioxide, p_tMicsReading->ammonia);

  // Reset accumulator for next measurement cycle
  p_tData->mics4514AdcAccumulator.oxVoltageSum = 0;
  p_tData->mics4514AdcAccumulator.redVoltageSum = 0;

  return STATUS_OK;
}

/**
 * @brief Calculate immediate gas concentrations from current MICS4514 ADC reading (for display)
 * @param mics4514          Reference to DFRobot MICS4514 sensor object
 * @param p_tData           Pointer to sensor data structure with calibration values
 * @param p_tMicsReading    Pointer to structure to store the calculated gas concentrations
 * @return mspStatus_t      STATUS_OK on success, STATUS_ERR on failure
 */
mspStatus_t tHalSensor_calculateMICS4514_Immediate(DFRobot_MICS& mics4514, sensorData_t* p_tData, MICS4514SensorReading_t* p_tMicsReading)
{
  // Get fresh voltage readings from MICS4514 sensor for immediate calculation
  int16_t ox_voltage_counts = mics4514.getADCData(OX_MODE);
  int16_t red_voltage_counts = mics4514.getADCData(RED_MODE);

  log_d("MICS4514 immediate ADC readings - OX: %d counts, RED: %d counts", ox_voltage_counts, red_voltage_counts);

  // Validate voltage readings
  if (ox_voltage_counts < 0 || red_voltage_counts < 0) {
    log_e("Invalid MICS4514 immediate voltage readings: OX=%d, RED=%d", ox_voltage_counts, red_voltage_counts);
    return STATUS_ERR;
  }

  // Calculate Rs/R0 ratios directly from current readings
  float rs_r0_red = (float)red_voltage_counts / (float)p_tData->micsTuningData.sensingResInAir.redSensor;
  float rs_r0_ox = (float)ox_voltage_counts / (float)p_tData->micsTuningData.sensingResInAir.oxSensor;

  log_d("MICS4514 immediate Rs/R0 ratios - RED: %.3f, OX: %.3f", rs_r0_red, rs_r0_ox);

  // Calculate gas concentrations using common helper function
  if (tHalSensor_calculateGasFromRsR0(rs_r0_red, rs_r0_ox, p_tMicsReading) != STATUS_OK) {
    log_e("MICS4514 immediate gas calculation from Rs/R0 failed");
    return STATUS_ERR;
  }

  log_d("MICS4514 immediate calculation: CO: %.2f ppm - NO2: %.2f ppm - NH3: %.2f ppm",
        p_tMicsReading->carbonMonoxide, p_tMicsReading->nitrogenDioxide, p_tMicsReading->ammonia);

  return STATUS_OK;
}

/**
 * @brief Calculate gas concentrations from Rs/R0 ratios using MICS4514 datasheet formulas
 * @param rs_r0_red         Rs/R0 ratio for RED sensor (CO, NH3)
 * @param rs_r0_ox          Rs/R0 ratio for OX sensor (NO2)
 * @param p_tMicsReading    Pointer to structure to store the calculated gas concentrations
 * @return mspStatus_t      STATUS_OK on success, STATUS_ERR on failure
 */
mspStatus_t tHalSensor_calculateGasFromRsR0(const float rs_r0_red, const float rs_r0_ox, MICS4514SensorReading_t* p_tMicsReading)
{
  // Use datasheet-based calculations for accurate gas concentration
  // Based on MICS4514 datasheet graphs for 25C, 50% RH conditions

  // CO calculation - RED sensor, Rs/R0 decreases with CO concentration
  // From datasheet: Rs/R0 goes from ~3.0 at 1ppm to ~0.01 at 1000ppm
  if (bGeneric_floatGreaterThan(rs_r0_red, 4.0f)) {
    p_tMicsReading->carbonMonoxide = 0.9f; // Clean air baseline
  } else {
    // Logarithmic relationship from graph: ppm = 4.671234 * (Rs/R0)^(-1.198476)
    // Using precise data points from MICS4514 datasheet graph
    p_tMicsReading->carbonMonoxide = 4.671234f * pow(rs_r0_red, -1.198476f);
    if (p_tMicsReading->carbonMonoxide > 1000.0f) p_tMicsReading->carbonMonoxide = 1000.0f;
    if (p_tMicsReading->carbonMonoxide < 1.0f) p_tMicsReading->carbonMonoxide = 1.0f;
  }

  // NO2 calculation - OX sensor, Rs/R0 increases with NO2 concentration
  // From datasheet: Rs/R0 goes from 0.06 at 0.01ppm to 40 at 6ppm
  if (bGeneric_floatLessThan(rs_r0_ox, 0.06f)) {
    p_tMicsReading->nitrogenDioxide = 0.0f; // Clean air baseline
  } else {
    // Power law relationship from graph: ppm = 0.149312 * (Rs/R0)^(0.988391)
    // Using precise data points from MICS4514 datasheet graph
    p_tMicsReading->nitrogenDioxide = 0.149312f * pow(rs_r0_ox, 0.988391f);
    if (p_tMicsReading->nitrogenDioxide < 0.01f) p_tMicsReading->nitrogenDioxide = 0.0f;
    if (p_tMicsReading->nitrogenDioxide > 7.0f) p_tMicsReading->nitrogenDioxide = 7.0f;
  }

  // NH3 calculation - RED sensor, Rs/R0 decreases with NH3 concentration
  // From datasheet: NH3 follows power law relationship like CO
  if (bGeneric_floatGreaterThan(rs_r0_red, 1.0f)) {
    p_tMicsReading->ammonia = 1.0f; // Clean air baseline
  } else {
    // Logarithmic relationship from graph: ppm = 1.101083 * (Rs/R0)^(-3.734670)
    // Using precise data points from MICS4514 datasheet graph
    p_tMicsReading->ammonia = 1.101083f * pow(rs_r0_red, -3.734670f);
    if (p_tMicsReading->ammonia > 200.0f) p_tMicsReading->ammonia = 200.0f;
    if (p_tMicsReading->ammonia < 1.0f) p_tMicsReading->ammonia = 1.0f;
  }

  // Validate results
  if ((p_tMicsReading->carbonMonoxide < 0) || (p_tMicsReading->nitrogenDioxide < 0) || (p_tMicsReading->ammonia < 0)) {
    log_w("MICS4514 gas calculation returned negative values");
    return STATUS_ERR;
  }

  return STATUS_OK;
}