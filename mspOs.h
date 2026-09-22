/********************************************************
 * @file    mspOs.h
 * @author  AB-Engineering - https://ab-engineering.it
 * @brief   management for the Milano Smart Park project
 * @details This file contains functions to manage mutexes for thread safety in the project.
 * @version 0.1
 * @date    2025-07-25
 * 
 * @copyright Copyright (c) 2025
 * 
 ********************************************************/


#ifndef MSPOS_H
#define MSPOS_H

#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

bool bMspOs_initDataAccessMutex();
void vMspOs_takeDataAccessMutex();
void vMspOs_giveDataAccessMutex();

#endif // MSPOS_H