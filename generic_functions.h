/**************************************************************************************
 * @file    generic_functions.h
 * @author  AB-Engineering - https://ab-engineering.it
 * @brief   Generic functions for the Milano Smart Park project
 * @details This file contains generic functions used across the project.
 * @version 0.1
 * @date 2025-07-12
 * 
 * @copyright Copyright (c) 2025
 * 
 *************************************************************************************/

#ifndef GENERIC_FUNCTIONS_H
#define GENERIC_FUNCTIONS_H

//-- includes --
#include <stddef.h>
#include <Arduino.h>

/*************************************************
 * @brief   converts float values in strings,
 *          with the decimal part separated 
 *          from the integer part by a comma
 * 
 * @param   value 
 * @return  String 
 *************************************************/
void vGeneric_dspFloatToComma(float value, char *buffer, size_t bufferSize);

/*************************************************
 * @brief   converts float values in strings 
 *          with the decimal part separated 
 *          from the integer part by a comma
 * 
 * @param   value 
 * @return  String 
 ************************************************/
String vGeneric_floatToComma(float value);

/************************************************************
 * @brief calculates ug/m3 from a gas ppm concentration
 * 
 * @param ppm 
 * @param mm 
 * @return float 
 ***********************************************************/
float vGeneric_convertPpmToUgM3(float ppm, float mm);

/************************************************************
 * @brief performs safe floating point comparison with epsilon tolerance
 *
 * @param a         first floating point value
 * @param b         second floating point value
 * @param epsilon   tolerance for comparison (default: 0.001f)
 * @return true     if values are approximately equal within epsilon
 * @return false    if values differ by more than epsilon
 ***********************************************************/
bool bGeneric_floatEqual(float a, float b, float epsilon = 0.001f);

/************************************************************
 * @brief performs safe floating point greater-than comparison with epsilon tolerance
 *
 * @param a         first floating point value
 * @param b         second floating point value
 * @param epsilon   tolerance for comparison (default: 0.001f)
 * @return true     if a is greater than (b - epsilon)
 * @return false    otherwise
 ***********************************************************/
bool bGeneric_floatGreaterThan(float a, float b, float epsilon = 0.001f);

/************************************************************
 * @brief performs safe floating point less-than comparison with epsilon tolerance
 *
 * @param a         first floating point value
 * @param b         second floating point value
 * @param epsilon   tolerance for comparison (default: 0.001f)
 * @return true     if a is less than (b + epsilon)
 * @return false    otherwise
 ***********************************************************/
bool bGeneric_floatLessThan(float a, float b, float epsilon = 0.001f);


#endif