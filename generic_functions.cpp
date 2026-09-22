/*******************************************************************************
 * @file    generic_functions.c
 * @author  AB-Engineering - https://ab-engineering.it
 * @brief   Generic functions for the Milano Smart Park project
 * @details This file contains generic functions used across the project.
 * @version 0.1
 * @date    2025-07-12
 * 
 * @copyright Copyright (c) 2025
 * 
 ******************************************************************************/

 // -- includes --
#include "stdio.h"
#include "math.h"
#include "generic_functions.h"
#include "shared_values.h"
#include "sensors.h"

#define LOC_STD_NUM_FMT   "%d,%02d"


/***************************************************************
 * @brief converts float values in strings,
 *          with the decimal part separated 
 *          from the integer part by a comma
 * 
 * @param value 
 * @param buffer 
 * @param bufferSize 
 ***************************************************************/
void vGeneric_dspFloatToComma(float value, char *buffer, size_t bufferSize)
{
  int intPart = (int)value;
  int decimalPart = (int) (fabs(value - intPart) * 100);
  snprintf(buffer,bufferSize,LOC_STD_NUM_FMT,intPart,decimalPart);
}

/**********************************************************
 * @brief   converts float values in strings 
 *          with the decimal part separated 
 *          from the integer part by a comma
 * 
 * @param   value 
 * @return  String 
 *********************************************************/
String vGeneric_floatToComma(float value)
{
  String convert = String(value, 3);
  convert.replace(STR_DOT, STR_COMMA);
  return convert;
}


/************************************************************
 * @brief calculates ug/m3 from a gas ppm concentration
 * 
 * @param ppm 
 * @param mm 
 * @return float 
 ***********************************************************/
float vGeneric_convertPpmToUgM3(float ppm, float mm)
{
  // Standard conversion: µg/m³ = ppm × (molecular_weight / molar_volume_at_STP) × conversion_factor
  return ppm * mm * PPM_TO_UGM3_FACTOR / MOLAR_VOLUME_STP;
}

/************************************************************
 * @brief performs safe floating point comparison with epsilon tolerance
 *
 * @param a         first floating point value
 * @param b         second floating point value
 * @param epsilon   tolerance for comparison (default: 0.001f)
 * @return true     if values are approximately equal within epsilon
 * @return false    if values differ by more than epsilon
 ***********************************************************/
bool bGeneric_floatEqual(float a, float b, float epsilon)
{
  return fabs(a - b) <= epsilon;
}

/************************************************************
 * @brief performs safe floating point greater-than comparison with epsilon tolerance
 *
 * @param a         first floating point value
 * @param b         second floating point value
 * @param epsilon   tolerance for comparison (default: 0.001f)
 * @return true     if a is greater than (b - epsilon)
 * @return false    otherwise
 ***********************************************************/
bool bGeneric_floatGreaterThan(float a, float b, float epsilon)
{
  return a >= (b - epsilon);
}

/************************************************************
 * @brief performs safe floating point less-than comparison with epsilon tolerance
 *
 * @param a         first floating point value
 * @param b         second floating point value
 * @param epsilon   tolerance for comparison (default: 0.001f)
 * @return true     if a is less than (b + epsilon)
 * @return false    otherwise
 ***********************************************************/
bool bGeneric_floatLessThan(float a, float b, float epsilon)
{
  return a <= (b + epsilon);
}

/************************************************************
 * @brief   returns the human-readable name of a gas sensor type.
 *          Single source of truth for every log/display/JSON label:
 *          add a new gas_sensor_type_t here and nowhere else.
 *
 * @param   gasSensorType  value of tSysStatus::gasSensorType
 * @return  const char*    sensor name, or "Unknown" if out of range
 ***********************************************************/
const char *pcGeneric_gasSensorName(uint8_t gasSensorType)
{
  switch (gasSensorType)
  {
  case GAS_SENSOR_MICS6814: return "MICS6814";
  case GAS_SENSOR_MICS4514: return "MICS4514";
  case GAS_SENSOR_MSP1:     return "MSP1";
  default:                  return "Unknown";
  }
}
