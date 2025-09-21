/******************************************************************************
 * @file    sensors.h
 * @author  Refactored by AB-Engineering - https://ab-engineering.it
 * @brief   Milano Smart Park Firmware
            Developed by Norman Mulinacci
            This code is usable under the terms and conditions of the
            GNU GENERAL PUBLIC LICENSE Version 3, 29 June 2007
 * @version 0.1
 * @date    2025-07-26
 * 
 * @copyright Copyright (c) 2025
 * 
 *****************************************************************************/

#ifndef SENSORS_H
#define SENSORS_H

// -- includes --
#include "shared_values.h"
#include <bsec.h>

// Forward declaration to avoid circular includes
class DFRobot_MICS;

#define SEA_LEVEL_ALTITUDE_IN_M     122.0f /*!<sea level altitude in meters, defaults to Milan, Italy */

#define REFERENCE_TEMP_C            25.0f             // Standard temperature in Celsius
#define GAS_CONSTANT                8.314f             // Ideal gas constant in J/(mol·K)
#define REFERENCE_PRESSURE_HPA      1013.25f           // Standard pressure in hPa
#define HUMIDITY_OFFSET             50.0f              // humidity offset 
#define PERCENT_DIVISOR             100.0f           // percent divisor

#define CELIUS_TO_KELVIN            273.15f // Conversion from Celsius to Kelvin
#define MICROGRAMS_PER_GRAM         1000.0f // µg in 1 gram

#define STD_TEMP_LAPSE_RATE         0.0065f // Standard temperature lapse rate: Temperature decrease with altitude
#define ISA_DERIVED_EXPONENTIAL     -5.257f //Exponent derived from ISA model: Based on gas constant, gravity, etc.

#define R0_RED_SENSOR (955U)
#define R0_OX_SENSOR  (900U)
#define R0_NH3_SENSOR (163U)

#define CO_MOLAR_MASS (28.01f)
#define NO2_MOLAR_MASS (46.01f)
#define NH3_MOLAR_MASS (17.03f)

#define HUMIDITY_COMP_PARAM (0.6f)
#define TEMP_COMP_PARAM     (1.352f)
#define PRESS_COMP_PARAM    (0.0132f)

#define DEFAULT_SENSOR_OFFSET (0)

#define MSP_DEFAULT_DATA ((int8_t) -1)

/*!< ozone sensor ADC zero default offset is 1489, -1 to disable it (0.4V to 1.1V range in 12bit resolution at 0dB attenuation) */
#define O3_SENS_DISABLE_ZERO_OFFSET (-1)

#define PMS_PREHEAT_TIME_IN_SEC 20 /*!<PMS5003 preheat time in seconds, defaults to 45 seconds */

// PPM to µg/m³ conversion constants
#define MOLAR_VOLUME_STP            24.45f             // Molar volume at STP (L/mol)
#define PPM_TO_UGM3_FACTOR          1000.0f            // Conversion factor for PPM to µg/m³

// Ozone sensor calculation constants  
#define O3_CALC_FACTOR_1            2.03552924f        // Ozone calculation factor 1
#define O3_CALC_FACTOR_2            12.187f            // Ozone calculation factor 2
#define O3_CALC_FACTOR_3            48.0f              // Ozone calculation factor 3

// Sensor reading retry constants
#define MAX_SENSOR_RETRIES          3                  // Maximum number of retry attempts
#define ROUNDING_THRESHOLD          0.5f               // Threshold for rounding operations

// Default calibration values as floats
#define R0_RED_SENSOR_F             955.0f             // Default R0 for red sensor (CO)
#define R0_OX_SENSOR_F              900.0f             // Default R0 for OX sensor (NO2) 
#define R0_NH3_SENSOR_F             163.0f             // Default R0 for NH3 sensor

/***************************************************
 * @brief checks BME680 status
 *
 * @param ptr Pointer to Bsec object
 * @return true
 * @return false
 ***************************************************/
mspStatus_t tHalSensor_checkBMESensor(Bsec *ptr);

/***************************************************
 * @brief checks analog ozone sensor status
 *
 * @return mspStatus_t
 ***************************************************/
mspStatus_t tHalSensor_isAnalogO3Connected();

/********************************************************************
 * @brief write firmware calibration values into MICS6814's EEPROM
 *        Store new base resistance values in EEPROM
 *
 * @param p_tData
 ********************************************************************/
void vHalSensor_writeMicsValues(sensorData_t *p_tData);

/**********************************************************
 * @brief   check if MICS6814 internal values are
 *          the same as firmware defaults
 *
 * @param p_tData
 * @param ptr
 * @return mspStatus_t
 **********************************************************/
mspStatus_t tHalSensor_checkMicsValues(sensorData_t *p_tData, sensorR0Value_t *ptr);

/*********************************************************************************
 * @brief compensate gas sensor readings (specifically for NO₂ and VOCs)
 *        based on environmental conditions: temperature, pressure, and humidity.
 *        for NO2 and VOC gas compensations
 *
 * @param inputGas
 * @param p_tcurrData
 * @param p_tData
 * @return float
 **********************************************************************************/
float fHalSensor_no2AndVocCompensation(float inputGas, bme680Data_t *p_tcurrData, sensorData_t *p_tData);

/********************************************************************************
 * @brief reads and calculates ozone ppm value from analog ozone sensor
 *
 * @param intemp
 * @param p_tData 
 * @return float
 *******************************************************************************/
float fHalSensor_analogUgM3O3Read(float *intemp, sensorData_t *p_tData);

/*******************************************************************************
 * @brief print measurements to serial output
 * 
 * @param data 
 * @param p_tPtr 
 *******************************************************************************/
void vHalSensor_printMeasurementsOnSerial(send_data_t *data, sensorData_t *p_tPtr);


/*****************************************************************************************************
 * @brief 
 * 
 * @param p_tErr 
 * @param p_tData 
 * @param number_of_measurements 
 *****************************************************************************************************/
void vHalSensor_performAverages(errorVars_t *p_tErr, sensorData_t *p_tData, deviceMeasurement_t *p_tMeas);


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
short sHalSensor_evaluateMSPIndex(sensorData_t *p_tData);

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
mspStatus_t tHalSensor_readMICS4514_ADC(DFRobot_MICS& mics4514, sensorData_t* p_tData);

/**
 * @brief Calculate gas concentrations from averaged MICS4514 ADC values
 * @param p_tData           Pointer to sensor data structure with calibration values and accumulated ADC
 * @param p_tMicsReading    Pointer to structure to store the calculated gas concentrations
 * @param measurement_count Number of measurements accumulated
 * @return mspStatus_t      STATUS_OK on success, STATUS_ERR on failure
 */
mspStatus_t tHalSensor_calculateMICS4514_Gases(sensorData_t* p_tData, MICS4514SensorReading_t* p_tMicsReading, int measurement_count);

/**
 * @brief Calculate immediate gas concentrations from current MICS4514 ADC reading (for display)
 * @param mics4514          Reference to DFRobot MICS4514 sensor object
 * @param p_tData           Pointer to sensor data structure with calibration values
 * @param p_tMicsReading    Pointer to structure to store the calculated gas concentrations
 * @return mspStatus_t      STATUS_OK on success, STATUS_ERR on failure
 */
mspStatus_t tHalSensor_calculateMICS4514_Immediate(DFRobot_MICS& mics4514, sensorData_t* p_tData, MICS4514SensorReading_t* p_tMicsReading);

/**
 * @brief Calculate gas concentrations from Rs/R0 ratios using MICS4514 datasheet formulas
 * @param rs_r0_red         Rs/R0 ratio for RED sensor (CO, NH3)
 * @param rs_r0_ox          Rs/R0 ratio for OX sensor (NO2)
 * @param p_tMicsReading    Pointer to structure to store the calculated gas concentrations
 * @return mspStatus_t      STATUS_OK on success, STATUS_ERR on failure
 */
mspStatus_t tHalSensor_calculateGasFromRsR0(const float rs_r0_red, const float rs_r0_ox, MICS4514SensorReading_t* p_tMicsReading);


#endif