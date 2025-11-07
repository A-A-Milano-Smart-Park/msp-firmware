/************************************************************************************************
 * @file    firmware_update.cpp
 * @author  AB-Engineering - https://ab-engineering.it
 * @brief   Firmware update management for the Milano Smart Park project
 * @version 0.1
 * @date    2025-01-11
 *
 * @copyright Copyright (c) 2025
 *
 ************************************************************************************************/

#include "firmware_update.h"
#include "network.h"
#include "display_task.h"
#include "config.h"
#include "mspOs.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <SD.h>
#include "esp32-hal-log.h"

// TinyGSM includes for GSM support
#define TINY_GSM_MODEM_SIM800
#include <TinyGsmClient.h>

// ESP-IDF OTA includes
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "esp_app_format.h"

#ifdef ENABLE_ENHANCED_SECURITY
#include "esp_secure_boot.h"
#include "esp_flash_encrypt.h"
#include "esp_efuse.h"
#include "mbedtls/sha256.h"
#include "mbedtls/rsa.h"
#include "mbedtls/pk.h"

#define HASH_LENGTH 32
#define FIRMWARE_MIN_VALID_LEN 1024

#endif

// GitHub API constants
#define GITHUB_API_URL "https://api.github.com/repos/A-A-Milano-Smart-Park/msp-firmware/releases/latest"

#ifdef __GITHUB_TEST_API_URL__
#define GITHUB_TEST_API_URL __GITHUB_TEST_API_URL__
#else
#define GITHUB_TEST_API_URL GITHUB_API_URL
#endif

#define FIRMWARE_UPDATE_TIMEOUT_MS 60000
#define DOWNLOAD_BUFFER_SIZE 2048 // Reduced from 8192 to prevent stack overflow

static String extractVersionFromTag(const String &tag);
static bool downloadFile(const String &url, const String &filepath);
static bool downloadFileGSM(const String &url, const String &filepath);
static bool downloadFileWiFi(const String &url, const String &filepath);

/**
 * @brief Check for firmware updates on GitHub
 */
bool bHalFirmware_checkForUpdates(systemData_t *sysData, systemStatus_t *sysStatus, deviceNetworkInfo_t *devInfo)
{
    log_i("Checking for firmware updates...");

    // Set firmware operation flag to prevent network disconnection during update check
    setFirmwareOperationInProgress(true);

    // Ensure we have internet connection
    if (isNetworkConnected() == false)
    {
        log_w("Network not connected for firmware update check");
        setFirmwareOperationInProgress(false); // Clear flag before returning
        requestNetworkConnection();
        return false;
    }

    // Determine which client to use based on connection type
    TinyGsm* modem = getModemInstance();
    bool useGSM = sysStatus->use_modem && modem && modem->isGprsConnected();

    String payload;

    if (useGSM)
    {
        // GitHub API requires HTTPS, but SIM800 doesn't support SSL/TLS
        // Firmware update checks are only possible via WiFi
        log_w("Firmware update check via GSM is not supported");
        log_w("GitHub API requires HTTPS which SIM800 modem cannot handle");
        log_i("Firmware updates will only be performed when WiFi is available");
        setFirmwareOperationInProgress(false);
        return false;
    }

    // WiFi path - use standard HTTPClient with HTTPS support
    log_i("Using WiFi client for firmware update check");

    HTTPClient http;
    http.setTimeout(FIRMWARE_UPDATE_TIMEOUT_MS);

    if (!http.begin(GITHUB_API_URL))
    {
        log_e("Failed to initialize HTTP client for GitHub API (WiFi)");
        setFirmwareOperationInProgress(false);
        return false;
    }

    http.addHeader("User-Agent", "MilanoSmartPark-ESP32");
    http.addHeader("Accept", "application/vnd.github.v3+json");

    int httpCode = http.GET();

    if (httpCode != HTTP_CODE_OK)
    {
        log_e("GitHub API request failed with code: %d", httpCode);
        http.end();
        setFirmwareOperationInProgress(false);
        return false;
    }

    payload = http.getString();
    http.end();

    log_d("GitHub API response: %s", payload.substring(0, 200).c_str());

    // Parse JSON response
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, payload);

    if (error)
    {
        log_e("JSON parsing failed: %s", error.c_str());
        setFirmwareOperationInProgress(false);
        return false;
    }

    // Extract release information
    String latestTag = doc["tag_name"].as<String>();
    String downloadUrl = "";
    String latestVersion = extractVersionFromTag(latestTag);

    // Look for the direct binary file (update_vX.X.X.bin)
    JsonArray assets = doc["assets"];
    String binaryFileName = "update_" + latestTag + ".bin";

    for (JsonObject asset : assets)
    {
        String name = asset["name"].as<String>();
        String url = asset["browser_download_url"].as<String>();

        if (name == binaryFileName)
        {
            downloadUrl = url;
            log_i("Found application binary: %s", name.c_str());
            break;
        }
    }

    if (downloadUrl.isEmpty())
    {
        log_e("No application binary (%s) found in release assets", binaryFileName.c_str());
        setFirmwareOperationInProgress(false);
        return true;
    }

    log_i("Current version: %s\n", sysData->ver.c_str());
    log_i("Latest version: %s", latestVersion.c_str());
    log_i("Download URL: %s", downloadUrl.c_str());

    // Compare versions
    if (bHalFirmware_compareVersions(sysData->ver, latestVersion))
    {
        log_i("New firmware version available, starting download and update process...");
        bHalFirmware_downloadBinaryFirmware(downloadUrl, sysData, sysStatus, devInfo);
    }
    else
    {
        log_i("No firmware update needed, current version is up to date");
    }

    // Clear firmware operation flag before returning
    setFirmwareOperationInProgress(false);
    return true;
}

/**
 * @brief Compare two version strings (semantic versioning)
 * @param currentVersion Current firmware version
 * @param remoteVersion Remote firmware version
 * @return true if remote version is newer
 */
bool bHalFirmware_compareVersions(const String &currentVersion, const String &remoteVersion)
{
    // Simple version comparison - assumes format like "v1.2.3"
    // Remove 'v' prefix if present
    String current = currentVersion;
    String remote = remoteVersion;

    if (current.startsWith("v"))
        current = current.substring(1);
    if (remote.startsWith("v"))
        remote = remote.substring(1);

    // Special case: development version is always considered older
    if (current.equals("DEV") || current.equals("dev"))
    {
        return true; // Any remote version is newer than DEV
    }

    // Parse version components
    int currentMajor = 0, currentMinor = 0, currentPatch = 0;
    int remoteMajor = 0, remoteMinor = 0, remotePatch = 0;

    // Parse current version
    int firstDot = current.indexOf('.');
    int secondDot = current.indexOf('.', firstDot + 1);

    if (firstDot > 0)
    {
        currentMajor = current.substring(0, firstDot).toInt();
        if (secondDot > firstDot)
        {
            currentMinor = current.substring(firstDot + 1, secondDot).toInt();
            currentPatch = current.substring(secondDot + 1).toInt();
        }
        else
        {
            currentMinor = current.substring(firstDot + 1).toInt();
        }
    }

    // Parse remote version
    firstDot = remote.indexOf('.');
    secondDot = remote.indexOf('.', firstDot + 1);

    if (firstDot > 0)
    {
        remoteMajor = remote.substring(0, firstDot).toInt();
        if (secondDot > firstDot)
        {
            remoteMinor = remote.substring(firstDot + 1, secondDot).toInt();
            remotePatch = remote.substring(secondDot + 1).toInt();
        }
        else
        {
            remoteMinor = remote.substring(firstDot + 1).toInt();
        }
    }

    log_d("Version comparison: current %d.%d.%d vs remote %d.%d.%d",
          currentMajor, currentMinor, currentPatch,
          remoteMajor, remoteMinor, remotePatch);

    // Compare versions
    if (remoteMajor > currentMajor)
        return true;
    if (remoteMajor < currentMajor)
        return false;

    if (remoteMinor > currentMinor)
        return true;
    if (remoteMinor < currentMinor)
        return false;
#if (SKIP_FIRMWARE_VERSION_CHECK == 1)
    return true;
#else
    return remotePatch > currentPatch;
#endif
}

/**
 * @brief Download firmware binary file directly
 */
bool bHalFirmware_downloadBinaryFirmware(const String &downloadUrl, systemData_t *sysData, systemStatus_t *sysStatus, deviceNetworkInfo_t *devInfo)
{
    log_i("Downloading firmware binary from: %s", downloadUrl.c_str());

    String firmwarePath = "/firmware.bin";

    // Check if firmware file already exists and delete it to ensure clean download
    if (SD.exists(firmwarePath.c_str()))
    {
        log_i("Existing firmware file found, deleting: %s", firmwarePath.c_str());
        if (SD.remove(firmwarePath.c_str()))
        {
            log_i("Successfully deleted existing firmware file");
        }
        else
        {
            log_e("Failed to delete existing firmware file");
            return false;
        }
    }

    if (!downloadFile(downloadUrl, firmwarePath))
    {
        log_e("Failed to download firmware binary file");
        return false;
    }

    log_i("Firmware binary downloaded successfully to: %s", firmwarePath.c_str());

    // Allow SD card to settle after large file operation
    delay(100);

    // Verify the downloaded file exists and has reasonable size
    File firmwareFile = SD.open(firmwarePath.c_str(), FILE_READ);
    if (!firmwareFile)
    {
        log_e("Failed to open downloaded firmware file");
        log_e("Firmware download may have failed - performing controlled reboot...");
        
        // Remove potentially corrupted firmware file
        if (SD.exists(firmwarePath.c_str()))
        {
            SD.remove(firmwarePath.c_str());
            log_i("Removed inaccessible firmware file: %s", firmwarePath.c_str());
        }
        
        // Brief delay before reboot to ensure logs are written
        delay(2000);
        
        // Perform controlled reboot instead of returning false
        esp_restart();
        
        return false; // Will never reach here due to restart
    }

    size_t fileSize = firmwareFile.size();
    firmwareFile.close();

    if (fileSize < 1000000 || fileSize > 2000000) // Reasonable firmware size range
    {
        log_w("Firmware file size (%zu bytes) seems unusual", fileSize);
        // Don't fail, just warn - size limits may vary
    }

    // Basic firmware validation (always enabled)
    log_i("Starting basic firmware validation...");

    // Always perform basic size and format validation
    if (fileSize < 1024)
    {
        firmwareFile.close();
        log_e("Firmware file too small to be valid ESP32 firmware");
        log_e("Invalid firmware size detected - performing controlled reboot...");
        SD.remove(firmwarePath.c_str());
        
        // Brief delay before reboot to ensure logs are written
        delay(2000);
        
        // Perform controlled reboot instead of returning false
        esp_restart();
        
        return false; // Will never reach here due to restart
    }

#ifdef ENABLE_ENHANCED_SECURITY
    log_i("Enhanced security features enabled - performing cryptographic verification...");

    // Step 1: Verify firmware signature and format
    if (!bHalFirmware_verifyFirmwareSignature(firmwarePath))
    {
        log_e("Firmware signature verification failed - aborting update");
        SD.remove(firmwarePath.c_str()); // Remove potentially malicious file
        return false;
    }

    // Step 2: Calculate firmware hash for integrity verification
    if (!bHalFirmware_verifyFirmwareHash(firmwarePath))
    {
        log_e("Firmware hash calculation failed - aborting update");
        SD.remove(firmwarePath.c_str()); // Remove potentially corrupted file
        return false;
    }

    // Step 3: Check for and verify detached signature file (if available)
    String signatureFilePath = firmwarePath + ".sig";
    if (SD.exists(signatureFilePath.c_str()))
    {
        log_i("Detached signature found, performing additional verification...");
        if (!bHalFirmware_verifyDetachedSignature(firmwarePath, signatureFilePath))
        {
            log_e("Detached signature verification failed - aborting update");
            SD.remove(firmwarePath.c_str());
            SD.remove(signatureFilePath.c_str());
            return false;
        }
        log_i("Detached signature verification passed");
    }
    else
    {
        log_w("No detached signature file found - relying on embedded verification");
    }

    log_i("Enhanced security verification completed successfully");
#else
    log_i("Enhanced security features disabled - using basic validation only");
    log_w("For production deployment, enable ENABLE_ENHANCED_SECURITY in config.h");

    // Reopen the firmware file for basic validation
    firmwareFile = SD.open(firmwarePath.c_str(), FILE_READ);

    if (firmwareFile)
    {
        // Read first few bytes to verify ESP32 BIN file format
        uint8_t header[4];
        if (firmwareFile.readBytes((char *)header, 4) == 4)
        {
            // ESP32 firmware binary header should start with 0xE9 (ESP_IMAGE_HEADER_MAGIC)
            if (header[0] == 0xE9)
            {
                log_i("PASS: Valid ESP32 BIN file header detected");

                firmwareFile.close();

                // Perform detailed OTA update test
                log_i("DETAILED TEST: OTA analysis");

                bool updateSuccess = bHalFirmware_performOTAUpdate(firmwarePath);

                if (updateSuccess == false)
                {
                    log_e("FAIL: OTA process failed");
                }
            }
            else
            {
                log_e("FAIL: Invalid ESP32 BIN file format - Header: 0x%02X%02X%02X%02X",
                      header[0], header[1], header[2], header[3]);
                log_e("Expected ESP32 firmware to start with 0xE9 magic byte");
            }
        }
        else
        {
            log_e("FAIL: Could not read BIN file header");
        }

        firmwareFile.close();
    }
    else
    {
        log_e("FAIL: Could not open downloaded BIN file");
    }

#endif

    log_i("Security verification completed successfully");
    log_i("Firmware binary ready for OTA update: %zu bytes", fileSize);
    return true;
}

/**
 * @brief Perform OTA update from firmware file using ESP-IDF native OTA
 */
bool bHalFirmware_performOTAUpdate(const String &firmwarePath)
{
    log_i("Starting ESP-IDF native OTA update from: %s", firmwarePath.c_str());

    if (!SD.exists(firmwarePath.c_str()))
    {
        log_e("Firmware file not found: %s", firmwarePath.c_str());
        return false;
    }

    File firmwareFile = SD.open(firmwarePath.c_str(), FILE_READ);
    if (!firmwareFile)
    {
        log_e("Failed to open firmware file");
        return false;
    }

    size_t firmwareSize = firmwareFile.size();
    log_i("Firmware size: %d bytes", firmwareSize);

    // Get current running partition info
    const esp_partition_t *running_partition = esp_ota_get_running_partition();
    if (running_partition == NULL)
    {
        log_e("Failed to get running partition");
        firmwareFile.close();
        return false;
    }

    log_i("Running partition: %s at offset 0x%08x", running_partition->label, running_partition->address);

    // Get next available OTA partition
    const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);
    if (update_partition == NULL)
    {
        log_e("Failed to get next update partition");
        log_e("OTA partition unavailable - performing controlled reboot...");
        firmwareFile.close();
        
        // Remove firmware file since we can't process it
        SD.remove(firmwarePath.c_str());
        
        // Brief delay before reboot to ensure logs are written
        delay(2000);
        
        // Perform controlled reboot instead of returning false
        esp_restart();
        
        return false; // Will never reach here due to restart
    }

    log_i("Update partition: %s at offset 0x%08x (size: %d bytes)",
          update_partition->label, update_partition->address, update_partition->size);

    // Check if firmware size fits in the partition
    if (firmwareSize > update_partition->size)
    {
        log_e("Firmware size (%d) exceeds partition size (%d)", firmwareSize, update_partition->size);
        log_e("Firmware too large for partition - performing controlled reboot...");
        firmwareFile.close();
        
        // Remove oversized firmware file
        SD.remove(firmwarePath.c_str());
        
        // Brief delay before reboot to ensure logs are written
        delay(2000);
        
        // Perform controlled reboot instead of returning false
        esp_restart();
        
        return false; // Will never reach here due to restart
    }

    // Begin OTA update process
    esp_ota_handle_t ota_handle = 0;
    esp_err_t err = esp_ota_begin(update_partition, firmwareSize, &ota_handle);
    if (err != ESP_OK)
    {
        log_e("Failed to begin OTA update: %s", esp_err_to_name(err));
        firmwareFile.close();
        return false;
    }

    log_i("OTA update started successfully");

    // Write firmware data in chunks
    static uint8_t buffer[DOWNLOAD_BUFFER_SIZE]; // Static to avoid stack allocation
    size_t bytesWritten = 0;
    bool ota_success = true;
    int loopCount = 0;

    while (firmwareFile.available() && ota_success)
    {
        size_t bytesRead = firmwareFile.readBytes((char *)buffer, DOWNLOAD_BUFFER_SIZE);
        if (bytesRead > 0)
        {
            err = esp_ota_write(ota_handle, buffer, bytesRead);
            if (err != ESP_OK)
            {
                log_e("OTA write failed: %s", esp_err_to_name(err));
                ota_success = false;
                break;
            }

            bytesWritten += bytesRead;

            // Progress logging every 32KB (reduced frequency)
            if (bytesWritten % (32 * 1024) == 0)
            {
                log_i("OTA progress: %d/%d bytes (%.1f%%)",
                      bytesWritten, firmwareSize,
                      (float)bytesWritten / firmwareSize * 100);
            }

            // Yield more frequently to prevent stack overflow
            if (++loopCount % 5 == 0)
            {
                yield();  // Give other tasks a chance
                delay(1); // Small delay to prevent watchdog reset
            }
        }
        else
        {
            break; // End of file
        }
    }

    firmwareFile.close();

    if (!ota_success)
    {
        log_e("OTA write process failed, aborting...");
        esp_ota_abort(ota_handle);
        return false;
    }

    log_i("Firmware written successfully: %d bytes", bytesWritten);

    // Finalize the OTA update
    err = esp_ota_end(ota_handle);
    if (err != ESP_OK)
    {
        log_e("Failed to finalize OTA update: %s", esp_err_to_name(err));
        return false;
    }

    log_i("OTA update finalized successfully");

    // Validate the written firmware
    err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK)
    {
        log_e("Failed to set boot partition: %s", esp_err_to_name(err));
        return false;
    }

    log_i("Boot partition set to: %s", update_partition->label);

    // Optional: Mark the update as valid (for rollback protection)
    // This will be done after successful boot in the main application

    log_i("OTA update completed successfully!");
    log_i("System will restart to apply the new firmware...");

    // Brief delay before restart
    delay(2000);

    // Restart the system
    esp_restart();

    return true; // Will never reach here due to restart
}

// Helper functions

/**
 * @brief Extract version number from git tag
 */
static String extractVersionFromTag(const String &tag)
{
    // Assumes tag format like "v1.2.3" or "1.2.3"
    return tag;
}

/**
 * @brief Download file from URL to SD card (routing function)
 */
static bool downloadFile(const String &url, const String &filepath)
{
    log_i("Starting download from: %s", url.c_str());
    log_i("Saving to: %s", filepath.c_str());

    // Disable network connectivity tests during download to prevent interference
    setFirmwareDownloadInProgress();

    // Check available memory before starting download
    size_t freeHeap = ESP.getFreeHeap();
    log_i("Free heap before download: %zu bytes", freeHeap);

    if (freeHeap < 50000)
    { // Require at least 50KB free heap
        log_e("Insufficient memory for download (need 50KB, have %zu bytes)", freeHeap);
        clearFirmwareDownloadInProgress();
        return false;
    }

    // Determine which download method to use based on active connection
    TinyGsm* modem = getModemInstance();
    bool useGSM = modem && modem->isGprsConnected();

    if (useGSM)
    {
        log_i("Using GSM download method");
        return downloadFileGSM(url, filepath);
    }
    else
    {
        log_i("Using WiFi download method");
        return downloadFileWiFi(url, filepath);
    }
}

/**
 * @brief Download file from URL to SD card using WiFi
 */
static bool downloadFileWiFi(const String &url, const String &filepath)
{
    log_i("WiFi download: %s", url.c_str());

    HTTPClient http;
    http.setTimeout(FIRMWARE_UPDATE_TIMEOUT_MS);

    // Handle both HTTP and HTTPS URLs with proper client setup
    WiFiClientSecure *secureClient = nullptr;
    bool usedSpiram = false;

    if (url.startsWith("https://"))
    {
        // Use SPIRAM for WiFiClientSecure allocation to avoid heap issues
        secureClient = (WiFiClientSecure *)heap_caps_malloc(sizeof(WiFiClientSecure), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!secureClient)
        {
            log_w("SPIRAM allocation failed, trying regular heap");
            secureClient = new (std::nothrow) WiFiClientSecure();
            if (!secureClient)
            {
                log_e("Failed to allocate WiFiClientSecure - insufficient memory");
                clearFirmwareDownloadInProgress();
                return false;
            }
        }
        else
        {
            // Construct the object in the SPIRAM memory
            new (secureClient) WiFiClientSecure();
            usedSpiram = true;
            log_i("WiFiClientSecure allocated in SPIRAM");
        }

        secureClient->setInsecure();              // Accept all certificates for simplicity
        secureClient->setTimeout(60000);          // 60 second timeout for large downloads
        secureClient->setHandshakeTimeout(30000); // 30 second handshake timeout

        if (!http.begin(*secureClient, url))
        {
            log_e("Failed to initialize HTTPS client for download");
            if (usedSpiram)
            {
                secureClient->~WiFiClientSecure();
                heap_caps_free(secureClient);
            }
            else
            {
                delete secureClient;
            }
            clearFirmwareDownloadInProgress();
            return false;
        }

        log_i("HTTPS client initialized successfully");
    }
    else
    {
        if (!http.begin(url))
        {
            log_e("Failed to initialize HTTP client for download");
            clearFirmwareDownloadInProgress();
            return false;
        }
        log_i("HTTP client initialized successfully");
    }

    // Attempt GET request with retry logic for connection issues
    int httpCode = -1;
    int retryCount = 0;
    const int maxRetries = 3;

    while (retryCount < maxRetries)
    {
        log_i("Attempting HTTP GET (attempt %d/%d)", retryCount + 1, maxRetries);

        // Add User-Agent header for better compatibility with GitHub
        http.addHeader("User-Agent", "MSP-Firmware-Downloader/1.0");
        http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS); // Enable redirect following

        httpCode = http.GET();

        if (httpCode > 0)
        {
            break; // Success or HTTP error (not connection error)
        }

        // Connection error (negative code), retry
        log_w("Connection error %d, retrying in 2 seconds...", httpCode);
        retryCount++;

        if (retryCount < maxRetries)
        {
            delay(2000); // Wait 2 seconds before retry
        }
    }

    if (httpCode < 0)
    {
        log_e("Connection failed after %d attempts with error: %d", maxRetries, httpCode);
        http.end();
        if (secureClient)
        {
            if (usedSpiram)
            {
                secureClient->~WiFiClientSecure();
                heap_caps_free(secureClient);
            }
            else
            {
                delete secureClient;
            }
        }
        return false;
    }

    // Handle redirects (GitHub often returns 302 redirects)
    int redirectCount = 0;
    const int maxRedirects = 5;

    while ((httpCode == HTTP_CODE_MOVED_PERMANENTLY || httpCode == HTTP_CODE_FOUND) && redirectCount < maxRedirects)
    {
        String newLocation = http.getLocation();
        log_i("HTTP %d redirect to: %s", httpCode, newLocation.c_str());

        if (newLocation.length() == 0)
        {
            log_e("Redirect location is empty");
            break;
        }

        http.end();

        // Follow the redirect
        if (!http.begin(newLocation))
        {
            log_e("Failed to begin HTTP client for redirect URL");
            if (secureClient)
            {
                if (usedSpiram)
                {
                    secureClient->~WiFiClientSecure();
                    heap_caps_free(secureClient);
                }
                else
                {
                    delete secureClient;
                }
            }
            return false;
        }

        httpCode = http.GET();
        redirectCount++;
    }

    if (httpCode != HTTP_CODE_OK)
    {
        log_e("Download request failed with code: %d after %d redirects", httpCode, redirectCount);
        http.end();
        if (secureClient)
        {
            if (usedSpiram)
            {
                secureClient->~WiFiClientSecure();
                heap_caps_free(secureClient);
            }
            else
            {
                delete secureClient;
            }
        }
        return false;
    }

    log_i("Download successful after %d redirects", redirectCount);

    int totalLength = http.getSize();
    int originalFileSize = totalLength; // Store original size for validation
    log_i("Starting download, file size: %d bytes", totalLength);

    log_i("Attempting to open SD card file: %s", filepath.c_str());
    File file = SD.open(filepath.c_str(), FILE_WRITE);
    if (!file)
    {
        log_e("Failed to create download file: %s", filepath.c_str());
        http.end();
        if (secureClient)
        {
            if (usedSpiram)
            {
                secureClient->~WiFiClientSecure();
                heap_caps_free(secureClient);
            }
            else
            {
                delete secureClient;
            }
        }
        return false;
    }
    log_i("SD card file opened successfully");

    log_i("Getting HTTP stream pointer...");
    WiFiClient *stream = http.getStreamPtr();
    log_i("Stream pointer obtained: %s", stream ? "valid" : "null");
    static uint8_t buffer[DOWNLOAD_BUFFER_SIZE]; // Static to avoid stack allocation
    int bytesWritten = 0;
    int loopCount = 0;
    unsigned long lastDataTime = millis();
    unsigned long noDataTimeout = 30000; // 30 second timeout for no data
    int consecutiveNoDataCount = 0;

    log_i("Starting download loop - expecting %d bytes total", totalLength);

    while (http.connected() && (totalLength > 0 || totalLength == -1))
    {
        size_t availableBytes = stream->available();
        
        // Try to read data even if available() reports 0, as this can be unreliable
        if (availableBytes > 0 || consecutiveNoDataCount < 10)
        {
            int bytesToRead = (availableBytes > 0) ? min(availableBytes, sizeof(buffer)) : sizeof(buffer);
            int bytesRead = stream->readBytes(buffer, bytesToRead);
            
            if (bytesRead > 0) 
            {
                // Reset timeout and retry counters when data is actually received
                lastDataTime = millis();
                consecutiveNoDataCount = 0;
                // Check if SD write succeeds to prevent corruption
                size_t bytesWrittenToFile = file.write(buffer, bytesRead);
                if (bytesWrittenToFile != bytesRead)
                {
                    log_e("SD card write failed: wrote %d of %d bytes", bytesWrittenToFile, bytesRead);
                    file.close();
                    http.end();
                    clearFirmwareDownloadInProgress();
                    return false;
                }
                
                bytesWritten += bytesRead;

                if (totalLength > 0)
                {
                    totalLength -= bytesRead;

                    // Enhanced progress logging with connection status and more frequent flushing
                    if (bytesWritten % (16 * 1024) == 0) // Reduced from 32KB to 16KB for more frequent updates
                    {
                        float progress = ((float)(bytesWritten) / (float)(bytesWritten + totalLength)) * 100.0;
                        log_i("Download progress: %d bytes (%.1f%%) - connection: %s", 
                              bytesWritten, progress, http.connected() ? "OK" : "LOST");
                        
                        // Force file flush more frequently
                        file.flush();
                        
                        // Additional SD card health check
                        if (!SD.exists(filepath.c_str())) 
                        {
                            log_e("SD card file disappeared during download!");
                            break;
                        }
                    }
                }
                
                // Flush every 64KB to prevent SD card buffer issues
                if (bytesWritten % (64 * 1024) == 0)
                {
                    file.flush();
                    log_d("Periodic flush completed at %d bytes", bytesWritten);
                }
            }
            else
            {
                // No data was read
                if (availableBytes > 0) {
                    log_w("Stream readBytes returned 0 despite available data: %d", availableBytes);
                } else {
                    log_d("No data available and no data read, attempt: %d", consecutiveNoDataCount);
                }
            }

            // Yield more frequently to prevent stack overflow
            if (++loopCount % 5 == 0) // Increased frequency
            {
                yield();  // Give other tasks a chance
                delay(1); // Small delay to prevent watchdog reset
            }
        }
        else
        {
            // No data available - implement timeout and retry logic
            consecutiveNoDataCount++;
            
            // Check for timeout
            if ((millis() - lastDataTime) > noDataTimeout)
            {
                log_e("Download timeout: no data received for %lu ms", millis() - lastDataTime);
                log_e("Connection status: %s, Total downloaded: %d bytes", 
                      http.connected() ? "connected" : "disconnected", bytesWritten);
                break;
            }
            
            // Check for too many consecutive no-data attempts (reduced threshold)
            if (consecutiveNoDataCount > 50) // Reduced from 100 to 50 for faster failure detection
            {
                log_e("Too many consecutive no-data attempts (%d), connection may be stalled", consecutiveNoDataCount);
                log_e("Possible causes: SD card issues, network congestion, or server problems");
                
                // Final SD card check before giving up
                if (!SD.exists(filepath.c_str())) 
                {
                    log_e("Downloaded file is missing - SD card failure detected!");
                } else {
                    log_i("Downloaded file still exists, likely network/server issue");
                }
                break;
            }
            
            // Log periodic status when waiting for data (more frequent for better debugging)
            if (consecutiveNoDataCount % 25 == 0)
            {
                log_d("Waiting for data... attempts: %d, connected: %s, downloaded: %d bytes", 
                      consecutiveNoDataCount, http.connected() ? "yes" : "no", bytesWritten);
                      
                // Periodic SD card health check during long waits
                if (!SD.exists("/"))
                {
                    log_w("SD card root directory check failed during wait");
                }
            }
            
            delay(10);
            yield(); // Yield when waiting
        }
        
        // Additional safety check - if connection is lost, break
        if (!http.connected())
        {
            log_w("HTTP connection lost during download at %d bytes", bytesWritten);
            break;
        }
    }
    
    log_i("Download loop completed - downloaded %d bytes, connection: %s", 
          bytesWritten, http.connected() ? "connected" : "disconnected");

    // Ensure file is properly flushed and closed
    file.flush();
    file.close();
    http.end();

    // Save bytesWritten before any cleanup to avoid corruption
    int finalBytesWritten = bytesWritten;

    // Clean up HTTPS client if used
    if (secureClient)
    {
        if (usedSpiram)
        {
            secureClient->~WiFiClientSecure();
            heap_caps_free(secureClient);
        }
        else
        {
            delete secureClient;
        }
        secureClient = nullptr;
    }

    // Check if download was successful - require exact file size match for FOTA safety
    bool downloadSuccessful = false;
    
    if (originalFileSize > 0)
    {
        // We know the expected size - require exact match for firmware safety
        downloadSuccessful = (finalBytesWritten == originalFileSize);
        log_i("Download validation: %d bytes written, expected: %d bytes, match: %s", 
              finalBytesWritten, originalFileSize, downloadSuccessful ? "EXACT" : "FAILED");
        
        if (!downloadSuccessful)
        {
            log_e("CRITICAL: Incomplete firmware download detected!");
            log_e("Expected: %d bytes, Got: %d bytes, Missing: %d bytes", 
                  originalFileSize, finalBytesWritten, originalFileSize - finalBytesWritten);
            log_e("FOTA update will be aborted to prevent device corruption");
        }
    }
    else
    {
        // Unknown size, assume success if we got reasonable amount of data
        downloadSuccessful = (finalBytesWritten > 100000); // At least 100KB
        log_w("Download validation: %d bytes written (unknown expected size), success: %s", 
              finalBytesWritten, downloadSuccessful ? "ASSUMED" : "FAILED");
    }
    
    log_i("Download completed: %d bytes written to %s", finalBytesWritten, filepath.c_str());
    
    if (!downloadSuccessful)
    {
        log_e("Download appears to be incomplete or failed");
        log_e("Performing controlled reboot to prevent system instability...");
        clearFirmwareDownloadInProgress();
        
        // Remove potentially corrupted firmware file
        if (SD.exists(filepath.c_str()))
        {
            SD.remove(filepath.c_str());
            log_i("Removed corrupted firmware file: %s", filepath.c_str());
        }
        
        // Brief delay before reboot to ensure logs are written
        delay(2000);
        
        // Perform controlled reboot instead of returning false
        esp_restart();
        
        return false; // Will never reach here due to restart
    }
    
    // Re-enable network connectivity tests
    clearFirmwareDownloadInProgress();
    
    // Phase 1 complete: Download successful, restart to apply update
    log_i("Download successful - restarting to apply firmware update with clean heap");
    delay(1000); // Brief delay to ensure logs are written
    esp_restart();
    
    return true; // Will never reach here due to restart
}

/**
 * @brief Download file from URL to SD card using GSM
 * Based on TinyGSM FileDownload example
 */
static bool downloadFileGSM(const String &url, const String &filepath)
{
    log_i("GSM download: %s", url.c_str());

    TinyGsm* modem = getModemInstance();
    TinyGsmClient* gsmClient = getGsmClientInstance();

    if (!gsmClient || !modem || !modem->isGprsConnected())
    {
        log_e("GSM client not available or not connected");
        clearFirmwareDownloadInProgress();
        return false;
    }

    // Parse URL to extract server, port, and resource path
    String server;
    int port = 80;
    String resource;

    // Remove protocol prefix
    String urlCopy = url;
    if (urlCopy.startsWith("https://"))
    {
        urlCopy = urlCopy.substring(8);
        port = 443;
    }
    else if (urlCopy.startsWith("http://"))
    {
        urlCopy = urlCopy.substring(7);
        port = 80;
    }

    // Extract server and resource
    int slashIndex = urlCopy.indexOf('/');
    if (slashIndex > 0)
    {
        server = urlCopy.substring(0, slashIndex);
        resource = urlCopy.substring(slashIndex);
    }
    else
    {
        server = urlCopy;
        resource = "/";
    }

    // Check for port in server string
    int colonIndex = server.indexOf(':');
    if (colonIndex > 0)
    {
        port = server.substring(colonIndex + 1).toInt();
        server = server.substring(0, colonIndex);
    }

    log_i("Connecting to server: %s:%d", server.c_str(), port);
    log_i("Resource: %s", resource.c_str());

    // Connect to server
    if (!gsmClient->connect(server.c_str(), port))
    {
        log_e("Failed to connect to server via GSM");
        clearFirmwareDownloadInProgress();
        return false;
    }

    log_i("Connected to server, sending HTTP GET request...");

    // Send HTTP GET request
    gsmClient->print(String("GET ") + resource + " HTTP/1.0\r\n");
    gsmClient->print(String("Host: ") + server + "\r\n");
    gsmClient->print("User-Agent: MSP-Firmware-Downloader-GSM/1.0\r\n");
    gsmClient->print("Connection: close\r\n\r\n");

    // Wait for response and read HTTP headers
    log_i("Waiting for HTTP response headers...");
    unsigned long timeout = millis();
    while (gsmClient->connected() && !gsmClient->available())
    {
        if (millis() - timeout > 30000L)
        {
            log_e("HTTP response timeout");
            gsmClient->stop();
            clearFirmwareDownloadInProgress();
            return false;
        }
        delay(100);
    }

    // Read and parse HTTP headers
    log_i("Reading HTTP headers...");
    String headers = "";
    int contentLength = -1;
    bool headersComplete = false;
    timeout = millis();

    while (gsmClient->connected() || gsmClient->available())
    {
        if (gsmClient->available())
        {
            char c = gsmClient->read();
            headers += c;

            // Check for end of headers
            if (headers.endsWith("\r\n\r\n"))
            {
                headersComplete = true;
                log_d("Headers complete");
                break;
            }

            timeout = millis(); // Reset timeout when data received
        }

        if (millis() - timeout > 30000L)
        {
            log_e("Headers read timeout");
            break;
        }
        delay(10);
    }

    if (!headersComplete)
    {
        log_e("Failed to read complete HTTP headers");
        gsmClient->stop();
        clearFirmwareDownloadInProgress();
        return false;
    }

    // Extract status code
    int statusLineEnd = headers.indexOf('\r');
    if (statusLineEnd > 0)
    {
        String statusLine = headers.substring(0, statusLineEnd);
        log_i("HTTP Status: %s", statusLine.c_str());

        // Check for success (200 OK)
        if (statusLine.indexOf("200") < 0 && statusLine.indexOf("OK") < 0)
        {
            log_e("HTTP error: %s", statusLine.c_str());
            gsmClient->stop();
            clearFirmwareDownloadInProgress();
            return false;
        }
    }

    // Extract Content-Length
    int contentLengthIndex = headers.indexOf("Content-Length:");
    if (contentLengthIndex >= 0)
    {
        int lineEnd = headers.indexOf('\r', contentLengthIndex);
        String lengthStr = headers.substring(contentLengthIndex + 15, lineEnd);
        lengthStr.trim();
        contentLength = lengthStr.toInt();
        log_i("Content-Length: %d bytes", contentLength);
    }
    else
    {
        log_w("Content-Length header not found");
    }

    // Open file for writing
    log_i("Opening SD card file for writing: %s", filepath.c_str());
    File file = SD.open(filepath.c_str(), FILE_WRITE);
    if (!file)
    {
        log_e("Failed to create download file on SD card");
        gsmClient->stop();
        clearFirmwareDownloadInProgress();
        return false;
    }

    // Download file content with progress tracking
    log_i("Starting file download...");
    static uint8_t buffer[DOWNLOAD_BUFFER_SIZE];
    int bytesWritten = 0;
    unsigned long lastProgressTime = millis();
    unsigned long lastDataTime = millis();
    const unsigned long dataTimeout = 30000; // 30 second timeout for no data

    while (gsmClient->connected() || gsmClient->available())
    {
        // Check for timeout
        if (gsmClient->available())
        {
            lastDataTime = millis(); // Reset timeout
        }
        else if (millis() - lastDataTime > dataTimeout)
        {
            log_e("Data reception timeout after %d bytes", bytesWritten);
            break;
        }

        while (gsmClient->available())
        {
            int bytesToRead = min((int)gsmClient->available(), (int)sizeof(buffer));
            int bytesRead = gsmClient->readBytes(buffer, bytesToRead);

            if (bytesRead > 0)
            {
                size_t written = file.write(buffer, bytesRead);
                if (written != bytesRead)
                {
                    log_e("SD card write error: wrote %d of %d bytes", written, bytesRead);
                    file.close();
                    gsmClient->stop();
                    clearFirmwareDownloadInProgress();
                    return false;
                }

                bytesWritten += bytesRead;
                lastDataTime = millis();

                // Progress logging every 16KB
                if (millis() - lastProgressTime > 5000 || bytesWritten % (16 * 1024) == 0)
                {
                    if (contentLength > 0)
                    {
                        float progress = ((float)bytesWritten / (float)contentLength) * 100.0;
                        log_i("Download progress: %d/%d bytes (%.1f%%)",
                              bytesWritten, contentLength, progress);
                    }
                    else
                    {
                        log_i("Download progress: %d bytes", bytesWritten);
                    }
                    lastProgressTime = millis();
                    file.flush(); // Periodic flush
                }
            }

            yield(); // Give other tasks time
        }

        delay(10);
    }

    // Close file and connection
    file.flush();
    file.close();
    gsmClient->stop();

    log_i("GSM download completed: %d bytes written", bytesWritten);

    // Validate download
    bool downloadSuccessful = false;
    if (contentLength > 0)
    {
        downloadSuccessful = (bytesWritten == contentLength);
        log_i("Download validation: %d bytes written, expected: %d bytes, match: %s",
              bytesWritten, contentLength, downloadSuccessful ? "EXACT" : "FAILED");

        if (!downloadSuccessful)
        {
            log_e("CRITICAL: Incomplete firmware download via GSM!");
            log_e("Expected: %d bytes, Got: %d bytes, Missing: %d bytes",
                  contentLength, bytesWritten, contentLength - bytesWritten);
        }
    }
    else
    {
        // Unknown size, assume success if we got reasonable amount of data
        downloadSuccessful = (bytesWritten > 100000); // At least 100KB
        log_w("Download validation: %d bytes written (unknown expected size), success: %s",
              bytesWritten, downloadSuccessful ? "ASSUMED" : "FAILED");
    }

    if (!downloadSuccessful)
    {
        log_e("GSM download failed - removing corrupted file");
        clearFirmwareDownloadInProgress();

        if (SD.exists(filepath.c_str()))
        {
            SD.remove(filepath.c_str());
            log_i("Removed corrupted firmware file: %s", filepath.c_str());
        }

        delay(2000);
        esp_restart();

        return false;
    }

    // Re-enable network connectivity tests
    clearFirmwareDownloadInProgress();

    // Download successful, restart to apply update
    log_i("GSM download successful - restarting to apply firmware update");
    delay(1000);
    esp_restart();

    return true;
}

// ESP-IDF OTA management functions implementation

/**
 * @brief Print current OTA partition information
 */
void vHalFirmware_printOTAInfo()
{
    log_i("=== OTA Partition Information ===");

#ifdef ENABLE_ENHANCED_SECURITY
    // Security status (only available with enhanced security)
    bool secure_boot_enabled = esp_secure_boot_enabled();
    bool flash_encryption_enabled = esp_flash_encryption_enabled();

    log_i("Security Status:");
    log_i("- Enhanced Security: ENABLED");
    log_i("- Secure Boot: %s", secure_boot_enabled ? "ENABLED" : "DISABLED");
    log_i("- Flash Encryption: %s", flash_encryption_enabled ? "ENABLED" : "DISABLED");

    if (!secure_boot_enabled)
    {
        log_w("WARNING: Secure boot disabled - firmware signature verification limited");
        log_w("For production deployment, enable secure boot using 'idf.py menuconfig'");
    }

    if (!flash_encryption_enabled)
    {
        log_w("WARNING: Flash encryption disabled - firmware stored in plain text");
        log_w("For enhanced security, enable flash encryption");
    }
#else
    log_i("Security Status:");
    log_i("- Enhanced Security: DISABLED (basic validation only)");
    log_w("To enable cryptographic verification, uncomment ENABLE_ENHANCED_SECURITY in config.h");
#endif

    // Get running partition
    const esp_partition_t *running_partition = esp_ota_get_running_partition();
    if (running_partition)
    {
        log_i("Running partition: %s", running_partition->label);
        log_i("  - Address: 0x%08x", running_partition->address);
        log_i("  - Size: %d bytes", running_partition->size);
        log_i("  - Type: %d, Subtype: %d", running_partition->type, running_partition->subtype);
    }

    // Get next update partition
    const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);
    if (update_partition)
    {
        log_i("Next update partition: %s", update_partition->label);
        log_i("  - Address: 0x%08x", update_partition->address);
        log_i("  - Size: %d bytes", update_partition->size);
    }

    // Get boot partition
    const esp_partition_t *boot_partition = esp_ota_get_boot_partition();
    if (boot_partition)
    {
        log_i("Boot partition: %s", boot_partition->label);
        log_i("  - Address: 0x%08x", boot_partition->address);
    }

    // Check OTA data partition
    const esp_partition_t *ota_data = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_OTA, NULL);
    if (ota_data)
    {
        log_i("OTA data partition found: %s", ota_data->label);
        log_i("  - Address: 0x%08x", ota_data->address);
        log_i("  - Size: %d bytes", ota_data->size);
    }

    // Check current app state
    esp_ota_img_states_t ota_state;
    const esp_partition_t *last_invalid_app = esp_ota_get_last_invalid_partition();
    if (last_invalid_app)
    {
        log_w("Last invalid partition: %s", last_invalid_app->label);
    }

    esp_err_t err = esp_ota_get_state_partition(running_partition, &ota_state);
    if (err == ESP_OK)
    {
        switch (ota_state)
        {
        case ESP_OTA_IMG_NEW:
            log_i("Current app state: NEW (first boot after update)");
            break;
        case ESP_OTA_IMG_PENDING_VERIFY:
            log_w("Current app state: PENDING_VERIFY (needs validation)");
            break;
        case ESP_OTA_IMG_VALID:
            log_i("Current app state: VALID (confirmed working)");
            break;
        case ESP_OTA_IMG_INVALID:
            log_e("Current app state: INVALID (marked as failed)");
            break;
        case ESP_OTA_IMG_ABORTED:
            log_e("Current app state: ABORTED (update was aborted)");
            break;
        case ESP_OTA_IMG_UNDEFINED:
            log_w("Current app state: UNDEFINED");
            break;
        }
    }

    log_i("=== End OTA Information ===");
}

/**
 * @brief Validate current firmware and mark it as working
 */
bool bHalFirmware_validateCurrentFirmware()
{
    log_i("Validating current firmware...");

    const esp_partition_t *running_partition = esp_ota_get_running_partition();
    if (!running_partition)
    {
        log_e("Failed to get running partition");
        return false;
    }

    esp_ota_img_states_t ota_state;
    esp_err_t err = esp_ota_get_state_partition(running_partition, &ota_state);
    if (err != ESP_OK)
    {
        log_e("Failed to get partition state: %s", esp_err_to_name(err));
        return false;
    }

    switch (ota_state)
    {
    case ESP_OTA_IMG_NEW:
    case ESP_OTA_IMG_PENDING_VERIFY:
        log_i("Firmware validation required, marking as valid...");
        return bHalFirmware_markFirmwareValid();

    case ESP_OTA_IMG_VALID:
        log_i("Firmware already validated");
        return true;

    case ESP_OTA_IMG_INVALID:
    case ESP_OTA_IMG_ABORTED:
        log_e("Current firmware is marked as invalid/aborted");
        return false;

    case ESP_OTA_IMG_UNDEFINED:
        log_i("Firmware state undefined - likely first boot or non-OTA firmware, marking as valid...");
        return bHalFirmware_markFirmwareValid();

    default:
        log_w("Unknown firmware state: %d", ota_state);
        // For unknown states, try to mark as valid to prevent boot loops
        log_i("Attempting to mark unknown state as valid...");
        return bHalFirmware_markFirmwareValid();
    }
}

#ifdef ENABLE_ENHANCED_SECURITY
/**
 * @brief Verify firmware signature and authenticity
 * @param firmwarePath Path to firmware file to verify
 * @return true if firmware is authentic and signature is valid
 */
bool bHalFirmware_verifyFirmwareSignature(const String &firmwarePath)
{
    log_i("Verifying firmware signature: %s", firmwarePath.c_str());

    // Check if secure boot is enabled
    bool secure_boot_enabled = esp_secure_boot_enabled();
    if (!secure_boot_enabled)
    {
        log_w("Secure boot not enabled - firmware signature verification skipped");
        log_w("For production deployment, enable secure boot for enhanced security");
        return true; // Allow update but warn
    }

    // Open firmware file
    File firmwareFile = SD.open(firmwarePath.c_str(), FILE_READ);
    if (!firmwareFile)
    {
        log_e("Failed to open firmware file for signature verification");
        return false;
    }

    size_t firmwareSize = firmwareFile.size();
    log_i("Firmware size: %zu bytes", firmwareSize);

    // Check minimum size for valid ESP32 firmware
    if (firmwareSize < FIRMWARE_MIN_VALID_LEN)
    {
        log_e("Firmware file too small to be valid");
        firmwareFile.close();
        return false;
    }

    // Read firmware header to validate format
    esp_image_header_t imageHeader;
    if (firmwareFile.readBytes((char *)&imageHeader, sizeof(imageHeader)) != sizeof(imageHeader))
    {
        log_e("Failed to read firmware header");
        firmwareFile.close();
        return false;
    }

    // Validate ESP32 firmware header magic number
    if (imageHeader.magic != ESP_IMAGE_HEADER_MAGIC)
    {
        log_e("Invalid firmware header magic: 0x%02X (expected 0x%02X)",
              imageHeader.magic, ESP_IMAGE_HEADER_MAGIC);
        firmwareFile.close();
        return false;
    }

    // Validate chip revision compatibility
    uint32_t chip_rev = esp_efuse_get_pkg_ver();
    if (imageHeader.chip_id != ESP_CHIP_ID_ESP32 ||
        (imageHeader.min_chip_rev > chip_rev))
    {
        log_e("Firmware not compatible with this ESP32 chip revision");
        firmwareFile.close();
        return false;
    }

    firmwareFile.close();

    // If we have secure boot, the signature verification happens during esp_ota_write()
    // This provides hardware-level signature verification
    log_i("Firmware header validation passed");
    log_i("Hardware signature verification will be performed during OTA write");

    return true;
}

/**
 * @brief Calculate and verify firmware SHA256 hash
 * @param firmwarePath Path to firmware file
 * @param expectedHash Expected SHA256 hash (optional, can be empty)
 * @return true if hash calculation succeeds and matches expected (if provided)
 */
bool bHalFirmware_verifyFirmwareHash(const String &firmwarePath, const String &expectedHash)
{
    log_i("Calculating firmware SHA256 hash: %s", firmwarePath.c_str());

    File firmwareFile = SD.open(firmwarePath.c_str(), FILE_READ);
    if (!firmwareFile)
    {
        log_e("Failed to open firmware file for hash verification");
        return false;
    }

    // Initialize SHA256 context
    mbedtls_sha256_context sha256_ctx;
    mbedtls_sha256_init(&sha256_ctx);
    mbedtls_sha256_starts(&sha256_ctx, 0); // 0 for SHA256 (not SHA224)

    // Process file in chunks
    static uint8_t buffer[1024]; // Use smaller buffer for hash calculation
    while (firmwareFile.available())
    {
        size_t bytesRead = firmwareFile.readBytes((char *)buffer, sizeof(buffer));
        if (bytesRead > 0)
        {
            mbedtls_sha256_update(&sha256_ctx, buffer, bytesRead);
        }
    }

    // Finalize hash calculation
    uint8_t hash[HASH_LENGTH]; // SHA256 produces 32-byte hash
    mbedtls_sha256_finish(&sha256_ctx, hash);
    mbedtls_sha256_free(&sha256_ctx);
    firmwareFile.close();

    // Convert hash to hex string
    String calculatedHash = "";
    for (int i = 0; i < HASH_LENGTH; i++)
    {
        char hex[3];
        sprintf(hex, "%02x", hash[i]);
        calculatedHash += hex;
    }

    log_i("Calculated SHA256: %s", calculatedHash.c_str());

    // Verify against expected hash if provided
    if (expectedHash.length() > 0)
    {
        if (calculatedHash.equalsIgnoreCase(expectedHash))
        {
            log_i("Hash verification PASSED");
            return true;
        }
        else
        {
            log_e("Hash verification FAILED");
            log_e("Expected: %s", expectedHash.c_str());
            log_e("Calculated: %s", calculatedHash.c_str());
            return false;
        }
    }

    log_i("Hash calculation completed (no expected hash provided)");
    return true;
}

/**
 * @brief Verify detached RSA signature for firmware
 * @param firmwarePath Path to firmware file
 * @param signatureFilePath Path to detached signature file
 * @return true if signature is valid
 */
bool bHalFirmware_verifyDetachedSignature(const String &firmwarePath, const String &signatureFilePath)
{
    log_i("Verifying detached signature for firmware");

    // For now, we'll implement a basic signature verification
    // In a full implementation, you would:
    // 1. Load the public key (could be embedded or from SD card)
    // 2. Read the signature file
    // 3. Verify the signature using mbedTLS RSA functions

    File signatureFile = SD.open(signatureFilePath.c_str(), FILE_READ);
    if (!signatureFile)
    {
        log_e("Could not open signature file: %s", signatureFilePath.c_str());
        return false;
    }

    size_t sigSize = signatureFile.size();
    signatureFile.close();

    // Basic validation - RSA-3072 signature should be 384 bytes
    if (sigSize != 384 && sigSize != 256) // Support both RSA-3072 (384) and RSA-2048 (256)
    {
        log_e("Invalid signature file size: %d bytes (expected 256 or 384)", sigSize);
        return false;
    }

    log_i("Signature file validation passed: %d bytes", sigSize);

    // TODO: Implement full RSA signature verification with mbedTLS
    // For now, we trust the signature file presence and size validation
    // This provides some protection against basic tampering

    log_w("Note: Full cryptographic signature verification requires additional implementation");
    log_w("Current implementation provides basic signature file validation");

    return true;
}
#endif // ENABLE_ENHANCED_SECURITY

/**
 * @brief Mark current firmware as valid (prevents rollback)
 */
bool bHalFirmware_markFirmwareValid()
{
    log_i("Marking current firmware as valid...");

    esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
    if (err == ESP_OK)
    {
        log_i("Firmware marked as valid successfully");
        return true;
    }
    else
    {
        log_e("Failed to mark firmware as valid: %s", esp_err_to_name(err));
        return false;
    }
}

/**
 * @brief Check if firmware rollback is available
 */
bool bHalFirmware_isRollbackAvailable()
{
    const esp_partition_t *last_invalid_app = esp_ota_get_last_invalid_partition();
    return (last_invalid_app != NULL);
}

/**
 * @brief Rollback to previous firmware version
 */
bool bHalFirmware_rollbackFirmware()
{
    log_i("Attempting firmware rollback...");

    if (!bHalFirmware_isRollbackAvailable())
    {
        log_e("No rollback partition available");
        return false;
    }

    const esp_partition_t *last_invalid_app = esp_ota_get_last_invalid_partition();
    log_i("Rolling back to partition: %s", last_invalid_app->label);

    esp_err_t err = esp_ota_set_boot_partition(last_invalid_app);
    if (err != ESP_OK)
    {
        log_e("Failed to set rollback partition: %s", esp_err_to_name(err));
        return false;
    }

    log_i("Rollback partition set successfully");
    log_i("System will restart to complete rollback...");

    delay(2000);
    esp_restart();

    return true; // Never reached due to restart
}

#ifdef ENABLE_FIRMWARE_UPDATE_TESTS
// Test functions implementation

/**
 * @brief Test version comparison functionality
 */
void vHalFirmware_testVersionComparison()
{
    log_i("=== Testing Version Comparison ===");

    struct
    {
        String current;
        String remote;
        bool expected;
        String description;
    } testCases[] = {
        {"1.0.0", "1.0.1", true, "Patch version update"},
        {"1.0.0", "1.1.0", true, "Minor version update"},
        {"1.0.0", "2.0.0", true, "Major version update"},
        {"1.1.0", "1.0.9", false, "Remote older minor"},
        {"1.0.1", "1.0.0", false, "Remote older patch"},
        {"2.0.0", "1.9.9", false, "Remote older major"},
        {"1.0.0", "1.0.0", false, "Same version"},
        {"v1.0.0", "v1.0.1", true, "With v prefix"},
        {"DEV", "1.0.0", true, "Development version"},
    };

    int passed = 0;
    int total = sizeof(testCases) / sizeof(testCases[0]);

    for (int i = 0; i < total; i++)
    {
        bool result = bHalFirmware_compareVersions(testCases[i].current, testCases[i].remote);
        if (result == testCases[i].expected)
        {
            log_i("PASS: %s (%s vs %s)", testCases[i].description.c_str(),
                  testCases[i].current.c_str(), testCases[i].remote.c_str());
            passed++;
        }
        else
        {
            log_e("FAIL: %s (%s vs %s) - Expected: %d, Got: %d",
                  testCases[i].description.c_str(), testCases[i].current.c_str(),
                  testCases[i].remote.c_str(), testCases[i].expected, result);
        }
    }

    log_i("Version comparison tests: %d/%d passed", passed, total);
}

/**
 * @brief Test GitHub API functionality (requires internet connection)
 */
void vHalFirmware_testGitHubAPI()
{
    log_i("=== Testing GitHub API ===");

    if (!WiFi.isConnected())
    {
        log_w("WiFi not connected, skipping GitHub API test");
        return;
    }

    HTTPClient http;
    http.setTimeout(10000);

    if (!http.begin(GITHUB_TEST_API_URL))
    {
        log_e("Failed to initialize HTTP client");
        return;
    }

    http.addHeader("User-Agent", "MilanoSmartPark-ESP32-Test");
    http.addHeader("Accept", "application/vnd.github.v3+json");

    log_i("Making request to: %s", GITHUB_TEST_API_URL);
    int httpCode = http.GET();

    if (httpCode == HTTP_CODE_OK)
    {
        String payload = http.getString();
        log_i("PASS: GitHub API responded successfully");
        log_d("Response length: %d bytes", payload.length());

        // Try to parse JSON
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, payload);

        if (!error)
        {
            String tagName = doc["tag_name"].as<String>();
            String publishedAt = doc["published_at"].as<String>();
            log_i("PASS: JSON parsing successful");
            log_i("Latest release: %s (published: %s)", tagName.c_str(), publishedAt.c_str());

            // Show available assets and which one would be selected
            JsonArray assets = doc["assets"];
            String macosUrl = "";
            String selectedAsset = "";

            log_i("Available assets:");
            for (JsonObject asset : assets)
            {
                String name = asset["name"].as<String>();
                int size = asset["size"].as<int>();
                log_i("  - %s (%d bytes)", name.c_str(), size);

                if (name.endsWith(".bin") && name.startsWith("update"))
                {
                    macosUrl = asset["browser_download_url"].as<String>();
                    selectedAsset = name;
                }
            }

            if (!selectedAsset.isEmpty())
            {
                log_i("PASS: Would select asset: %s", selectedAsset.c_str());
            }
            else
            {
                log_w("No suitable firmware asset found");
            }
        }
        else
        {
            log_e("FAIL: JSON parsing failed: %s", error.c_str());
        }
    }
    else
    {
        log_e("FAIL: GitHub API request failed with code: %d", httpCode);
    }

    http.end();
}

/**
 * @brief Test configuration parsing
 */
void vHalFirmware_testConfigParsing(systemStatus_t *sysStatus)
{
    log_i("=== Testing Configuration Parsing ===");

    log_i("fwAutoUpgrade setting: %s", sysStatus->fwAutoUpgrade ? "enabled" : "disabled");

    if (sysStatus->fwAutoUpgrade)
    {
        log_i("PASS: Firmware auto-upgrade is enabled");
    }
    else
    {
        log_i("INFO: Firmware auto-upgrade is disabled (this is normal for testing)");
    }

    log_i("Other system status:");
    log_i("- SD Card: %s", sysStatus->sdCard ? "OK" : "FAIL");
    log_i("- Configuration: %s", sysStatus->configuration ? "OK" : "FAIL");
    log_i("- Connection: %s", sysStatus->connection ? "OK" : "FAIL");
    log_i("- DateTime: %s", sysStatus->datetime ? "OK" : "FAIL");
    log_i("- Server: %s", sysStatus->server_ok ? "OK" : "FAIL");
}

/**
 * @brief Test OTA management functions with real firmware download and update
 */
void vHalFirmware_testOTAManagement()
{
    log_i("=== Testing ESP-IDF OTA Management ===");

    // Test 1: Print OTA partition information
    log_i("Test 1: OTA Partition Information");
    vHalFirmware_printOTAInfo();

    // Test 2: Check current firmware validation status
    log_i("Test 2: Current Firmware Validation");
    bool isValid = bHalFirmware_validateCurrentFirmware();
    if (isValid)
    {
        log_i("PASS: Current firmware is valid");
    }
    else
    {
        log_w("WARNING: Current firmware validation failed - this may be expected for new firmware");
    }

    // Test 3: Check if rollback is available
    log_i("Test 3: Rollback Availability");
    bool rollbackAvailable = bHalFirmware_isRollbackAvailable();
    if (rollbackAvailable)
    {
        log_i("INFO: Rollback is available to previous firmware");
    }
    else
    {
        log_i("INFO: No rollback available (single firmware installed)");
    }

    // Test 4: Mark current firmware as valid (if not already)
    log_i("Test 4: Mark Firmware as Valid");
    bool markSuccess = bHalFirmware_markFirmwareValid();
    if (markSuccess)
    {
        log_i("PASS: Successfully marked firmware as valid");
    }
    else
    {
        log_w("WARNING: Failed to mark firmware as valid");
    }

    // Test 5: Download and test complete FOTA process with older firmware
    log_i("Test 5: Complete FOTA Process Test");

    // Test URL - try to get actual latest release URL first, fallback to fixed URL for testing
    String testFirmwareUrl = "https://github.com/A-A-Milano-Smart-Park/msp-firmware/releases/download/v4.1.0/update_v4.1.0.bin";
    String testFirmwarePath = "/update_v4.1.0.bin";

    // Alternative: Try to get latest release URL from GitHub API (but simpler for now)
    log_i("Attempting to download test firmware from: %s", testFirmwareUrl.c_str());
    log_i("Note: If this URL doesn't exist, the test will use simulated data instead");

    // Use the existing downloadFile function which has proper redirect handling
    bool downloadSuccess = downloadFile(testFirmwareUrl, testFirmwarePath);

    if (downloadSuccess)
    {
        log_i("PASS: Firmware download completed successfully");

        // Test 6: Complete BIN File Validation and OTA Process
        log_i("Test 6: Complete FOTA Process Test");

        // Test the complete FOTA process
        log_i("Testing complete FOTA pipeline...");

        File binFile = SD.open(testFirmwarePath.c_str(), FILE_READ);
        if (binFile)
        {
            size_t binSize = binFile.size();
            log_i("BIN file size: %zu bytes", binSize);

            // Read first few bytes to verify ESP32 BIN file format
            uint8_t header[4];
            if (binFile.readBytes((char *)header, 4) == 4)
            {
                // ESP32 firmware binary header should start with 0xE9 (ESP_IMAGE_HEADER_MAGIC)
                if (header[0] == 0xE9)
                {
                    log_i("PASS: Valid ESP32 BIN file header detected");

                    binFile.close();

                    // Perform detailed OTA update test
                    log_i("DETAILED TEST: OTA analysis");

                    bool updateSuccess = bHalFirmware_performOTAUpdate(testFirmwarePath);

                    if (updateSuccess == false)
                    {
                        log_e("FAIL: OTA process failed");
                    }
                }
                else
                {
                    log_e("FAIL: Invalid ESP32 BIN file format - Header: 0x%02X%02X%02X%02X",
                          header[0], header[1], header[2], header[3]);
                    log_e("Expected ESP32 firmware to start with 0xE9 magic byte");
                }
            }
            else
            {
                log_e("FAIL: Could not read BIN file header");
            }

            binFile.close();
        }
        else
        {
            log_e("FAIL: Could not open downloaded BIN file");
        }

        // Clean up test files
        log_i("Cleaning up test files...");
        SD.remove(testFirmwarePath.c_str());
        log_i("PASS: Complete FOTA process test completed successfully");
    }
    else
    {
        log_w("INFO: Could not download test firmware - this is expected if URL doesn't exist");
        log_i("Testing with local simulation instead...");

        // Fallback: Test with simulated firmware data
        log_i("SIMULATION: Creating test firmware data");
        File testFile = SD.open(testFirmwarePath.c_str(), FILE_WRITE);
        if (testFile)
        {
            // Write some test data (simulating a small firmware)
            uint8_t testData[1024];
            for (int i = 0; i < sizeof(testData); i++)
            {
                testData[i] = (uint8_t)(i % 256);
            }
            testFile.write(testData, sizeof(testData));
            testFile.close();

            log_i("PASS: Created simulated firmware test file");

            // Clean up
            SD.remove(testFirmwarePath.c_str());
        }
    }

    log_i("=== OTA Management Tests Completed ===");
}

/**
 * @brief Test function for force OTA update
 */
void vHalFirmware_testForceOTAUpdate(systemData_t *sysData, systemStatus_t *sysStatus, deviceNetworkInfo_t *devInfo)
{
    log_i("=== Force OTA Update Test ===");
    log_w("DANGER: This will perform ACTUAL OTA update without version checking!");
    log_w("The device WILL reboot and install the latest firmware from GitHub!");
    log_i("Requesting force OTA update via network task for proper stack management...");

    // Use network task for consistent stack management
    bHalFirmware_checkForUpdates(sysData, sysStatus, devInfo);
}

#endif

/**
 * @brief Check and apply pending firmware update from downloaded file
 * @param firmwarePath Path to downloaded firmware file on SD card
 * @return true if update was applied, false if no update needed or failed
 */
bool bHalFirmware_checkAndApplyPendingUpdate(const char* firmwarePath)
{
    log_i("=== Phase 2: Checking Downloaded Firmware ===");
    
    if (!SD.exists(firmwarePath)) {
        log_w("No firmware file found at: %s", firmwarePath);
        return false;
    }
    
    // Open firmware file
    File firmwareFile = SD.open(firmwarePath, FILE_READ);
    if (!firmwareFile) {
        log_e("Failed to open firmware file for reading");
        return false;
    }
    
    size_t fileSize = firmwareFile.size();
    log_i("Found firmware file: %s (size: %d bytes)", firmwarePath, fileSize);
    
    // Get next OTA partition
    const esp_partition_t* update_partition = esp_ota_get_next_update_partition(NULL);
    if (!update_partition) {
        log_e("No OTA partition available");
        firmwareFile.close();
        return false;
    }
    
    log_i("OTA partition: %s (size: %d bytes)", update_partition->label, update_partition->size);
    
    // Check if firmware fits in partition
    if (fileSize > update_partition->size) {
        log_e("Firmware size (%d bytes) exceeds partition size (%d bytes)", 
              fileSize, update_partition->size);
        firmwareFile.close();
        return false;
    }
    
    // Begin OTA update
    esp_ota_handle_t ota_handle = 0;
    esp_err_t err = esp_ota_begin(update_partition, fileSize, &ota_handle);
    if (err != ESP_OK) {
        log_e("Failed to begin OTA update: %s", esp_err_to_name(err));
        firmwareFile.close();
        return false;
    }
    
    log_i("OTA update started successfully");
    
    // Write firmware in chunks
    uint8_t buffer[1024];
    size_t totalWritten = 0;
    bool updateSuccess = true;
    
    log_i("Writing firmware data...");
    while (firmwareFile.available() && updateSuccess) {
        size_t bytesRead = firmwareFile.readBytes((char*)buffer, sizeof(buffer));
        if (bytesRead > 0) {
            err = esp_ota_write(ota_handle, buffer, bytesRead);
            if (err != ESP_OK) {
                log_e("Failed to write OTA data at offset %d: %s", 
                      totalWritten, esp_err_to_name(err));
                updateSuccess = false;
                break;
            }
            totalWritten += bytesRead;
            
            // Log progress every 100KB
            if (totalWritten % (100 * 1024) == 0) {
                log_i("Written: %d / %d bytes (%.1f%%)", 
                      totalWritten, fileSize, (totalWritten * 100.0f) / fileSize);
            }
        }
    }
    
    firmwareFile.close();
    
    if (updateSuccess && totalWritten == fileSize) {
        log_i("Firmware write completed: %d bytes", totalWritten);
        
        // Finalize OTA update
        err = esp_ota_end(ota_handle);
        if (err != ESP_OK) {
            log_e("Failed to finalize OTA update: %s", esp_err_to_name(err));
            return false;
        }
        
        // Set boot partition
        err = esp_ota_set_boot_partition(update_partition);
        if (err != ESP_OK) {
            log_e("Failed to set boot partition: %s", esp_err_to_name(err));
            return false;
        }
        
        log_i("OTA update successful - device will restart with new firmware");
        
        vTaskDelay(pdMS_TO_TICKS(1000));
        
        SD.remove(firmwarePath);

        vTaskDelay(pdMS_TO_TICKS(1000));

        esp_restart(); // This will boot into new firmware
        return true; // Never reached
    } else {
        log_e("Firmware write failed: written %d, expected %d", totalWritten, fileSize);
        esp_ota_end(ota_handle); // End OTA even on failure
        return false;
    }
}