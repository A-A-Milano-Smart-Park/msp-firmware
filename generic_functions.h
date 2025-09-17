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
void vGeneric_dspFloatToComma(const float value, char *buffer, const size_t bufferSize);

/*************************************************
 * @brief   converts float values in strings 
 *          with the decimal part separated 
 *          from the integer part by a comma
 * 
 * @param   value 
 * @return  String 
 ************************************************/
String vGeneric_floatToComma(const float value);

/************************************************************
 * @brief calculates ug/m3 from a gas ppm concentration
 * 
 * @param ppm 
 * @param mm 
 * @return float 
 ***********************************************************/
float vGeneric_convertPpmToUgM3(const float ppm, const float mm);


#endif