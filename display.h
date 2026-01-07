/********************************************************************
 * @file    display.h
 * @author  Refactored by AB-Engineering - https://ab-engineering.it
 * @brief   Milano Smart Park Firmware
            Developed by Norman Mulinacci
            
            This code is usable under the terms and conditions of the
            GNU GENERAL PUBLIC LICENSE Version 3, 29 June 2007

 * @version 0.1
 * @date    2025-07-25
 * 
 * @copyright Copyright (c) 2025
 * 
 ***************************************************************/

#ifndef DISPLAY_H
#define DISPLAY_H

// -- includes --
#include "shared_values.h"
#include "display_task.h"

/******************************************************
 * @brief function to initialize the serial and I2C display.
 * 
 ******************************************************/
void vHalDisplay_initSerialAndI2c(void);

/******************************************************
 * @brief  draws the boot screen on the U8G2 display
 * 
 * @param fwver 
 *****************************************************/
void vHalDisplay_DrawBoot(String *fwver, const uint8_t screenDelay);

/*******************************************************
* @brief draws a text line on the U8G2 display
* 
* @param message 
* @param secdelay 
* @param statPtr 
* @param devinfoPtr 
*******************************************************/
void vHalDisplay_drawLine(const char message[], short secdelay,systemStatus_t *statPtr, deviceNetworkInfo_t *devinfoPtr);


/********************************************************
 * @brief draws two text lines on the U8G2 display
 * 
 * @param message1 
 * @param message2 
 * @param secdelay 
 * @param statPtr 
 * @param devinfoPtr 
 ********************************************************/
void vHalDisplay_drawTwoLines(const char message1[], const char message2[], short secdelay, systemStatus_t *statPtr, deviceNetworkInfo_t *devinfoPtr);

/*******************************************************
 * @brief draws a countdown on the U8G2 display
 * 
 * @param startsec 
 * @param message 
 * @param statPtr 
 * @param devinfoPtr 
 *******************************************************/
void vHalDisplay_drawCountdown(short startsec, const char message[],systemStatus_t *statPtr, deviceNetworkInfo_t *devinfoPtr);

/*********************************************************************************
 * @brief function to draw the MICS6814 sensor values on the display.
 * 
 * @param redval 
 * @param oxval 
 * @param nh3val 
 * @param statPtr 
 * @param devinfoPtr 
 *********************************************************************************/
void vHalDisplay_drawMicsValues(uint16_t redval, uint16_t oxval, uint16_t nh3val,systemStatus_t *statPtr, deviceNetworkInfo_t *devinfoPtr);

/*********************************************************************************
 * @brief function to draw the firmware version on the display.
 *
 * @param statPtr
 * @param devinfoPtr
 *********************************************************************************/
void vHalDisplay_drawFirmwareVersion(systemStatus_t *statPtr, deviceNetworkInfo_t *devinfoPtr, const uint8_t secDelay);

/*********************************************************************************
 * @brief function to draw the configuration values from SD card on the display.
 *
 * @param p_tData
 * @param statPtr
 * @param devinfoPtr
 * @param measPtr
 * @param secDelay
 *********************************************************************************/
void vHalDisplay_drawConfigurationValues(sensorData_t *p_tData, systemStatus_t *statPtr, deviceNetworkInfo_t *devinfoPtr, deviceMeasurement_t *measPtr, const uint8_t secDelay);

/*********************************************************************************
 * @brief function to draw the BME680 gas sensor data on the display.
 * 
 * @param p_tData 
 * @param statPtr 
 * @param devinfoPtr 
 ********************************************************************************/
void vHalDisplay_drawBme680GasSensorData(sensorData_t *p_tData,systemStatus_t *statPtr, deviceNetworkInfo_t *devinfoPtr, short secdelay);

/*********************************************************************************
 * @brief function to draw the PMS5003 air quality sensor data on the display.
 * 
 * @param p_tData 
 * @param statPtr 
 * @param devinfoPtr 
 *********************************************************************************/
void vHalDisplay_drawPMS5003AirQualitySensorData(sensorData_t *p_tData,systemStatus_t *statPtr, deviceNetworkInfo_t *devinfoPtr, short secdelay);

/*******************************************************************************************
 * @brief function to draw the MICS6814 pollution sensor data on the display.
 * 
 * @param p_tData 
 * @param statPtr 
 * @param devinfoPtr 
 ******************************************************************************************/
void vHalDisplay_drawMICSxx14PollutionSensorData(sensorData_t *p_tData,systemStatus_t *statPtr, deviceNetworkInfo_t *devinfoPtr, short secdelay);

/******************************************************************************************
 * @brief function to draw the Ozone sensor data on the display.
 * 
 * @param p_tData 
 * @param statPtr 
 * @param devinfoPtr 
 *****************************************************************************************/
void vHalDisplay_drawOzoneSensorData(sensorData_t *p_tData,systemStatus_t *statPtr, deviceNetworkInfo_t *devinfoPtr, short secdelay);

/******************************************************************************************
 * @brief draws the MSP index data on the display.
 * 
 * @param p_tData 
 * @param statPtr 
 * @param devinfoPtr 
 * @param secdelay 
 ******************************************************************************************/
void vHalDisplay_drawMspIndexData(sensorData_t *p_tData,systemStatus_t *statPtr, deviceNetworkInfo_t *devinfoPtr, short secdelay);

/******************************************************************************************
 * @brief Update display status with network events
 * 
 * @param devInfo Device network info structure
 * @param sysStatus System status structure  
 * @param event Display event to show
 ******************************************************************************************/
void updateDisplayStatus(deviceNetworkInfo_t *devInfo, systemStatus_t *sysStatus, displayEvents_t event);

/******************************************************************************************
 * @brief Updates display data and sends event to display task queue
 * 
 * @param event Display event to process and send
 * @param sensorData Sensor data structure
 * @param devInfo Device network info structure
 * @param measStat Measurement statistics structure
 * @param sysData System data structure
 * @param sysStat System status structure
 ******************************************************************************************/
void vMsp_updateDataAndSendEvent(displayEvents_t event, 
                                sensorData_t *sensorData,
                                deviceNetworkInfo_t *devInfo,
                                deviceMeasurement_t *measStat,
                                systemData_t *sysData,
                                systemStatus_t *sysStat);

#endif