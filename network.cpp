/****************************************************
 * @file    network.cpp (Improved Version)
 * @author  AB-Engineering - https://ab-engineering.it
 * @brief   Enhanced Network management for the Milano Smart Park project
 * @details Thread-safe network management with proper state machine implementation
 * @version 0.2
 * @date    2025-08-05
 *
 * @copyright Copyright (c) 2025
 *
 ****************************************************/

#define TINY_GSM_MODEM_SIM800

#include <Arduino.h>
#include <TinyGsmClient.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <time.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include "network.h"
#include "trust_anchor.h"
#include "display_task.h"
#include "display.h"
#include "mspOs.h"
#include "sdcard.h"
#include "sensors.h"
#include "config.h"
#include "firmware_update.h"

// -- Network Configuration Constants
#define TIME_SYNC_MAX_RETRY 5

// Modem buffer configuration
#if !defined(TINY_GSM_RX_BUFFER)
#define TINY_GSM_RX_BUFFER 650
#endif

// Hardware serial for GSM
HardwareSerial gsmSerial(1);

// Task configuration - public values defined in network.h
#define SEND_DATA_QUEUE_LENGTH 512 // Internal queue configuration

// Static task variables
static StaticTask_t networkTaskBuffer;
static StackType_t networkTaskStack[NETWORK_TASK_STACK_SIZE];
static TaskHandle_t networkTaskHandle = NULL;

// Queue and synchronization objects
static QueueHandle_t sendDataQueue = NULL;
static QueueHandle_t serverConfigQueue = NULL; // Queue for server config responses
static EventGroupHandle_t networkEventGroup = NULL;
static StaticEventGroup_t networkEventGroupBuffer;
static SemaphoreHandle_t networkStateMutex = NULL;
static StaticSemaphore_t networkStateMutexBuffer;

// Network state variables (protected by mutex)
static struct
{
    bool wifiConnected;
    bool gsmConnected;
    bool internetConnected;
    bool timeSync;
    int connectionRetries;
    unsigned long lastConnectionAttempt;
    netwkr_task_evt_t currentState;
    netwkr_task_evt_t nextState;
    bool taskRunning;
    bool configurationLoaded;
    int ntpSyncExpired;              // Counter for NTP sync expiration
    bool firmwareDownloadInProgress; // Flag to skip connectivity checks during firmware download
} networkState = {
    .wifiConnected = false,
    .gsmConnected = false,
    .internetConnected = false,
    .timeSync = false,
    .connectionRetries = 0,
    .lastConnectionAttempt = 0,
    .currentState = NETWRK_EVT_WAIT,
    .nextState = NETWRK_EVT_WAIT,
    .taskRunning = false,
    .configurationLoaded = false,
    .ntpSyncExpired = NTP_SYNC_TX_COUNT, // Initialize with default count
    .firmwareDownloadInProgress = false
};

// Global instances (properly managed within task)
static TinyGsm *modem = NULL;
static TinyGsmClient *gsmClient = NULL;
static WiFiClient wifi_base;
static SSLClient *sslClient = NULL;

// Global data structure pointers (shared with main task)
static systemData_t *globalSysData = NULL;
static systemStatus_t *globalSysStatus = NULL;
static deviceNetworkInfo_t *globalDevInfo = NULL;

// Forward declarations
static void networkTask(void *pvParameters);
static bool initializeNetworkResources(bool isUsingModem);
static void cleanupNetworkResources();
static bool handleWiFiConnection(deviceNetworkInfo_t *devInfo, systemStatus_t *sysStatus);
static bool handleGSMConnection(deviceNetworkInfo_t *devInfo, systemStatus_t *sysStatus);
static bool syncDateTime(deviceNetworkInfo_t *devInfo, systemStatus_t *sysStatus, systemData_t *sysData);
static bool sendDataToServer(send_data_t *dataToSend, deviceNetworkInfo_t *devInfo,
                             systemStatus_t *sysStatus, systemData_t *sysData);
static void updateNetworkState(netwkr_task_evt_t newState);
static netwkr_task_evt_t getNetworkState();
static bool loadNetworkConfiguration(deviceNetworkInfo_t *devInfo, systemStatus_t *sysStatus,
                                     systemData_t *sysData, sensorData_t *sensorData,
                                     deviceMeasurement_t *measStat);

// Public interface functions
bool enqueueSendData(const send_data_t &data, TickType_t ticksToWait)
{
    if (sendDataQueue == NULL)
    {
        log_e("Send data queue not initialized");
        return false;
    }

    // Check queue status before attempting to send
    UBaseType_t queueSpaces = uxQueueSpacesAvailable(sendDataQueue);
    UBaseType_t queueWaiting = uxQueueMessagesWaiting(sendDataQueue);
    log_i("Queue status before enqueue: %d spaces available, %d items waiting", queueSpaces, queueWaiting);

    // Alert if queue is accumulating items (suggests processing issues)
    if (queueWaiting >= (SEND_DATA_QUEUE_LENGTH / 2))
    {
        log_w("QUEUE ACCUMULATION WARNING: %d/%d items queued (>50%% full)", queueWaiting, SEND_DATA_QUEUE_LENGTH);
        log_w("This suggests the network task may not be processing the queue effectively");

        if (queueWaiting >= (SEND_DATA_QUEUE_LENGTH * 3 / 4))
        {
            log_e("QUEUE CRITICAL: %d/%d items queued (>75%% full) - risk of data loss!", queueWaiting, SEND_DATA_QUEUE_LENGTH);
            log_e("Network task processing may be blocked or failing");
        }
    }

    BaseType_t result = xQueueSend(sendDataQueue, &data, ticksToWait);
    if (result != pdPASS)
    {
        log_w("Failed to enqueue send data - queue full or error. Spaces: %d, Waiting: %d",
              uxQueueSpacesAvailable(sendDataQueue), uxQueueMessagesWaiting(sendDataQueue));
        return false;
    }

    log_i("Data enqueued successfully. Queue now has %d items", uxQueueMessagesWaiting(sendDataQueue));

    // Trigger network task to process data
    xEventGroupSetBits(networkEventGroup, NET_EVT_DATA_READY);
    return true;
}

bool dequeueSendData(send_data_t *data, TickType_t ticksToWait)
{
    if (sendDataQueue == NULL || data == NULL)
    {
        log_e("Invalid parameters for dequeue operation");
        return false;
    }

    return (xQueueReceive(sendDataQueue, data, ticksToWait) == pdPASS);
}

bool dequeueServerConfig(server_config_msg_t *config, TickType_t ticksToWait)
{
    if (serverConfigQueue == NULL || config == NULL)
    {
        return false;
    }

    return (xQueueReceive(serverConfigQueue, config, ticksToWait) == pdPASS);
}

void initSendDataOp(systemData_t *sysData, systemStatus_t *sysStatus, deviceNetworkInfo_t *devInfo)
{
    // Store global data structure pointers
    globalSysData = sysData;
    globalSysStatus = sysStatus;
    globalDevInfo = devInfo;

    log_i("Network task initialized with global data structures");
    log_i("Server OK from main task: %d", globalSysStatus->server_ok);

    // Create send data queue
    if (sendDataQueue == NULL)
    {
        sendDataQueue = xQueueCreate(SEND_DATA_QUEUE_LENGTH, sizeof(send_data_t));
        if (sendDataQueue == NULL)
        {
            log_e("Failed to create send data queue");
            return;
        }
        log_i("Send data queue created successfully with size %d", SEND_DATA_QUEUE_LENGTH);
    }
    else
    {
        // Queue already exists, flush any stale items
        log_i("Queue already exists with %d items, flushing stale data", uxQueueMessagesWaiting(sendDataQueue));
        xQueueReset(sendDataQueue);
        log_i("Queue flushed, now has %d items", uxQueueMessagesWaiting(sendDataQueue));
    }

    // Create server config queue (size 1 - only latest config matters)
    if (serverConfigQueue == NULL)
    {
        serverConfigQueue = xQueueCreate(1, sizeof(server_config_msg_t));
        if (serverConfigQueue == NULL)
        {
            log_e("Failed to create server config queue");
            return;
        }
        log_i("Server config queue created successfully");
    }

    // Create mutex for network state protection
    if (networkStateMutex == NULL)
    {
        networkStateMutex = xSemaphoreCreateMutexStatic(&networkStateMutexBuffer);
        if (networkStateMutex == NULL)
        {
            log_e("Failed to create network state mutex");
            return;
        }
        log_i("Network state mutex created successfully");
    }

    // Create network task
    if (networkTaskHandle == NULL)
    {
        networkTaskHandle = xTaskCreateStaticPinnedToCore(
            networkTask,
            "NetworkTask",
            NETWORK_TASK_STACK_SIZE,
            NULL,
            NETWORK_TASK_PRIORITY,
            networkTaskStack,
            &networkTaskBuffer,
            1 // Core 1
        );

        if (networkTaskHandle == NULL)
        {
            log_e("Failed to create network task");
        }
        else
        {
            log_i("Network task created successfully");
            // Mark task as running
            if (xSemaphoreTake(networkStateMutex, pdMS_TO_TICKS(1000)) == pdTRUE)
            {
                networkState.taskRunning = true;
                xSemaphoreGive(networkStateMutex);
            }
        }
    }
}

void createNetworkEvents()
{
    if (networkEventGroup == NULL)
    {
        networkEventGroup = xEventGroupCreateStatic(&networkEventGroupBuffer);
        if (networkEventGroup == NULL)
        {
            log_e("Failed to create network event group");
        }
        else
        {
            log_i("Network event group created successfully");
        }
    }
}

bool sendNetworkEvent(net_evt_t event)
{
    if (networkEventGroup == NULL)
    {
        log_e("Network event group not initialized");
        return false;
    }

    EventBits_t result = xEventGroupSetBits(networkEventGroup, event);
    return (result & event) != 0;
}

bool checkNetworkEvent(net_evt_t event)
{
    if (networkEventGroup == NULL)
    {
        return false;
    }

    EventBits_t currentBits = xEventGroupGetBits(networkEventGroup);
    return (currentBits & event) != 0;
}

bool waitForNetworkEvent(net_evt_t event, TickType_t ticksToWait)
{
    if (networkEventGroup == NULL)
    {
        log_e("Network event group not initialized");
        return false;
    }

    EventBits_t result = xEventGroupWaitBits(
        networkEventGroup,
        event,
        pdTRUE,  // Clear bits on exit
        pdFALSE, // Wait for any bit
        ticksToWait);

    return (result & event) != 0;
}

uint8_t vHalNetwork_modemDisconnect()
{
    if (modem && modem->isGprsConnected())
    {
        log_i("Disconnecting from GPRS...");
        bool result = modem->gprsDisconnect();

        // Update state
        if (xSemaphoreTake(networkStateMutex, pdMS_TO_TICKS(1000)) == pdTRUE)
        {
            networkState.gsmConnected = false;
            xSemaphoreGive(networkStateMutex);
        }

        return result ? 1 : 0;
    }
    return 1; // Already disconnected
}

// Network state management (thread-safe)
static void updateNetworkState(netwkr_task_evt_t newState)
{
    if (xSemaphoreTake(networkStateMutex, pdMS_TO_TICKS(1000)) == pdTRUE)
    {
        networkState.nextState = newState;
        xSemaphoreGive(networkStateMutex);
    }
}

static netwkr_task_evt_t getNetworkState()
{
    netwkr_task_evt_t state = NETWRK_EVT_WAIT;
    if (xSemaphoreTake(networkStateMutex, pdMS_TO_TICKS(1000)) == pdTRUE)
    {
        state = networkState.currentState;
        xSemaphoreGive(networkStateMutex);
    }
    return state;
}

bool isNetworkConnected()
{
    bool connected = false;
    if (xSemaphoreTake(networkStateMutex, pdMS_TO_TICKS(1000)) == pdTRUE)
    {
        connected = (networkState.wifiConnected) || (networkState.gsmConnected);
        xSemaphoreGive(networkStateMutex);
    }
    return connected;
}

/**
 * @brief Test internet connectivity by attempting DNS resolution
 * @details Tests connectivity to well-known DNS servers and attempts to resolve common domains
 * @return true if internet connectivity is available, false otherwise
 */
static bool testInternetConnectivity()
{
    // First check if we have basic network connectivity
    if (!isNetworkConnected())
    {
        log_v("No network connection for internet test");
        return false;
    }

    // Test DNS resolution of well-known domains
    const char *testDomains[] = {
        "google.com",
        "cloudflare.com",
        "microsoft.com"};

    for (int i = 0; i < 3; i++)
    {
        IPAddress result;
        int dnsResult;

        // Use WiFi hostByName for WiFi connections, or try TinyGSM for cellular
        bool wifiConnected = false;
        bool gsmConnected = false;

        if (xSemaphoreTake(networkStateMutex, pdMS_TO_TICKS(1000)) == pdTRUE)
        {
            wifiConnected = networkState.wifiConnected;
            gsmConnected = networkState.gsmConnected;
            xSemaphoreGive(networkStateMutex);
        }

        if (wifiConnected)
        {
            dnsResult = WiFi.hostByName(testDomains[i], result);
        }
        else if (gsmConnected && modem)
        {
            // For GSM connections, we can try to resolve through the modem
            // Note: Some modems support DNS resolution, but this is a simpler check
            // We'll fall back to trying WiFi's DNS resolver which should work through any interface
            dnsResult = WiFi.hostByName(testDomains[i], result);
        }
        else
        {
            continue; // No valid connection method
        }

        if (dnsResult == 1)
        {
            log_v("DNS resolution successful for %s -> %s", testDomains[i], result.toString().c_str());
            return true;
        }
        else
        {
            log_v("DNS resolution failed for %s (error: %d)", testDomains[i], dnsResult);
        }
    }

    log_w("All DNS resolution tests failed - DNS/Internet connectivity issue detected");
    return false;
}

// Initialize network resources
static bool initializeNetworkResources(bool isUsingModem)
{
    log_i("Initializing network resources...");

    if (isUsingModem == true)
    {
        // Initialize GSM serial
        gsmSerial.begin(9600, SERIAL_8N1, MODEM_RX, MODEM_TX);
        delay(1000);

        // Create modem instance
        if (modem == NULL)
        {
            modem = new TinyGsm(gsmSerial);
            if (modem == NULL)
            {
                log_e("Failed to create TinyGsm instance");
                return false;
            }
        }

        // Create GSM client
        if (gsmClient == NULL)
        {
            gsmClient = new TinyGsmClient(*modem);
            if (gsmClient == NULL)
            {
                log_e("Failed to create TinyGsmClient instance");
                delete modem;
                modem = NULL;
                return false;
            }
        }

        // Create SSL client
        if (sslClient == NULL)
        {
            sslClient = new SSLClient(*gsmClient, TAs, (size_t)TAs_NUM, SSL_RAND_PIN, 1, SSLClient::SSL_ERROR);
            if (sslClient == NULL)
            {
                log_e("Failed to create SSLClient instance");
                delete gsmClient;
                delete modem;
                gsmClient = NULL;
                modem = NULL;
                return false;
            }
        }
    }
    else
    {
        if (sslClient == NULL)
        {
            sslClient = new SSLClient(wifi_base, TAs, (size_t)TAs_NUM, SSL_RAND_PIN, 1, SSLClient::SSL_ERROR);
            if (sslClient == NULL)
            {
                log_e("Failed to create SSLClient instance");
                delete gsmClient;
                delete modem;
                gsmClient = NULL;
                modem = NULL;
                return false;
            }
        }
    }

    log_i("Network resources initialized successfully");
    return true;
}

// Cleanup network resources
static void cleanupNetworkResources()
{
    log_i("Cleaning up network resources...");

    // Disconnect WiFi
    if (WiFi.status() == WL_CONNECTED)
    {
        WiFi.disconnect();
        WiFi.mode(WIFI_OFF);
        log_i("WiFi disconnected and turned off");
    }

    // Disconnect GSM
    if (modem && modem->isGprsConnected())
    {
        modem->gprsDisconnect();
        log_i("GPRS disconnected");
    }

    // Clean up SSL client
    if (sslClient)
    {
        sslClient->stop();
        delete sslClient;
        sslClient = NULL;
        log_d("SSLClient cleaned up");
    }

    // Clean up GSM client
    if (gsmClient)
    {
        delete gsmClient;
        gsmClient = NULL;
        log_d("GSMClient cleaned up");
    }

    // Clean up modem
    if (modem)
    {
        delete modem;
        modem = NULL;
        log_d("Modem cleaned up");
    }

    // Update state
    if (xSemaphoreTake(networkStateMutex, pdMS_TO_TICKS(1000)) == pdTRUE)
    {
        networkState.wifiConnected = false;
        networkState.gsmConnected = false;
        networkState.internetConnected = false;
        networkState.timeSync = false;
        networkState.connectionRetries = 0;
        xSemaphoreGive(networkStateMutex);
    }

    log_i("Network resources cleaned up successfully");
}

// WiFi connection handler
static bool handleWiFiConnection(deviceNetworkInfo_t *devInfo, systemStatus_t *sysStatus)
{
    log_i("Attempting WiFi connection to SSID: %s", devInfo->ssid.c_str());

    // Validate input parameters
    if (devInfo->ssid.length() == 0)
    {
        log_e("WiFi SSID is empty");
        return false;
    }

    // Set WiFi mode and power
    WiFi.mode(WIFI_STA);
    delay(1000);
    WiFi.setTxPower(devInfo->wifipow);
    log_i("WiFi power set to %d", devInfo->wifipow);

    updateDisplayStatus(devInfo, sysStatus, DISP_EVENT_CONN_TO_WIFI);

    for (int retry = 0; retry < MAX_CONNECTION_RETRIES; retry++)
    {
        log_i("WiFi connection attempt %d/%d", retry + 1, MAX_CONNECTION_RETRIES);

        // Scan for networks
        int networks = WiFi.scanNetworks();
        if (networks <= 0)
        {
            log_w("No networks found on attempt %d", retry + 1);
            updateDisplayStatus(devInfo, sysStatus, DISP_EVENT_NO_NETWORKS_FOUND);

            if (retry < MAX_CONNECTION_RETRIES - 1)
            {
                delay(NETWORK_RETRY_DELAY_MS);
            }
            continue;
        }

        log_i("Found %d networks", networks);

        // Look for target SSID
        bool ssidFound = false;
        int targetRSSI = -999;

        for (int i = 0; i < networks; i++)
        {
            String currentSSID = WiFi.SSID(i);
            int currentRSSI = WiFi.RSSI(i);

            log_v("Network %d: %s (RSSI: %d)", i, currentSSID.c_str(), currentRSSI);

            if (currentSSID == devInfo->ssid)
            {
                ssidFound = true;
                targetRSSI = currentRSSI;
                break;
            }
        }

        if (!ssidFound)
        {
            log_w("SSID '%s' not found in scan", devInfo->ssid.c_str());
            devInfo->noNet = "NO " + devInfo->ssid + "!";
            updateDisplayStatus(devInfo, sysStatus, DISP_EVENT_SSID_NOT_FOUND);

            if (retry < MAX_CONNECTION_RETRIES - 1)
            {
                delay(NETWORK_RETRY_DELAY_MS);
            }
            continue;
        }

        log_i("Target SSID found with RSSI: %d", targetRSSI);
        devInfo->foundNet = devInfo->ssid + " OK!";

        // Attempt connection
        WiFi.begin(devInfo->ssid.c_str(), devInfo->passw.c_str());

        unsigned long startTime = millis();
        wl_status_t wifiStatus = WL_IDLE_STATUS;

        while (((wifiStatus = WiFi.status()) != WL_CONNECTED) &&
               ((millis() - startTime) < WIFI_CONNECTION_TIMEOUT_MS))
        {
            delay(500);

            // Check for connection failures
            if ((wifiStatus == WL_CONNECT_FAILED) || (wifiStatus == WL_CONNECTION_LOST))
            {
                log_w("WiFi connection failed with status: %d", wifiStatus);
                break;
            }
        }

        if (WiFi.status() == WL_CONNECTED)
        {
            log_i("WiFi connected successfully");
            log_i("IP address: %s", WiFi.localIP().toString().c_str());
            log_i("Gateway: %s", WiFi.gatewayIP().toString().c_str());
            log_i("DNS: %s", WiFi.dnsIP().toString().c_str());

            // Update state - no mutex needed (internal state)
            networkState.wifiConnected = true;
            networkState.connectionRetries = 0;

            sysStatus->connection = true;
            sendNetworkEvent(NET_EVENT_CONNECTED);
            return true;
        }

        log_w("WiFi connection failed on attempt %d (Status: %d)", retry + 1, WiFi.status());
        WiFi.disconnect();

        if (retry < MAX_CONNECTION_RETRIES - 1)
        {
            devInfo->remain = String(MAX_CONNECTION_RETRIES - retry - 1) + " tries remain.";
            updateDisplayStatus(devInfo, sysStatus, DISP_EVENT_CONN_RETRY);
            delay(NETWORK_RETRY_DELAY_MS);
        }
    }

    log_e("WiFi connection failed after all retries");
    updateDisplayStatus(devInfo, sysStatus, DISP_EVENT_WIFI_DISCONNECTED);
    sysStatus->connection = false;

    // Update retry count - no mutex needed (internal state)
    networkState.connectionRetries++;

    return false;
}

// GSM connection handler
static bool handleGSMConnection(deviceNetworkInfo_t *devInfo, systemStatus_t *sysStatus)
{
    log_i("Attempting GSM connection with APN: %s", devInfo->apn.c_str());
    updateDisplayStatus(devInfo, sysStatus, DISP_EVENT_CONN_TO_GPRS);

    if (!modem)
    {
        log_e("Modem not initialized");
        return false;
    }

    // Validate APN
    if (devInfo->apn.length() == 0)
    {
        log_e("APN is empty");
        return false;
    }

    // Initialize/restart modem
    log_i("Initializing modem...");
    modem->restart();
    delay(3000); // Give modem time to initialize

    // Get modem information
    String modelName = modem->getModemName();
    String modemInfo = modem->getModemInfo();
    String imei = modem->getIMEI();

    log_i("Modem: %s", modelName.c_str());
    log_i("Info: %s", modemInfo.c_str());
    log_i("IMEI: %s", imei.c_str());

    // Check SIM card
    String ccid = modem->getSimCCID();
    String imsi = modem->getIMSI();

    if ((ccid.startsWith("ERROR")) || (imsi.startsWith("ERROR")) ||
        (ccid.length() < 10) || (imsi.length() < 10))
    {
        log_e("SIM card error - CCID: %s, IMSI: %s", ccid.c_str(), imsi.c_str());
        updateDisplayStatus(devInfo, sysStatus, DISP_EVENT_SIM_ERROR);
        return false;
    }

    log_i("SIM card detected - CCID: %s, IMSI: %s", ccid.c_str(), imsi.c_str());

    // Wait for cellular network
    log_i("Waiting for cellular network...");
    unsigned long networkStart = millis();
    bool networkFound = false;

    while (!networkFound && (millis() - networkStart) < GPRS_CONNECTION_TIMEOUT_MS)
    {
        networkFound = modem->waitForNetwork(5000);
        if (!networkFound)
        {
            log_w("Still waiting for network... (%lu ms elapsed)",
                  millis() - networkStart);
            delay(2000);
        }
    }

    if (!modem->isNetworkConnected())
    {
        log_e("Failed to connect to cellular network after timeout");
        updateDisplayStatus(devInfo, sysStatus, DISP_EVENT_NETWORK_ERROR);
        return false;
    }

    // Get network information
    String operatorName = modem->getOperator();
    int signalQuality = modem->getSignalQuality();

    log_i("Cellular network connected");
    log_i("Operator: %s", operatorName.c_str());
    log_i("Signal quality: %d", signalQuality);

    if (signalQuality < 5)
    {
        log_w("Low signal quality detected: %d", signalQuality);
    }

    // Connect to GPRS
    log_i("Connecting to GPRS with APN: %s", devInfo->apn.c_str());

    for (int retry = 0; retry < MAX_CONNECTION_RETRIES; retry++)
    {
        log_i("GPRS connection attempt %d/%d", retry + 1, MAX_CONNECTION_RETRIES);

        if (modem->gprsConnect(devInfo->apn.c_str(), "", ""))
        {
            // Verify connection
            IPAddress localIP = modem->localIP();
            log_i("GPRS connected successfully");
            log_i("Local IP: %s", localIP.toString().c_str());

            // Update state - no mutex needed (internal state)
            networkState.gsmConnected = true;
            networkState.connectionRetries = 0;

            sysStatus->connection = true;
            sendNetworkEvent(NET_EVENT_CONNECTED);
            return true;
        }

        log_w("GPRS connection failed, attempt %d/%d", retry + 1, MAX_CONNECTION_RETRIES);

        if (retry < MAX_CONNECTION_RETRIES - 1)
        {
            log_i("Retrying GPRS connection in %d ms...", NETWORK_RETRY_DELAY_MS);
            delay(NETWORK_RETRY_DELAY_MS);
        }
    }

    log_e("GPRS connection failed after all retries");
    updateDisplayStatus(devInfo, sysStatus, DISP_EVENT_GPRS_ERROR);
    sysStatus->connection = false;

    // Update retry count - no mutex needed (internal state)
    networkState.connectionRetries++;

    return false;
}

// DateTime synchronization
static bool syncDateTime(deviceNetworkInfo_t *devInfo, systemStatus_t *sysStatus, systemData_t *sysData)
{
    log_i("Synchronizing date and time...");
    updateDisplayStatus(devInfo, sysStatus, DISP_EVENT_RETREIVE_DATETIME);

    // Get timezone rule (will be used by configTzTime later)
    String tzRule = (sysData->timezone.length() > 0) ? sysData->timezone : TZ_DEFAULT;
    log_i("Using timezone: %s", tzRule.c_str());

#ifdef FAKE_NTP_TIME
    // Fake NTP sync for testing
    if (strlen(FAKE_NTP_TIME) > 0)
    {
        struct tm timeInfo;
        time_t now;
        time(&now);
        localtime_r(&now, &timeInfo);

        int year, month, day, hour, minute, second;
        static bool validTime = false;
        if (validTime == true)
        {
            // Update system status and network state like real NTP sync
            sysStatus->datetime = true;
            if (xSemaphoreTake(networkStateMutex, pdMS_TO_TICKS(1000)) == pdTRUE)
            {
                networkState.timeSync = true;
                xSemaphoreGive(networkStateMutex);
            }

            // Send the event that main loop is waiting for
            sendNetworkEvent(NET_EVENT_TIME_SYNCED);
            return true;
        }

        // Try full date-time format first: "YYYY-MM-DD HH:MM:SS"
        if (sscanf(FAKE_NTP_TIME, "%d-%d-%d %d:%d:%d", &year, &month, &day, &hour, &minute, &second) == 6)
        {
            if (year >= 2020 && year <= 2030 && month >= 1 && month <= 12 && day >= 1 && day <= 31 &&
                hour >= 0 && hour < 24 && minute >= 0 && minute < 60 && second >= 0 && second < 60)
            {
                timeInfo.tm_year = year - 1900;
                timeInfo.tm_mon = month - 1;
                timeInfo.tm_mday = day;
                validTime = true;
            }
        }
        // Fallback to time-only format: "HH:MM:SS" (uses current date)
        else if (sscanf(FAKE_NTP_TIME, "%d:%d:%d", &hour, &minute, &second) == 3)
        {
            if (hour >= 0 && hour < 24 && minute >= 0 && minute < 60 && second >= 0 && second < 60)
            {
                validTime = true;
            }
        }

        if (validTime)
        {
            timeInfo.tm_hour = hour;
            timeInfo.tm_min = minute;
            timeInfo.tm_sec = second;
            timeInfo.tm_isdst = -1;

            time_t fakeTime = mktime(&timeInfo);
            if (fakeTime > 0)
            {
                struct timeval tv = {fakeTime, 0};
                settimeofday(&tv, NULL);

                strftime(sysData->Date, sizeof(sysData->Date), "%d/%m/%Y", &timeInfo);
                strftime(sysData->Time, sizeof(sysData->Time), "%T", &timeInfo);
                sysData->currentDataTime = String(sysData->Date) + " " + String(sysData->Time);

                log_i("Fake time set: %s", sysData->currentDataTime.c_str());
                updateDisplayStatus(devInfo, sysStatus, DISP_EVENT_DATETIME_OK);

                // Update system status and network state like real NTP sync
                sysStatus->datetime = true;
                if (xSemaphoreTake(networkStateMutex, pdMS_TO_TICKS(1000)) == pdTRUE)
                {
                    networkState.timeSync = true;
                    xSemaphoreGive(networkStateMutex);
                }

                // Send the event that main loop is waiting for
                sendNetworkEvent(NET_EVENT_TIME_SYNCED);
                return true;
            }
        }
        log_w("Fake time setup failed, using real NTP");
    }
#endif

    // Validate NTP server
    String ntpServer = (sysData->ntp_server.length() > 0) ? sysData->ntp_server : NTP_SERVER_DEFAULT;
    log_i("Using NTP server: %s", ntpServer.c_str());

    struct tm timeInfo;
    bool timeObtained = false;

    for (int retry = 0; retry < TIME_SYNC_MAX_RETRY && !timeObtained; retry++)
    {
        log_i("Time sync attempt %d/%d", retry + 1, TIME_SYNC_MAX_RETRY);

        if ((sysStatus->use_modem) && (modem) && (modem->isGprsConnected()))
        {
            // Use GSM time sync
            log_i("Syncing time via GSM modem...");

            int year, month, day, hour, minute, second;
            float timezone;

            // Try NTP sync via modem
            if (modem->NTPServerSync(ntpServer, 0))
            {
                delay(2000); // Wait for sync to complete

                if (modem->getNetworkTime(&year, &month, &day, &hour, &minute, &second, &timezone))
                {
                    timeInfo.tm_year = year - 1900;
                    timeInfo.tm_mon = month - 1;
                    timeInfo.tm_mday = day;
                    timeInfo.tm_hour = hour;
                    timeInfo.tm_min = minute;
                    timeInfo.tm_sec = second;
                    timeInfo.tm_isdst = -1;

                    time_t epochTime = mktime(&timeInfo);
                    if (epochTime > 0)
                    {
                        struct timeval tv = {epochTime, 0};
                        settimeofday(&tv, NULL);
                        timeObtained = true;
                        log_i("Time obtained from GSM: %d-%02d-%02d %02d:%02d:%02d",
                              year, month, day, hour, minute, second);
                    }
                }
            }
        }
        else if (networkState.wifiConnected)
        {
            // Use WiFi NTP sync with proper timezone handling
            log_i("Syncing time via WiFi NTP...");

            // Use configTzTime instead of configTime to properly handle timezone
            // This prevents timezone conflicts that cause permanent time offset
            configTzTime(tzRule.c_str(), ntpServer.c_str());

            // Wait for time sync with timeout
            unsigned long syncStart = millis();
            while (!getLocalTime(&timeInfo) && (millis() - syncStart) < 10000)
            {
                delay(500);
            }

            if (getLocalTime(&timeInfo))
            {
                timeObtained = true;
                log_i("Time obtained from WiFi NTP: %d-%02d-%02d %02d:%02d:%02d",
                      timeInfo.tm_year + 1900, timeInfo.tm_mon + 1, timeInfo.tm_mday,
                      timeInfo.tm_hour, timeInfo.tm_min, timeInfo.tm_sec);
            }
        }

        if ((!timeObtained) && (retry < TIME_SYNC_MAX_RETRY - 1))
        {
            log_w("Time sync failed, retrying in 5 seconds...");
            delay(5000);
        }
    }

    if (timeObtained)
    {
        // Update system data
        strftime(sysData->Date, sizeof(sysData->Date), "%d/%m/%Y", &timeInfo);
        strftime(sysData->Time, sizeof(sysData->Time), "%T", &timeInfo);
        sysData->currentDataTime = String(sysData->Date) + " " + String(sysData->Time);

        log_i("Time synchronized successfully: %s", sysData->currentDataTime.c_str());
        updateDisplayStatus(devInfo, sysStatus, DISP_EVENT_DATETIME_OK);

        // Update state
        if (xSemaphoreTake(networkStateMutex, pdMS_TO_TICKS(1000)) == pdTRUE)
        {
            networkState.timeSync = true;
            xSemaphoreGive(networkStateMutex);
        }

        sysStatus->datetime = true;
        sendNetworkEvent(NET_EVENT_TIME_SYNCED);
        return true;
    }
    else
    {
        log_e("Failed to synchronize time after all retries");
        updateDisplayStatus(devInfo, sysStatus, DISP_EVENT_DATETIME_ERR);
        sysStatus->datetime = false;
        return false;
    }
}

// Helper function to ping server and check connectivity
static bool pingServer(const String &serverName)
{
    log_i("Pinging server to check connectivity: %s", serverName.c_str());

    // Create a simple HTTP client for ping
    HTTPClient pingClient;
    WiFiClientSecure *pingSSLClient = new WiFiClientSecure;

    if (!pingSSLClient)
    {
        log_e("Failed to create SSL client for server ping");
        return false;
    }

    // Configure SSL client (same settings as main transmission)
    pingSSLClient->setInsecure();                           // Skip certificate validation
    String pingURL = "https://" + serverName + "/api/ping"; // Try ping endpoint first

    pingClient.begin(*pingSSLClient, pingURL);
    pingClient.setTimeout(10000); // Short 10-second timeout for ping

    // Send a simple GET request to check server availability
    int httpCode = pingClient.GET();

    // If ping endpoint doesn't exist, try the main data endpoint with HEAD
    if (httpCode == 404)
    {
        pingClient.end();
        String dataURL = "https://" + serverName + "/api/data";
        pingClient.begin(*pingSSLClient, dataURL);
        httpCode = pingClient.sendRequest("HEAD", "");
    }

    bool serverAvailable = (httpCode > 0 && httpCode < 500); // Any response except server errors

    if (serverAvailable)
    {
        log_i("Server ping successful (HTTP %d) - server is responsive", httpCode);
    }
    else
    {
        log_w("Server ping failed (HTTP %d) - server may be down or overloaded", httpCode);
    }

    pingClient.end();
    delete pingSSLClient;

    return serverAvailable;
}

// Send data to server
static bool sendDataToServer(send_data_t *dataToSend, deviceNetworkInfo_t *devInfo,
                             systemStatus_t *sysStatus, systemData_t *sysData)
{
#ifdef FAKE_NTP_TIME
    // Skip data transmission when fake NTP is active
    if (strlen(FAKE_NTP_TIME) > 0)
    {
        log_i("FAKE_NTP_TIME active - skipping data transmission");
        sysData->sent_ok = true;
        sendNetworkEvent(NET_EVENT_DATA_SENT);
        return true;
    }
#endif

    if (!sslClient)
    {
        log_e("SSL client not initialized");
        return false;
    }

    if (!isNetworkConnected())
    {
        log_e("No network connection available");
        return false;
    }

    // Validate required parameters
    if ((devInfo->deviceid.length() == 0) || (sysData->server.length() == 0))
    {
        log_e("Missing required parameters: deviceid or server");
        return false;
    }

    log_i("Sending data to server: %s", sysData->server.c_str());
    log_i("Device ID: %s", devInfo->deviceid.c_str());

    // Step 1: Ping server to verify connectivity before data transmission
    if (!pingServer(sysData->server))
    {
        log_e("Server ping failed - aborting data transmission to prevent timeouts");
        return false;
    }

    log_i("Server ping successful - proceeding with data transmission");

    // Set SSL verification time
    time_t epochTime = mktime(&dataToSend->sendTimeInfo);
    if (epochTime <= 0)
    {
        log_e("Invalid timestamp in data to send");
        return false;
    }

    sslClient->setVerificationTime((epochTime / 86400UL) + 719528UL, epochTime % 86400UL);

    // Build POST data string
    String postData = "X-MSP-ID=" + devInfo->deviceid;

    // Add firmware version (strip 'v' prefix if present)
    String fwVersion = String(VERSION_STRING);
    if (fwVersion.startsWith("v")) {
        fwVersion = fwVersion.substring(1);
    }
    postData += "&firmwareVersion=" + fwVersion;

    // Add sensor data - match original working logic by including all available data
    // BME680 data (always include if temperature is in reasonable range)
    if ((dataToSend->temp > -50.0) && (dataToSend->temp < 85.0))
    {
        postData += "&temp=" + String(dataToSend->temp, 3);
        postData += "&hum=" + String(dataToSend->hum, 3);
        postData += "&pre=" + String(dataToSend->pre, 3);
        postData += "&voc=" + String(dataToSend->VOC, 3);
        log_d("Added BME680 data: T=%.3f, H=%.3f, P=%.3f, VOC=%.3f",
              dataToSend->temp, dataToSend->hum, dataToSend->pre, dataToSend->VOC);
    }

    // MICS6814 data (include if any gas reading is positive)
    if ((dataToSend->MICS_CO >= 0.0) || (dataToSend->MICS_NO2 >= 0.0) || (dataToSend->MICS_NH3 >= 0.0))
    {
        postData += "&cox=" + String(dataToSend->MICS_CO, 3);
        postData += "&nox=" + String(dataToSend->MICS_NO2, 3);
        postData += "&nh3=" + String(dataToSend->MICS_NH3, 3);
        log_d("Added MICS6814 data: CO=%.3f, NO2=%.3f, NH3=%.3f",
              dataToSend->MICS_CO, dataToSend->MICS_NO2, dataToSend->MICS_NH3);
    }

    // PMS5003 data (include if any PM reading is positive)
    if ((dataToSend->PM1 >= 0) || (dataToSend->PM25 >= 0) || (dataToSend->PM10 >= 0))
    {
        postData += "&pm1=" + String(dataToSend->PM1);
        postData += "&pm25=" + String(dataToSend->PM25);
        postData += "&pm10=" + String(dataToSend->PM10);
        log_d("Added PMS5003 data: PM1=%d, PM2.5=%d, PM10=%d",
              dataToSend->PM1, dataToSend->PM25, dataToSend->PM10);
    }

    // O3 data (include if reading is positive)
    if (dataToSend->ozone >= 0.0)
    {
        postData += "&o3=" + String(dataToSend->ozone, 3);
        log_d("Added O3 data: %.3f", dataToSend->ozone);
    }

    postData += "&msp=" + String(dataToSend->MSP);
    postData += "&recordedAt=" + String(epochTime);

    log_d("POST data length: %d bytes", postData.length());

    // Server communication with enhanced response logging

    // Attempt server connection with retries
    for (int retry = 0; retry < MAX_CONNECTION_RETRIES; retry++)
    {
        log_i("Server connection attempt %d/%d", retry + 1, MAX_CONNECTION_RETRIES);
        bool wasSSLTimeout = false; // Track if this attempt had SSL timeout

        // Check if we still have network connectivity
        if (!isNetworkConnected())
        {
            log_e("Network connection lost during server communication");
            return false;
        }

        // Use HTTPS (port 443) with SSL client
        if (sslClient && sslClient->connect(sysData->server.c_str(), 443))
        {
            log_i("Connected to server successfully via HTTPS");

            // Build HTTP request
            String httpRequest = "POST /api/v1/records HTTP/1.1\r\n";
            httpRequest += "Host: " + sysData->server + "\r\n";
            httpRequest += "Authorization: Bearer " + sysData->api_secret_salt + ":" + devInfo->deviceid + "\r\n";
            httpRequest += "Connection: close\r\n";
            httpRequest += "User-Agent: MilanoSmartPark/0.2\r\n";
            httpRequest += "Content-Type: application/x-www-form-urlencoded\r\n";
            httpRequest += "Content-Length: " + String(postData.length()) + "\r\n";
            httpRequest += "\r\n";
            httpRequest += postData;

            log_d("HTTP request size: %d bytes", httpRequest.length());

            // Send request
            size_t written = sslClient->print(httpRequest);
            bool dataSentSuccessfully = (written == httpRequest.length());

            if (!dataSentSuccessfully)
            {
                log_w("Incomplete request sent: %d/%d bytes", written, httpRequest.length());
            }
            else
            {
                log_i("HTTP request sent successfully (%d bytes)", written);
            }
            sslClient->flush();

            // Read response with proper timeout handling
            String response = "";
            unsigned long responseStart = millis();
            bool dataReceived = false;
            bool headerCompleted = false;

            log_i("Waiting for server response (timeout: %d ms)...", SERVER_RESPONSE_TIMEOUT_MS);

            // Wait for response and read until no more data or timeout
            while (millis() - responseStart < SERVER_RESPONSE_TIMEOUT_MS)
            {
                if (sslClient->available())
                {
                    dataReceived = true;
                    char c = sslClient->read();
                    response += c;

                    // Mark when we've read the HTTP headers
                    if (!headerCompleted && response.indexOf("\r\n\r\n") >= 0)
                    {
                        headerCompleted = true;
                        log_i("HTTP headers received after %lu ms", millis() - responseStart);
                        // Don't break - continue reading body!
                    }
                }
                else
                {
                    // No data available - check if we got headers and should wait for body
                    if (headerCompleted)
                    {
                        // Give a short grace period for body data to arrive
                        delay(50);
                        // Check again
                        if (!sslClient->available())
                        {
                            // No more data coming, we're done
                            break;
                        }
                    }
                    else
                    {
                        // Still waiting for headers
                        delay(10);
                    }
                }
            }

            unsigned long responseTime = millis() - responseStart;
            sslClient->stop();

            // Enhanced response analysis and logging
            log_i("Server response analysis:");
            log_i("  - Response time: %lu ms", responseTime);
            log_i("  - Data received: %s", dataReceived ? "YES" : "NO");
            log_i("  - Headers completed: %s", headerCompleted ? "YES" : "NO");
            log_i("  - Response length: %d bytes", response.length());

            if (response.length() > 0)
            {
                // Extract status line for detailed logging
                int statusLineEnd = response.indexOf('\r');
                if (statusLineEnd > 0)
                {
                    String statusLine = response.substring(0, statusLineEnd);
                    log_i("  - Status line: %s", statusLine.c_str());
                }

                // Extract and print the response body (after headers)
                int bodyStart = response.indexOf("\r\n\r\n");
                if (bodyStart >= 0 && (bodyStart + 4) < response.length())
                {
                    String body = response.substring(bodyStart + 4);
                    log_i("  - Response body (%d bytes):", body.length());

                    // Print body in chunks to avoid log truncation
                    // ESP32 log buffer is typically ~256 bytes, so split if needed
                    int chunk_size = 200;
                    for (int i = 0; i < body.length(); i += chunk_size)
                    {
                        int end = min(i + chunk_size, (int)body.length());
                        log_i("    %s", body.substring(i, end).c_str());
                    }
                }
                else
                {
                    // Fallback: print first part of response if body not found
                    log_i("  - Response preview (first 300 chars): %s", response.substring(0, 300).c_str());
                }
            }
            else
            {
                log_e("  - EMPTY RESPONSE! This indicates a timeout or SSL failure");
            }

            // Response validation - same logic for all times
            if (response.startsWith("HTTP/1.1 200", 0) || response.startsWith("HTTP/1.1 201", 0))
            {
                log_i("SUCCESS: Data uploaded successfully! Status: %s",
                      response.substring(0, response.indexOf('\r')).c_str());

                // Extract and store server configuration response
                int bodyStart = response.indexOf("\r\n\r\n");
                if (bodyStart >= 0 && (bodyStart + 4) < response.length())
                {
                    String body = response.substring(bodyStart + 4);
                    if (body.length() > 0)
                    {
                        log_i("Storing server configuration response (%d bytes)", body.length());
                        sysData->server_config_response = body;
                        sysData->server_config_received = true;

                        // Send config to main loop via queue
                        server_config_msg_t configMsg;
                        configMsg.valid = true;
                        configMsg.response_length = min((size_t)body.length(), sizeof(configMsg.json_response) - 1);
                        strncpy(configMsg.json_response, body.c_str(), configMsg.response_length);
                        configMsg.json_response[configMsg.response_length] = '\0';

                        // Overwrite any old config (queue size is 1)
                        if (serverConfigQueue != NULL)
                        {
                            xQueueOverwrite(serverConfigQueue, &configMsg);
                            log_i("Server config sent to main loop via queue");
                        }
                        else
                        {
                            log_w("Server config queue not available");
                        }
                    }
                }

                sysData->sent_ok = true;
                sendNetworkEvent(NET_EVENT_DATA_SENT);
                return true;
            }
            else if (response.length() == 0)
            {
                log_e("TIMEOUT: No response received - likely SSL timeout or connection issue");
                log_e("This could be due to server overload, network issues, or SSL problems");
                wasSSLTimeout = true; // Mark this attempt as SSL timeout

                // Smart assumption logic: If server ping was successful AND data was sent completely,
                // assume the data reached the server even though we didn't get a response
                if (dataSentSuccessfully)
                {
                    log_w("SMART SUCCESS: Server ping was OK and data sent completely");
                    log_w("Assuming server received data despite timeout response - preventing duplicates");
                    sysData->sent_ok = true;
                    sendNetworkEvent(NET_EVENT_DATA_SENT);
                    return true;
                }
                else
                {
                    log_e("Data transmission was incomplete - genuine failure, will retry");
                    // Continue with normal retry logic
                }
            }
            else if (response.startsWith("HTTP/1.1", 0))
            {
                // We got an HTTP response but it's not successful
                String statusLine = response.substring(0, response.indexOf('\r'));
                log_e("HTTP ERROR: Server returned error status: %s", statusLine.c_str());

                // Log response body if available for debugging
                if (response.indexOf("Content-Length:") >= 0)
                {
                    int bodyStart = response.indexOf("\r\n\r\n");
                    if (bodyStart >= 0 && bodyStart + 4 < response.length())
                    {
                        String body = response.substring(bodyStart + 4);
                        log_e("Response body: %s", body.substring(0, 300).c_str());
                    }
                }
                // Continue to retry
            }
            else
            {
                log_e("INVALID RESPONSE: Corrupted or invalid response format");
                log_e("Response starts with: %s", response.substring(0, 50).c_str());
                // Continue to retry
            }
        }
        else
        {
            log_w("Failed to connect to server via HTTP (attempt %d)", retry + 1);
        }

        if (retry < MAX_CONNECTION_RETRIES - 1)
        {
            // Progressive delay for SSL timeout retries - give server more time to recover
            int retryDelay = NETWORK_RETRY_DELAY_MS;
            if (wasSSLTimeout) // SSL timeout case
            {
                retryDelay = NETWORK_RETRY_DELAY_MS * (retry + 2); // 2x, 3x delay for timeouts
                log_i("SSL timeout - using extended retry delay: %d ms", retryDelay);
            }
            else
            {
                log_i("Retrying in %d ms...", retryDelay);
            }
            delay(retryDelay);
        }
    }

    log_e("Failed to send data after all retries");
    sysData->sent_ok = false;
    return false;
}

// Main network task
static void networkTask(void *pvParameters)
{
    log_i("Network Task started on core %d", xPortGetCoreID());

    // Use global data structures if available, otherwise create local defaults
    deviceNetworkInfo_t devInfo;
    systemStatus_t sysStatus;
    systemData_t sysData;

    if (globalSysData && globalSysStatus && globalDevInfo)
    {
        // Copy from global structures (with mutex protection)
        vMspOs_takeDataAccessMutex();
        devInfo = *globalDevInfo;
        sysStatus = *globalSysStatus;
        sysData = *globalSysData;
        vMspOs_giveDataAccessMutex();

        log_i("Network task using global data structures");
        log_i("Server: %s, Server OK: %d", sysData.server.c_str(), sysStatus.server_ok);
    }
    else
    {
        // Fallback to local initialization
        log_w("Global data structures not available, using local defaults");
        devInfo = {};
        sysStatus = {};
        sysData = {};

        // Initialize default values
        devInfo.wifipow = WIFI_POWER_17dBm;
        sysData.ntp_server = NTP_SERVER_DEFAULT;
        sysData.timezone = TZ_DEFAULT;

        // Set compile-time defaults
        sysData.server = __API_SERVER;
        sysStatus.server_ok = true;
        sysData.api_secret_salt = __API_SECRET_SALT;
    }

    // Load initial configuration from SD card
    sensorData_t sensorData = {};
    deviceMeasurement_t measStat = {};
    loadNetworkConfiguration(&devInfo, &sysStatus, &sysData, &sensorData, &measStat);

    // Initialize network resources
    if (!initializeNetworkResources(sysStatus.use_modem))
    {
        log_e("Failed to initialize network resources, task exiting");

        // Mark task as not running
        if (xSemaphoreTake(networkStateMutex, pdMS_TO_TICKS(1000)) == pdTRUE)
        {
            networkState.taskRunning = false;
            xSemaphoreGive(networkStateMutex);
        }

        vTaskDelete(NULL);
        return;
    }

    // Print MAC address for identification
    uint8_t baseMac[6];
    esp_read_mac(baseMac, ESP_MAC_WIFI_STA);
    sprintf(devInfo.baseMacChr, "%02X:%02X:%02X:%02X:%02X:%02X",
            baseMac[0], baseMac[1], baseMac[2], baseMac[3], baseMac[4], baseMac[5]);
    log_i("WiFi MAC: %s", devInfo.baseMacChr);
    updateDisplayStatus(&devInfo, &sysStatus, DISP_EVENT_WIFI_MAC_ADDR);

    // Initialize state machine
    updateNetworkState(NETWRK_EVT_WAIT);

    log_i("Network task initialized, entering main loop");

    // Main task loop
    while (1)
    {
        // Update current state from next state - no mutex needed (internal state)
        networkState.currentState = networkState.nextState;

        netwkr_task_evt_t currentState = getNetworkState();

        switch (currentState)
        {
        case NETWRK_EVT_WAIT:
        {
            // Wait for events or periodic maintenance
            EventBits_t events = xEventGroupWaitBits(
                networkEventGroup,
                NET_EVT_DATA_READY | NET_EVT_TIME_SYNC_REQ | NET_EVT_CONNECT_REQ | NET_EVT_DISCONNECT_REQ | NET_EVT_CONFIG_UPDATED,
                pdFALSE,             // DON'T clear bits on exit - we'll clear manually after processing
                pdFALSE,             // Wait for any bit
                pdMS_TO_TICKS(30000) // 30 second timeout for periodic checks
            );

            if (events & NET_EVT_CONFIG_UPDATED)
            {
                log_i("Configuration update request received");
                // Reload configuration from SD card
                sensorData_t sensorData = {};
                deviceMeasurement_t measStat = {};
                loadNetworkConfiguration(&devInfo, &sysStatus, &sysData, &sensorData, &measStat);

                // Clear the processed event bit
                xEventGroupClearBits(networkEventGroup, NET_EVT_CONFIG_UPDATED);
            }
            else if (events & NET_EVT_CONNECT_REQ)
            {
                log_i("Connection request received");
                updateNetworkState(NETWRK_EVT_INIT_CONNECTION);

                // Clear the processed event bit
                xEventGroupClearBits(networkEventGroup, NET_EVT_CONNECT_REQ);
            }
            else if (events & NET_EVT_TIME_SYNC_REQ)
            {
                log_i("Manual time sync request received (redundant - time syncs automatically after connection)");
                // Time sync now happens automatically after connection, but we'll honor manual requests
                updateNetworkState(NETWRK_EVT_SYNC_DATETIME);

                // Clear the processed event bit
                xEventGroupClearBits(networkEventGroup, NET_EVT_TIME_SYNC_REQ);
            }
            else if (events & NET_EVT_DATA_READY)
            {
                log_i("*** NET_EVT_DATA_READY event received - transitioning to UPDATE_DATA state");
                log_i("Queue has %d items waiting for processing", uxQueueMessagesWaiting(sendDataQueue));
                updateNetworkState(NETWRK_EVT_UPDATE_DATA);
                // Note: NET_EVT_DATA_READY will be cleared manually in NETWRK_EVT_UPDATE_DATA case after processing
            }
            else if (events & NET_EVT_DISCONNECT_REQ)
            {
                log_i("Disconnect request received");
                updateNetworkState(NETWRK_EVT_DEINIT_CONNECTION);

                // Clear the processed event bit
                xEventGroupClearBits(networkEventGroup, NET_EVT_DISCONNECT_REQ);
            }
            else
            {
                // Timeout occurred - perform periodic maintenance
                log_v("Network task periodic check");

                // PRIORITY: Check if queue has accumulated items that need processing
                int queueSize = uxQueueMessagesWaiting(sendDataQueue);
                if (queueSize > 0)
                {
                    log_w("PERIODIC CHECK: Found %d items in queue that need processing!", queueSize);
                    log_w("Triggering immediate queue processing...");
                    xEventGroupSetBits(networkEventGroup, NET_EVT_DATA_READY);
                    // Continue with normal maintenance but queue processing will be prioritized
                }

                // Check connection health
                bool wifiConnected = (WiFi.status() == WL_CONNECTED);
                bool gsmConnected = (modem && modem->isGprsConnected());

                // Check internet connectivity (DNS resolution test)
                // Skip connectivity test if firmware download is in progress to avoid interference
                bool internetConnected = false;
                if ((wifiConnected || gsmConnected) && !networkState.firmwareDownloadInProgress)
                {
                    internetConnected = testInternetConnectivity();
                }
                else if (networkState.firmwareDownloadInProgress)
                {
                    // Keep current internet status during download to avoid disruption
                    internetConnected = networkState.internetConnected;
                    log_v("Skipping connectivity test - firmware download in progress");
                }

                if (xSemaphoreTake(networkStateMutex, pdMS_TO_TICKS(100)) == pdTRUE)
                {
                    // Update connection status if changed
                    if (networkState.wifiConnected != wifiConnected)
                    {
                        networkState.wifiConnected = wifiConnected;
                        log_i("WiFi connection status changed: %s", wifiConnected ? "connected" : "disconnected");
                    }

                    if (networkState.gsmConnected != gsmConnected)
                    {
                        networkState.gsmConnected = gsmConnected;
                        log_i("GSM connection status changed: %s", gsmConnected ? "connected" : "disconnected");
                    }

                    if (networkState.internetConnected != internetConnected)
                    {
                        networkState.internetConnected = internetConnected;
                        log_i("Internet connectivity status changed: %s", internetConnected ? "connected" : "disconnected");

                        // Alert if network is connected but internet is not accessible (DNS issues)
                        if (!internetConnected && (wifiConnected || gsmConnected))
                        {
                            log_w("Network connected but internet not accessible - possible DNS issues");
                        }
                    }

                    xSemaphoreGive(networkStateMutex);
                }
            }
            break;
        }

        case NETWRK_EVT_INIT_CONNECTION:
        {
            log_i("Initializing network connection...");
            bool connected = false;

            // Check retry limits
            int currentRetries = 0;
            if (xSemaphoreTake(networkStateMutex, pdMS_TO_TICKS(1000)) == pdTRUE)
            {
                currentRetries = networkState.connectionRetries;
                xSemaphoreGive(networkStateMutex);
            }

            if (currentRetries >= MAX_CONNECTION_RETRIES)
            {
                log_w("Maximum connection retries reached, backing off...");
                delay(30000); // 30 second backoff

                // Reset retry counter
                if (xSemaphoreTake(networkStateMutex, pdMS_TO_TICKS(1000)) == pdTRUE)
                {
                    networkState.connectionRetries = 0;
                    xSemaphoreGive(networkStateMutex);
                }
            }

            if (!sysStatus.use_modem)
            {
                // Try WiFi connection
                log_i("Attempting WiFi connection...");
                connected = handleWiFiConnection(&devInfo, &sysStatus);
            }
            else
            {
                // Try GSM connection
                log_i("Attempting GSM connection...");
                connected = handleGSMConnection(&devInfo, &sysStatus);
            }

            if (connected)
            {
                log_i("Network connection established successfully");
            }
            else
            {
                log_w("Network connection failed, returning to wait state");
                // Exponential backoff delay
                int backoffDelay = NETWORK_RETRY_DELAY_MS * (1 << min(currentRetries, 4));
                delay(backoffDelay);
            }

            updateNetworkState(NETWRK_EVT_WAIT);
            break;
        }

        case NETWRK_EVT_SYNC_DATETIME:
        {
            log_i("Synchronizing date and time...");

            // Only sync if we have a connection
            if (!isNetworkConnected())
            {
                log_w("No network connection for time sync");
                updateNetworkState(NETWRK_EVT_INIT_CONNECTION);
                break;
            }

            if (syncDateTime(&devInfo, &sysStatus, &sysData))
            {
                log_i("Time synchronization successful");

                // Reset NTP sync counter on successful sync
                if (xSemaphoreTake(networkStateMutex, pdMS_TO_TICKS(1000)) == pdTRUE)
                {
                    networkState.ntpSyncExpired = NTP_SYNC_TX_COUNT;
                    xSemaphoreGive(networkStateMutex);
                }
            }
            else
            {
                log_w("Time synchronization failed, but continuing...");
            }

            updateNetworkState(NETWRK_EVT_WAIT);
            break;
        }

        case NETWRK_EVT_UPDATE_DATA:
        {
            log_i("*** NETWRK_EVT_UPDATE_DATA triggered - Processing data for transmission...");

            // Handle connection state based on NTP sync expiration
            bool needsConnection = false;
            bool needsTimeSync = false;

            // No mutex needed - internal state access within network task
            if ((!networkState.wifiConnected) && (!networkState.gsmConnected))
            {
                // Not connected, check if we need time sync
                if (networkState.configurationLoaded)
                {
                    networkState.ntpSyncExpired--;

                    if (networkState.ntpSyncExpired <= 0)
                    {
                        log_i("NTP sync expired, full reconnection with time sync needed");
                        networkState.ntpSyncExpired = NTP_SYNC_TX_COUNT; // Reset counter
                        needsConnection = true;
                        needsTimeSync = true;
                    }
                    else
                    {
                        log_i("Connect without NTP sync (count: %d)", networkState.ntpSyncExpired);
                        needsConnection = true;
                        needsTimeSync = false;
                    }
                }
            }

            // Check queue size BEFORE handling connection requirements
            int currentQueueSize = uxQueueMessagesWaiting(sendDataQueue);
            if (currentQueueSize > 0)
            {
                log_i("Queue contains %d items that need processing", currentQueueSize);

                // If queue is getting full (>75% capacity), prioritize processing over connection management
                if (currentQueueSize >= (SEND_DATA_QUEUE_LENGTH * 3 / 4))
                {
                    log_w("Queue is %d/%d (>75%% full) - prioritizing queue processing over connection management",
                          currentQueueSize, SEND_DATA_QUEUE_LENGTH);
                }
            }

            // Handle connection requirements - but don't abandon queue processing
            if (needsConnection)
            {
                if (needsTimeSync)
                {
                    log_i("NTP sync needed - will attempt quick connection then process queue");
                    updateNetworkState(NETWRK_EVT_INIT_CONNECTION);
                    // Don't break here - let connection establish then return to process queue
                    break;
                }
                else
                {
                    log_i("Connection needed - will attempt quick connection then process queue");
                    updateNetworkState(NETWRK_EVT_INIT_CONNECTION);
                    // Don't break here - let connection establish then return to process queue
                    break;
                }
            }

            // Check if we have a connection for data transmission
            if (!isNetworkConnected())
            {
                log_w("No network connection available for data transmission");

                // IMPORTANT: Don't abandon the queue! Try to establish connection but keep event active
                if (currentQueueSize > 0)
                {
                    log_w("Queue has %d items waiting - will attempt connection then retry queue processing", currentQueueSize);

                    // Set a flag or re-trigger the event after connection attempt
                    updateNetworkState(NETWRK_EVT_INIT_CONNECTION);

                    // Re-trigger data processing event to ensure queue gets processed after connection
                    log_i("Re-triggering NET_EVT_DATA_READY to ensure queue processing after connection");
                    xEventGroupSetBits(networkEventGroup, NET_EVT_DATA_READY);
                    break;
                }
                else
                {
                    log_i("No queued data, connection can wait");
                    updateNetworkState(NETWRK_EVT_INIT_CONNECTION);
                    break;
                }
            }

            // Process all queued data with detailed timing analysis
            int processedCount = 0;
            int failedCount = 0;
            send_data_t currentData;

            int initialQueueSize = uxQueueMessagesWaiting(sendDataQueue);
            struct tm currentTime;
            String processingTimeStr = "UNKNOWN";
            if (getLocalTime(&currentTime))
            {
                char timeBuffer[32];
                strftime(timeBuffer, sizeof(timeBuffer), "%H:%M:%S", &currentTime);
                processingTimeStr = String(timeBuffer);
            }

            log_i("=== QUEUE PROCESSING START ===");
            log_i("Processing time: %s (minute: %02d)", processingTimeStr.c_str(), currentTime.tm_min);
            log_i("Initial queue size: %d items", initialQueueSize);

            if (initialQueueSize > 1)
            {
                log_w("MULTIPLE DATA ITEMS DETECTED! This may cause duplicate transmissions at peak times");
                log_w("Queue contains %d items - each will be processed individually", initialQueueSize);
            }

            while (dequeueSendData(&currentData, 0))
            { // Non-blocking dequeue
                processedCount++;
                int remainingItems = uxQueueMessagesWaiting(sendDataQueue);

                log_i("=== PROCESSING ITEM %d/%d ===", processedCount, initialQueueSize);
                log_i("Queue items remaining: %d", remainingItems);

                // Analyze the data timestamp vs current time
                struct tm dataTime = currentData.sendTimeInfo;
                log_i("Data timestamp: %02d:%02d:%02d, Current time: %s",
                      dataTime.tm_hour, dataTime.tm_min, dataTime.tm_sec, processingTimeStr.c_str());

                // Check if this data originated from a peak time
                bool dataFromPeakTime = (dataTime.tm_min == 0 || dataTime.tm_min == 30);
                if (dataFromPeakTime)
                {
                    log_w("DATA FROM PEAK TIME DETECTED (minute %02d) - extra care needed for duplicates", dataTime.tm_min);
                }

                // Send data to server if connection and time sync are OK
                // No mutex needed - internal state access within network task
                bool canSendData = ((networkState.wifiConnected) || (networkState.gsmConnected)) &&
                                   (networkState.timeSync) && (sysStatus.server_ok);

                // Debug logging to trace connection conditions
                log_i("Connection check: WiFi=%d, GSM=%d, TimeSync=%d, ServerOK=%d",
                      networkState.wifiConnected, networkState.gsmConnected,
                      networkState.timeSync, sysStatus.server_ok);

                if (canSendData)
                {
                    // Show upload status on display
                    updateDisplayStatus(&devInfo, &sysStatus, DISP_EVENT_URL_UPLOAD_STAT);

                    if (sendDataToServer(&currentData, &devInfo, &sysStatus, &sysData))
                    {
                        processedCount++;
                        log_i("Data item %d sent successfully to server", processedCount);

                        // The sendDataToServer function already sends NET_EVENT_DATA_SENT
                        // and updates sysData->sent_ok = true when successful
                    }
                    else
                    {
                        failedCount++;
                        log_e("Failed to send data item to server, re-queuing for later retry");

                        // Send network error event
                        sendNetworkEvent(NET_EVENT_ERROR);
                        updateDisplayStatus(&devInfo, &sysStatus, DISP_EVENT_NETWORK_ERROR);

                        // Re-queue the data for later retry (with timeout to avoid blocking)
                        if (!enqueueSendData(currentData, pdMS_TO_TICKS(1000)))
                        {
                            log_e("Failed to re-queue data, data lost!");
                        }

                        // Stop processing on failure to avoid continuous failures
                        break;
                    }
                }
                else
                {
                    log_w("Cannot send data - conditions not met: WiFi=%d, GSM=%d, TimeSync=%d, ServerOK=%d",
                          networkState.wifiConnected, networkState.gsmConnected,
                          networkState.timeSync, sysStatus.server_ok);
                }

                // SD card logging removed from here - data is now logged immediately after sensor reading
                // in msp-firmware.ino before enqueueing, to ensure data is always logged regardless of
                // network status and to prevent duplicate logging when queue is processed
                log_i("SD card logging already completed at measurement time - skipping duplicate log");

                // Print measurements to serial
                log_d("Printing measurements to serial...");
                sensorData_t localSensorData;
                memset(&localSensorData, 0, sizeof(sensorData_t));
                vHalSensor_printMeasurementsOnSerial(&currentData, &localSensorData);

                // Handle modem disconnection for power saving
                if ((sysStatus.use_modem) && ((networkState.gsmConnected) || (modem)))
                {
                    if (vHalNetwork_modemDisconnect())
                    {
                        if (xSemaphoreTake(networkStateMutex, pdMS_TO_TICKS(1000)) == pdTRUE)
                        {
                            networkState.gsmConnected = false;
                            xSemaphoreGive(networkStateMutex);
                        }
                        log_i("Modem disconnected to save power");
                    }
                }

                // Small delay between transmissions
                delay(100);
            }

            // Queue processing completion summary
            log_i("=== QUEUE PROCESSING COMPLETE ===");
            if (processedCount > 0)
            {
                log_i("Successfully processed: %d data items", processedCount);
            }

            if (failedCount > 0)
            {
                log_w("Failed to process: %d data items", failedCount);
            }

            int finalQueueSize = uxQueueMessagesWaiting(sendDataQueue);
            log_i("Final queue size: %d items (started with %d)", finalQueueSize, initialQueueSize);

            if (initialQueueSize > 1 && processedCount > 1)
            {
                log_w("MULTIPLE TRANSMISSIONS COMPLETED: %d items processed from peak/near-peak time", processedCount);
                log_w("This explains why you see multiple sends in server logs at 00/30 minutes");
                log_w("Consider implementing data aggregation or queue deduplication to reduce server load");
            }

            // Manually clear the NET_EVT_DATA_READY bit now that we've finished processing all data
            xEventGroupClearBits(networkEventGroup, NET_EVT_DATA_READY);
            log_d("NET_EVT_DATA_READY bit cleared after processing %d items", processedCount + failedCount);

            updateNetworkState(NETWRK_EVT_WAIT);
            break;
        }

        case NETWRK_EVT_DEINIT_CONNECTION:
        {
            // Check if firmware operation is in progress - if so, skip disconnection
            if (networkState.firmwareDownloadInProgress)
            {
                log_i("Firmware operation in progress - skipping network disconnection");
                updateNetworkState(NETWRK_EVT_WAIT);
                break;
            }

            log_i("Deinitializing network connections...");

            // Disconnect WiFi
            if (WiFi.status() == WL_CONNECTED)
            {
                WiFi.disconnect();
                WiFi.mode(WIFI_OFF);
                log_i("WiFi disconnected and turned off");
            }

            // Disconnect GSM
            if (modem && modem->isGprsConnected())
            {
                if (modem->gprsDisconnect())
                {
                    log_i("GPRS disconnected successfully");
                }
                else
                {
                    log_w("GPRS disconnect failed");
                }
            }

            // Update connection states
            if (xSemaphoreTake(networkStateMutex, pdMS_TO_TICKS(1000)) == pdTRUE)
            {
                networkState.wifiConnected = false;
                networkState.gsmConnected = false;
                networkState.timeSync = false;
                networkState.connectionRetries = 0;
                xSemaphoreGive(networkStateMutex);
            }

            sysStatus.connection = false;
            log_i("Network deinitialization completed");

            sendNetworkEvent(NET_EVENT_DISCONNECTED);
            updateNetworkState(NETWRK_EVT_WAIT);
            break;
        }

        default:
        {
            log_w("Unknown network state: %d, returning to wait state", currentState);
            updateNetworkState(NETWRK_EVT_WAIT);
            break;
        }
        }

        // Small delay to prevent excessive CPU usage and allow other tasks to run
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    // This should never be reached, but cleanup just in case
    log_w("Network task exiting unexpectedly");
    cleanupNetworkResources();

    // Mark task as not running
    if (xSemaphoreTake(networkStateMutex, pdMS_TO_TICKS(1000)) == pdTRUE)
    {
        networkState.taskRunning = false;
        xSemaphoreGive(networkStateMutex);
    }

    vTaskDelete(NULL);
}

// Utility functions
void vHalNetwork_printWiFiMACAddr(systemStatus_t *p_tSys, deviceNetworkInfo_t *p_tDev)
{
    if (!p_tSys || !p_tDev)
    {
        log_e("Invalid parameters for MAC address function");
        return;
    }

    uint8_t baseMac[6];
    esp_read_mac(baseMac, ESP_MAC_WIFI_STA);
    sprintf(p_tDev->baseMacChr, "%02X:%02X:%02X:%02X:%02X:%02X",
            baseMac[0], baseMac[1], baseMac[2], baseMac[3], baseMac[4], baseMac[5]);

    log_i("WiFi MAC Address: %s", p_tDev->baseMacChr);
    updateDisplayStatus(p_tDev, p_tSys, DISP_EVENT_WIFI_MAC_ADDR);
}

// Load network configuration from SD card
static bool loadNetworkConfiguration(deviceNetworkInfo_t *devInfo, systemStatus_t *sysStatus,
                                     systemData_t *sysData, sensorData_t *sensorData,
                                     deviceMeasurement_t *measStat)
{
    log_i("Loading network configuration from SD card...");

    // Initialize SD card if not already done
    if (!initializeSD(sysStatus, devInfo))
    {
        log_e("Failed to initialize SD card");
        return false;
    }

    // Load configuration from SD card
    bool configLoaded = checkConfig(CONFIG_PATH, devInfo, sensorData, measStat, sysStatus, sysData);

    if (configLoaded)
    {
        log_i("Network configuration loaded successfully");
        log_i("WiFi SSID: %s", devInfo->ssid.c_str());
        log_i("GSM APN: %s", devInfo->apn.c_str());
        log_i("Device ID: %s", devInfo->deviceid.c_str());
        log_i("Server: %s", sysData->server.c_str());
        log_i("Server OK status: %d", sysStatus->server_ok);
        log_i("Use modem: %s", sysStatus->use_modem ? "yes" : "no");

        // If server_ok is not set, check if we should fall back to API_SERVER
        if (!sysStatus->server_ok || sysData->server.length() == 0)
        {
            log_i("Server not configured from SD card, checking for compile-time fallback...");
#ifdef API_SERVER
            log_i("Using compile-time API_SERVER: %s", API_SERVER);
            sysData->server = API_SERVER;
            sysStatus->server_ok = true;
            log_i("Server OK status updated to: %d", sysStatus->server_ok);
#else
            log_e("No server configured and no compile-time API_SERVER defined!");
            sysStatus->server_ok = false;
#endif
        }

        // Update state
        if (xSemaphoreTake(networkStateMutex, pdMS_TO_TICKS(1000)) == pdTRUE)
        {
            networkState.configurationLoaded = true;
            xSemaphoreGive(networkStateMutex);
        }

        return true;
    }
    else
    {
        log_e("Failed to load network configuration");
        updateDisplayStatus(devInfo, sysStatus, DISP_EVENT_SD_CARD_CONFIG_ERROR);
        return false;
    }
}

// Additional utility functions
bool isNetworkTaskRunning()
{
    bool running = false;

    if (xSemaphoreTake(networkStateMutex, pdMS_TO_TICKS(1000)) == pdTRUE)
    {
        running = networkState.taskRunning;
        xSemaphoreGive(networkStateMutex);
    }

    return running;
}

bool isInternetConnected()
{
    bool connected = false;

    if (xSemaphoreTake(networkStateMutex, pdMS_TO_TICKS(1000)) == pdTRUE)
    {
        connected = networkState.internetConnected;
        xSemaphoreGive(networkStateMutex);
    }

    return connected;
}

void requestNetworkDisconnection()
{
    log_i("Requesting network disconnection");
    if (networkEventGroup)
    {
        xEventGroupSetBits(networkEventGroup, NET_EVT_DISCONNECT_REQ);
    }
}

void requestNetworkConnection()
{
    log_i("Requesting network connection");
    if (networkEventGroup)
    {
        xEventGroupSetBits(networkEventGroup, NET_EVT_CONNECT_REQ);
    }
}

void requestTimeSync()
{
    log_i("Requesting time synchronization");
    if (networkEventGroup)
    {
        xEventGroupSetBits(networkEventGroup, NET_EVT_TIME_SYNC_REQ);
    }
}

void updateNetworkConfig()
{
    log_i("Requesting network configuration update");
    if (networkEventGroup)
    {
        xEventGroupSetBits(networkEventGroup, NET_EVT_CONFIG_UPDATED);
    }
}

void setFirmwareOperationInProgress(bool inProgress)
{
    if (xSemaphoreTake(networkStateMutex, pdMS_TO_TICKS(1000)) == pdTRUE)
    {
        networkState.firmwareDownloadInProgress = inProgress;
        log_i("Firmware operation in progress flag set to: %s", inProgress ? "true" : "false");
        xSemaphoreGive(networkStateMutex);
    }
    else
    {
        log_e("Failed to acquire mutex for setting firmware operation flag");
    }
}

bool getNetworkStatus(bool *wifiConnected, bool *gsmConnected, bool *timeSync)
{
    if (!wifiConnected || !gsmConnected || !timeSync)
    {
        return false;
    }

    if (xSemaphoreTake(networkStateMutex, pdMS_TO_TICKS(1000)) == pdTRUE)
    {
        *wifiConnected = networkState.wifiConnected;
        *gsmConnected = networkState.gsmConnected;
        *timeSync = networkState.timeSync;
        xSemaphoreGive(networkStateMutex);
        return true;
    }

    return false;
}

// Network configuration initialization functions (moved from main file)
void vMspInit_setDefaultSslName(systemData_t *p_tData)
{
    // Default server name for SSL data upload
    p_tData->server = __API_SERVER;
    p_tData->server_ok = true;
    log_i("Network SSL server set to: %s", p_tData->server.c_str());
}

void vMspInit_setApiSecSaltAndFwVer(systemData_t *p_tData)
{
    // API security salt
    p_tData->api_secret_salt = __API_SECRET_SALT;
    log_i("API secret salt configured");

    // Current firmware version
    p_tData->ver = VERSION_STRING;
    log_i("Firmware version set to: %s", p_tData->ver.c_str());
}

void setFirmwareDownloadInProgress(void)
{
    if (xSemaphoreTake(networkStateMutex, pdMS_TO_TICKS(1000)) == pdTRUE)
    {
        networkState.firmwareDownloadInProgress = true;
        log_i("Firmware download started - network connectivity tests disabled");
        xSemaphoreGive(networkStateMutex);
    }
    else
    {
        log_e("Failed to acquire network state mutex for firmware download flag");
    }
}

void clearFirmwareDownloadInProgress(void)
{
    if (xSemaphoreTake(networkStateMutex, pdMS_TO_TICKS(1000)) == pdTRUE)
    {
        networkState.firmwareDownloadInProgress = false;
        log_i("Firmware download completed - network connectivity tests re-enabled");
        xSemaphoreGive(networkStateMutex);
    }
    else
    {
        log_e("Failed to acquire network state mutex for firmware download flag");
    }
}