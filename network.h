/**************************************************************************
 * @file    network.h (Improved Version)
 * @author  Refactored by AB-Engineering - https://ab-engineering.it
 * @brief   Enhanced network management functions for the Milano Smart Park project
 * @details Thread-safe network management with proper state machine implementation
 * @version 0.2
 * @date    2025-08-05
 *
 * @copyright Copyright (c) 2025
 *
 ************************************************************************/

#ifndef NETWORK_H
#define NETWORK_H

// --includes
#include "shared_values.h"
#include "SSLClient.h"
#include <WiFi.h>
#include <stdbool.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/event_groups.h>
#include <freertos/semphr.h>

// Network internal event bits for task management (different from public events)
#define NET_EVT_CONNECT_REQ      (1 << 5)
#define NET_EVT_DISCONNECT_REQ   (1 << 6)
#define NET_EVT_TIME_SYNC_REQ    (1 << 7)
#define NET_EVT_DATA_READY       (1 << 8)
#define NET_EVT_CONFIG_UPDATED   (1 << 9)

// Network task states
typedef enum __NETWORK_TASK_EVT__
{
    NETWRK_EVT_WAIT = 0,                /*!< Wait for events or periodic maintenance */
    NETWRK_EVT_INIT_CONNECTION,         /*!< Initialize network connection (WiFi or GSM) */
    NETWRK_EVT_SYNC_DATETIME,          /*!< Synchronize date and time via NTP */
    NETWRK_EVT_UPDATE_DATA,            /*!< Process and send queued data to server */
    NETWRK_EVT_DEINIT_CONNECTION,      /*!< Deinitialize network connections */
    //--- LAST EVENT
    NETWRK_EVT_MAX_EVENTS              /*!< Maximum number of events (keep last) */
} netwkr_task_evt_t;

// Network connection types
typedef enum {
    NETWORK_TYPE_NONE = 0,
    NETWORK_TYPE_WIFI,
    NETWORK_TYPE_GSM,
    NETWORK_TYPE_BOTH
} network_type_t;

// Network status structure
typedef struct {
    bool wifiConnected;
    bool gsmConnected;
    bool internetConnected;
    bool timeSync;
    int connectionRetries;
    unsigned long lastConnectionAttempt;
    network_type_t activeConnection;
} network_status_t;

// ===== Core Network Management Functions =====

/**
 * @brief Initialize the network subsystem and create the network task
 * @details This function must be called before using any other network functions.
 *          It creates the data queue, event group, mutex, and network task.
 * @param sysData Pointer to initialized system data structure
 * @param sysStatus Pointer to system status structure
 * @param devInfo Pointer to device network info structure
 */
void initSendDataOp(systemData_t *sysData, systemStatus_t *sysStatus, deviceNetworkInfo_t *devInfo);

/**
 * @brief Create network event group for inter-task communication
 * @details Creates the event group used for signaling between tasks
 */
void createNetworkEvents(void);

// ===== Data Queue Management =====

/**
 * @brief Enqueue data for transmission to server
 * @param data Reference to the data structure to send
 * @param ticksToWait Maximum time to wait if queue is full
 * @return true if data was successfully enqueued, false otherwise
 */
bool enqueueSendData(const send_data_t &data, TickType_t ticksToWait);

/**
 * @brief Dequeue data from transmission queue (internal use)
 * @param data Pointer to store the dequeued data
 * @param ticksToWait Maximum time to wait if queue is empty
 * @return true if data was successfully dequeued, false otherwise
 */
bool dequeueSendData(send_data_t *data, TickType_t ticksToWait);

/**
 * @brief Check if server configuration is available from network task
 * @param config Pointer to store the configuration message
 * @param ticksToWait Maximum time to wait if queue is empty (0 for non-blocking)
 * @return true if config was available and retrieved, false otherwise
 */
bool dequeueServerConfig(server_config_msg_t *config, TickType_t ticksToWait);

// ===== Event Management =====

/**
 * @brief Send a network event to the network task
 * @param event Event bit to set
 * @return true if event was successfully sent, false otherwise
 */
bool sendNetworkEvent(net_evt_t event);

/**
 * @brief Check if a specific network event is currently set
 * @param event Event bit to check
 * @return true if event is set, false otherwise
 */
bool checkNetworkEvent(net_evt_t event);

/**
 * @brief Wait for a specific network event to occur
 * @param event Event bit to wait for
 * @param ticksToWait Maximum time to wait for the event
 * @return true if event occurred within timeout, false otherwise
 */
bool waitForNetworkEvent(net_evt_t event, TickType_t ticksToWait);

// ===== Network Connection Management =====

/**
 * @brief Request network connection establishment
 * @details Signals the network task to establish connection (WiFi or GSM)
 */
void requestNetworkConnection(void);

/**
 * @brief Request network disconnection
 * @details Signals the network task to disconnect from all networks
 */
void requestNetworkDisconnection(void);

/**
 * @brief Request time synchronization
 * @details Signals the network task to sync time via NTP
 */
void requestTimeSync(void);

/**
 * @brief Update network configuration from SD card
 * @details Signals the network task to reload configuration from SD card
 */
void updateNetworkConfig(void);

/**
 * @brief Get current network status
 * @param wifiConnected Pointer to store WiFi connection status
 * @param gsmConnected Pointer to store GSM connection status
 * @param timeSync Pointer to store time sync status
 * @return true if status was successfully retrieved, false otherwise
 */
bool getNetworkStatus(bool *wifiConnected, bool *gsmConnected, bool *timeSync);

/**
 * @brief Check if network is connected
 * @return true if network is connected, false otherwise
 */
bool isNetworkConnected();

/**
 * @brief Set firmware operation in progress flag to prevent network disconnection
 * @details Prevents automatic network disconnection during critical firmware operations
 */
void setFirmwareOperationInProgress(bool inProgress);

/**
 * @brief Check if network task is running
 * @return true if network task is active, false otherwise
 */
bool isNetworkTaskRunning(void);

/**
 * @brief Test internet connectivity by attempting DNS resolution
 * @details Tests connectivity to well-known DNS servers and attempts to resolve common domains
 * @return true if internet connectivity is available, false otherwise
 */
bool isInternetConnected(void);

/**
 * @brief Set firmware download in progress flag to skip network connectivity tests
 * @details When firmware download is active, network connectivity tests are skipped to avoid interference
 */
void setFirmwareDownloadInProgress(void);

/**
 * @brief Clear firmware download in progress flag to resume network connectivity tests
 * @details Called when firmware download completes or fails to resume normal network monitoring
 */
void clearFirmwareDownloadInProgress(void);

// ===== Network Configuration Functions =====

/**
 * @brief Set default SSL server name and mark server as OK
 * @param p_tData Pointer to system data structure
 */
void vMspInit_setDefaultSslName(systemData_t *p_tData);

/**
 * @brief Set API security salt and firmware version
 * @param p_tData Pointer to system data structure
 */
void vMspInit_setApiSecSaltAndFwVer(systemData_t *p_tData);

// ===== Hardware Access Functions =====

/**
 * @brief Disconnect from GSM modem
 * @return 1 if successful, 0 if failed
 */
uint8_t vHalNetwork_modemDisconnect(void);

/**
 * @brief Print WiFi MAC address and update display
 * @param p_tSys Pointer to system status structure
 * @param p_tDev Pointer to device network info structure
 */
void vHalNetwork_printWiFiMACAddr(systemStatus_t *p_tSys, deviceNetworkInfo_t *p_tDev);

// ===== Configuration Macros =====

// Network task configuration
#ifndef NETWORK_TASK_STACK_SIZE
#define NETWORK_TASK_STACK_SIZE (12 * 1024)  // Increased for SSL/TLS operations and OTA
#endif

#ifndef NETWORK_TASK_PRIORITY
#define NETWORK_TASK_PRIORITY 5
#endif


// Connection timeout and retry configuration - defined in config.h
// MAX_CONNECTION_RETRIES defined in config.h

// Debug configuration
#ifdef NETWORK_DEBUG
#define NETWORK_LOG_LEVEL ESP_LOG_DEBUG
#else
#define NETWORK_LOG_LEVEL ESP_LOG_INFO
#endif

// Version information
#define NETWORK_LIB_VERSION_MAJOR 0
#define NETWORK_LIB_VERSION_MINOR 2
#define NETWORK_LIB_VERSION_PATCH 0
#define NETWORK_LIB_VERSION "0.2.0"

#endif // NETWORK_H

//************************************** EOF **************************************