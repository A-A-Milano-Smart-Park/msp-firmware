/********************************************************
 * @file    config.h
 * @author  AB-Engineering - https://ab-engineering.it
 * @brief   Configuration constants for the Milano Smart Park project
 * @details Centralized configuration management for API credentials, pins, and hardware settings
 * @version 0.1
 * @date    2025-08-05
 * 
 * @copyright Copyright (c) 2025
 * 
 ********************************************************/

#ifndef CONFIG_H
#define CONFIG_H

// ===== String Constants =====
#define STR_TRUE  "true"
#define STR_FALSE "false"

// ===== Network Configuration =====

// #define ENABLE_FIRMWARE_UPDATE_TESTS      // enable for FOTA controlled testing

// ===== Debug Configuration =====

// Fake NTP Time Testing (format: "YYYY-MM-DD HH:MM:SS" or "HH:MM:SS" or "" to disable)
#define FAKE_NTP_TIME ""  // override NTP sync with custom date/time for testing
//
// Usage examples:
// #define FAKE_NTP_TIME "2025-01-15 23:59:30"  // full date-time (recommended)
// #define FAKE_NTP_TIME "00:05:00"             // time only (uses current date)
// #define FAKE_NTP_TIME ""                     // disable fake time (empty string)
//
// When enabled, this bypasses real NTP sync and data transmission, setting fake time.
// Useful for testing time-dependent behaviors like midnight firmware checks.

// #define ENABLE_I2C_SCANNER                 // enable to scan I2C devices at startup

// ===== Security Configuration =====

// Enhanced Security Features (disabled by default - enable for production)
// #define ENABLE_ENHANCED_SECURITY

// When ENABLE_ENHANCED_SECURITY is defined, provides:
// - Firmware signature verification using ESP32 secure boot integration
// - SHA256 hash validation for integrity checking  
// - Detached signature support for GitHub Actions releases
// - Enhanced security logging and status reporting
// - Compatible with ESP32 secure boot and flash encryption
// - Automatic removal of potentially malicious firmware files
//
// Note: Basic firmware validation (header, size, format) is always enabled
// Enable this flag for production deployments requiring cryptographic verification
#define SKIP_FIRMWARE_VERSION_CHECK 0
#define FORCE_GSM_NETWORK_CONNECTION 0

// API Server Configuration
#ifndef API_SERVER
#define __API_SERVER "server.info"
#else
#define __API_SERVER API_SERVER
#endif

// API Security Configuration
#ifndef API_SECRET_SALT
#define __API_SECRET_SALT "secret_salt"
#else
#define __API_SECRET_SALT API_SECRET_SALT
#endif

// ===== Hardware Pin Definitions =====

// Sensor Pin Definitions
#define PMSERIAL_RX 14
#define PMSERIAL_TX 12

// Network Hardware Pin Definitions
#define MODEM_RST 4
#define SSL_RAND_PIN 35

// Network Hardware Configuration
#define MODEM_TX 13
#define MODEM_RX 15

// Config Filename
#define CONFIG_FILENAME "config_v4.json"
#define CONFIG_PATH "/" CONFIG_FILENAME

// JSON Config Keys
#define JSON_CONFIG_SECTION "config"
#define JSON_HELP_SECTION "help"

// Main Config Keys
#define JSON_KEY_SSID "ssid"
#define JSON_KEY_PASSWORD "password"
#define JSON_KEY_DEVICE_ID "device_id"
#define JSON_KEY_WIFI_POWER "wifi_power"
#define JSON_KEY_O3_ZERO_VALUE "o3_zero_value"
#define JSON_KEY_AVERAGE_MEASUREMENTS "average_measurements"
#define JSON_KEY_AVERAGE_DELAY_SECONDS "average_delay_seconds"
#define JSON_KEY_SEA_LEVEL_ALTITUDE "sea_level_altitude"
#define JSON_KEY_UPLOAD_SERVER "upload_server"
#define JSON_KEY_MICS_CALIBRATION_VALUES "mics_calibration_values"
#define JSON_KEY_MICS_MEASUREMENTS_OFFSETS "mics_measurements_offsets"
#define JSON_KEY_COMPENSATION_FACTORS "compensation_factors"
#define JSON_KEY_USE_MODEM "use_modem"
#define JSON_KEY_MODEM_APN "modem_apn"
#define JSON_KEY_NTP_SERVER "ntp_server"
#define JSON_KEY_TIMEZONE "timezone"
#define JSON_KEY_FW_AUTO_UPGRADE "fw_auto_upgrade"
#define JSON_KEY_GAS_SENSOR_TYPE "gas_sensor_type"

// MICS Calibration Sub-keys
#define JSON_KEY_MICS_RED "RED"
#define JSON_KEY_MICS_OX "OX"
#define JSON_KEY_MICS_NH3 "NH3"

// Compensation Factor Sub-keys
#define JSON_KEY_COMP_H "compH"
#define JSON_KEY_COMP_T "compT"
#define JSON_KEY_COMP_P "compP"

// ===== Network Protocol Configuration =====

// NTP Configuration
#define NTP_SERVER_DEFAULT "pool.ntp.org"
#define TZ_DEFAULT "GMT0"

// Connection Timeouts
#define WIFI_CONNECTION_TIMEOUT_MS 15000
#define GPRS_CONNECTION_TIMEOUT_MS 30000
#define SERVER_RESPONSE_TIMEOUT_MS 10000

// Retry Configuration
#define MAX_CONNECTION_RETRIES 3
#define NETWORK_RETRY_DELAY_MS 5000

// Data Transmission Configuration
#define NTP_SYNC_TX_COUNT 100
#define SEND_DATA_TIMEOUT_IN_SEC (5 * 60)  // 5 minutes

// ===== Version Information =====

#ifndef VERSION_STRING
#define VERSION_STRING "4.1.4"
#endif

#endif // CONFIG_H