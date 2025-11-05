/******************************************************************************************************
 * @file    sdcard.h
 * @author  Refactored by AB-Engineering - https://ab-engineering.it
 * @brief   SD Card and File Management Functions for the Milano Smart Park project
 * @details Milano Smart Park Firmware
            Developed by Norman Mulinacci
            This code is usable under the terms and conditions of the
            GNU GENERAL PUBLIC LICENSE Version 3, 29 June 2007
 * @version 0.1
 * @date 2025-07-25
 * 
 * @copyright Copyright (c) 2025
 * 
 *****************************************************************************************************/

#ifndef SDCARD_H
#define SDCARD_H

// -- includes --
#include "shared_values.h"

// Legacy function removed - now using date-based logging with automatic file creation

/*******************************************************************************
 * @brief log to SD card 
 * 
 * @param data 
 * @param p_tSysData 
 * @param p_tSys 
 * @param p_tData 
 * @param p_tDev 
 ******************************************************************************/
void vHalSdcard_logToSD(send_data_t *data, systemData_t *p_tSysData, systemStatus_t *p_tSys, sensorData_t *p_tData, deviceNetworkInfo_t *p_tDev);

/**************************************************************
 * @brief Create date-based log path (YYYY/MM/DD.csv format)
 * 
 * @param timeInfo Time structure for the log entry
 * @return String Complete path for the log file
 *************************************************************/
String sHalSdcard_createDateBasedLogPath(const struct tm* timeInfo);

/**************************************************************
 * @brief Ensure directory path exists, create if necessary
 * 
 * @param dirPath Directory path to create
 * @return bool Success/failure
 *************************************************************/
bool bHalSdcard_ensureDirectoryExists(const String& dirPath);

/******************************************************
 * @brief read SD card 
 * 
 * @param p_tSys 
 * @param p_tDev 
 * @param p_tData 
 * @param pDev 
 * @param p_tSysData 
 *****************************************************/
void vHalSdcard_readSD(systemStatus_t *p_tSys,deviceNetworkInfo_t *p_tDev,sensorData_t *p_tData,deviceMeasurement_t *pDev,systemData_t *p_tSysData);

/********************************************************
 * @brief initialize SD card
 * 
 * @param p_tSys system status structure
 * @param p_tDev device network info structure  
 * @return uint8_t success/failure
 ********************************************************/
uint8_t initializeSD(systemStatus_t *p_tSys, deviceNetworkInfo_t *p_tDev);

/********************************************************
 * @brief check and parse configuration file
 * 
 * @param configpath path to config file
 * @param p_tDev device network info structure
 * @param p_tData sensor data structure  
 * @param pDev device measurement structure
 * @param p_tSys system status structure
 * @param p_tSysData system data structure
 * @return uint8_t success/failure
 ********************************************************/
uint8_t checkConfig(const char *configpath, deviceNetworkInfo_t *p_tDev, sensorData_t *p_tData, deviceMeasurement_t *pDev, systemStatus_t *p_tSys, systemData_t *p_tSysData);

/********************************************************
 * @brief Update system structures from server JSON response
 *
 * @param server_json JSON string from server with configuration updates
 * @param p_tDev device network info structure
 * @param p_tData sensor data structure
 * @param pDev device measurement structure
 * @param p_tSys system status structure
 * @param p_tSysData system data structure
 * @return bool success/failure
 ********************************************************/
bool bHalSdcard_updateFromServerConfig(const String &server_json, deviceNetworkInfo_t *p_tDev, sensorData_t *p_tData, deviceMeasurement_t *pDev, systemStatus_t *p_tSys, systemData_t *p_tSysData);

/********************************************************
 * @brief Write current system configuration to SD card config file
 *
 * @param p_tDev device network info structure
 * @param p_tData sensor data structure
 * @param pDev device measurement structure
 * @param p_tSys system status structure
 * @param p_tSysData system data structure
 * @return bool success/failure
 ********************************************************/
bool bHalSdcard_writeConfig(deviceNetworkInfo_t *p_tDev, sensorData_t *p_tData, deviceMeasurement_t *pDev, systemStatus_t *p_tSys, systemData_t *p_tSysData);

/********************************************************
 * @brief periodic SD card presence check
 * 
 * @param p_tSys system status structure
 * @param p_tDev device network info structure
 * @return uint8_t current SD card presence status
 ********************************************************/
uint8_t vHalSdcard_periodicCheck(systemStatus_t *p_tSys, deviceNetworkInfo_t *p_tDev);

#endif