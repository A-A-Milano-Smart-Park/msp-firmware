/***********************************************************************************************
 * @file    mspOs.cpp
 * @author  AB-Engineering - https://ab-engineering.it
 * @brief   management for the Milano Smart Park project
 * @details This file contains functions to manage mutexes for thread safety in the project.
 * @version 0.1
 * @date    2025-07-25
 *
 * @copyright Copyright (c) 2025
 *
 ***********************************************************************************************/

// -- includes --
#include "mspOs.h"

// -- global mutex handle
SemaphoreHandle_t dataAccessMutex = NULL;


/**************************************************
 * @brief Initializes the mutex for data access.
 * @return true on success, false if allocation failed.
 **************************************************/
bool bMspOs_initDataAccessMutex()
{
    dataAccessMutex = xSemaphoreCreateMutex();
    if (dataAccessMutex == NULL)
    {
        Serial.println("FATAL: failed to create data access mutex!");
        return false;
    }
    return true;
}

/**********************************************************
 * @brief   Takes (locks) the mutex for data access.
 *
 *********************************************************/
void vMspOs_takeDataAccessMutex()
{
    if (dataAccessMutex != NULL)
    {
        if (xSemaphoreTake(dataAccessMutex, pdMS_TO_TICKS(5000)) != pdTRUE)
        {
            log_e("dataAccessMutex timeout — possible deadlock!");
        }
    }
}

/**********************************************************
 * @brief  Releases (unlocks) the mutex for data access.
 *
 **********************************************************/
void vMspOs_giveDataAccessMutex()
{
    if (dataAccessMutex != NULL)
    {
        xSemaphoreGive(dataAccessMutex);
    }
}
