/************************************************************************************************
 * @file    firmware_update.h
 * @author  AB-Engineering - https://ab-engineering.it
 * @brief   Firmware update management for the Milano Smart Park project
 * @version 0.1
 * @date    2025-01-11
 *
 * @copyright Copyright (c) 2025
 *
 ************************************************************************************************/

#ifndef FIRMWARE_UPDATE_H
#define FIRMWARE_UPDATE_H

#include <Arduino.h>
#include "shared_values.h"

// Function declarations
bool bHalFirmware_checkForUpdates(systemData_t *sysData, systemStatus_t *sysStatus, deviceNetworkInfo_t *devInfo);
bool bHalFirmware_downloadToSDCard(const String &downloadUrl);
bool bHalFirmware_compareVersions(const String &currentVersion, const String &remoteVersion);
bool bHalFirmware_downloadBinaryFirmware(const String &downloadUrl, systemData_t *sysData, systemStatus_t *sysStatus, deviceNetworkInfo_t *devInfo);
bool bHalFirmware_performOTAUpdate(const String &firmwarePath);
bool bHalFirmware_forceOTAUpdate(systemData_t *sysData, systemStatus_t *sysStatus, deviceNetworkInfo_t *devInfo);

// SD Card firmware management functions
bool bHalFirmware_forceFlashFromSDCard(const char* firmwarePath);
bool bHalFirmware_validateAndFlashFromSD(const char* firmwarePath, bool forceUpdate);
bool bHalFirmware_checkVersionAndFlash(const char* firmwarePath);
String bHalFirmware_extractVersionFromFile(const char* firmwarePath);

// ESP-IDF OTA management functions
void vHalFirmware_printOTAInfo();
bool bHalFirmware_validateCurrentFirmware();
bool bHalFirmware_markFirmwareValid();
bool bHalFirmware_rollbackFirmware();
bool bHalFirmware_isRollbackAvailable();
bool bHalFirmware_checkAndApplyPendingUpdate(const char* firmwarePath);

#ifdef ENABLE_ENHANCED_SECURITY
// Enhanced Security functions (only available when ENABLE_ENHANCED_SECURITY is defined)
bool bHalFirmware_verifyFirmwareSignature(const String &firmwarePath);
bool bHalFirmware_verifyFirmwareHash(const String &firmwarePath, const String &expectedHash = "");
bool bHalFirmware_verifyDetachedSignature(const String &firmwarePath, const String &signatureFilePath);
#endif

#ifdef ENABLE_FIRMWARE_UPDATE_TESTS
// Test functions
void vHalFirmware_testVersionComparison();
void vHalFirmware_testGitHubAPI();
void vHalFirmware_testConfigParsing(systemStatus_t *sysStatus);
void vHalFirmware_testOTAManagement();
void vHalFirmware_testForceOTAUpdate(systemData_t *sysData, systemStatus_t *sysStatus, deviceNetworkInfo_t *devInfo);
bool bHalFirmware_forceUpdateFromGitHub(systemData_t *sysData, systemStatus_t *sysStatus, deviceNetworkInfo_t *devInfo);
#endif

#endif // FIRMWARE_UPDATE_H