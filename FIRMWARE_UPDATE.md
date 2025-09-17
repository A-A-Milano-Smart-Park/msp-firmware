# Automatic Firmware Update System (FOTA)

## Overview

This firmware implements a comprehensive two-phase Over-The-Air (OTA) update system that automatically checks for new firmware releases on GitHub daily at 00:00:00 and performs secure firmware updates when enabled.

The system uses a **two-phase approach**:
- **Phase 1**: Download firmware to SD card and reboot with clean memory
- **Phase 2**: Apply firmware update using ESP-IDF native OTA on fresh boot

## Configuration

### SD Card Configuration

Add the following line to your `config_v3.txt` file on the SD card:

```
#fwAutoUpgrade=true;
```

Set to `true` to enable automatic firmware updates, or `false` to disable them.

### Default Behavior

- **Default**: `false` (disabled)
- **When enabled**: Daily check at 00:00:00
- **GitHub Repository**: https://github.com/A-A-Milano-Smart-Park/msp-firmware/releases
- **Asset Format**: Looks for `update_vX.X.X.bin` files in releases

## How It Works

### Phase 1: Download (Daily Check)
1. **Daily Timer**: Every day at 00:00:00, if `fwAutoUpgrade=true`, the system checks for updates
2. **GitHub API**: Queries the latest release using GitHub API
3. **Version Check**: Compares current firmware version with latest release using semantic versioning
4. **Asset Selection**: Looks for direct binary files named `update_vX.X.X.bin`
5. **Download**: Downloads the firmware binary to SD card as `/firmware.bin` if newer version available
6. **Reboot**: System automatically reboots to apply update with clean memory

### Phase 2: Apply Update (After Reboot)
1. **SD Card Check**: On boot, checks for `/firmware.bin` on SD card
2. **Version Extraction**: Extracts version from firmware binary using ESP-IDF app descriptor
3. **Version Validation**: Compares versions to ensure update is needed
4. **Security Validation**: Performs firmware integrity checks
5. **ESP-IDF OTA**: Uses native ESP-IDF OTA functions for safe partition management
6. **Final Reboot**: Reboots into new firmware and cleans up SD card file

## Version Comparison

The system uses semantic versioning (SemVer) format:
- Format: `v1.2.3` or `1.2.3`
- Comparison: Major.Minor.Patch
- Special case: `DEV` version is always considered older than any release

## Security Features

### Basic Security (Always Enabled)
- **ESP32 Magic Number Verification**: Validates firmware binary format
- **Size Validation**: Ensures firmware size is within reasonable bounds
- **Download Integrity**: Requires exact file size match for downloads
- **Partition Validation**: Verifies firmware fits in available OTA partition
- **ESP-IDF Native OTA**: Uses hardware-validated OTA partition management

### Enhanced Security (Optional)
When `ENABLE_ENHANCED_SECURITY` is defined in `config.h`:

- **Firmware Signature Verification**: Hardware-level signature validation
- **SHA256 Hash Verification**: Cryptographic integrity checks
- **Detached Signature Support**: Additional RSA signature verification
- **Secure Boot Integration**: Leverages ESP32 secure boot features
- **Flash Encryption Support**: Works with ESP32 flash encryption

## Testing

### Compile-Time Testing

Add `-DENABLE_FIRMWARE_UPDATE_TESTS` to your build flags to enable comprehensive testing:

### Available Test Functions

```cpp
// Test version comparison logic
vHalFirmware_testVersionComparison();

// Test GitHub API connectivity (requires internet)
vHalFirmware_testGitHubAPI();

// Test configuration parsing
vHalFirmware_testConfigParsing(&sysStatus);

// Test ESP-IDF OTA management
vHalFirmware_testOTAManagement();

// Force firmware update (bypasses version checks)
vHalFirmware_testForceOTAUpdate(&sysData, &sysStatus, &devInfo);
```

### SD Card Firmware Functions

```cpp
// Force flash from SD card (no version check)
bHalFirmware_forceFlashFromSDCard("/firmware.bin");

// Validate and flash if newer version
bHalFirmware_validateAndFlashFromSD("/firmware.bin", false);

// Extract version from firmware binary
String version = bHalFirmware_extractVersionFromFile("/firmware.bin");
```

## Safety Features

### Update Safety
1. **Two-Phase Design**: Separates download from update for memory safety
2. **Clean Memory**: Updates happen on fresh boot with maximum available memory
3. **Network Requirement**: Updates only occur when internet connectivity is available
4. **Version Validation**: Only installs newer versions based on semantic versioning
5. **File Validation**: Verifies firmware file exists and has correct format before update
6. **Exact Size Matching**: Requires exact file size match during download for integrity

### ESP-IDF Integration
7. **Native OTA**: Uses ESP-IDF's proven OTA partition management
8. **Rollback Protection**: ESP32 hardware rollback mechanisms prevent boot loops
9. **Partition Validation**: Ensures firmware fits in available OTA partition
10. **Boot Validation**: New firmware marked as valid only after successful boot

### Error Handling
11. **Controlled Reboots**: On critical errors, performs controlled restart instead of hang
12. **File Cleanup**: Removes corrupted or failed firmware files automatically
13. **Memory Management**: Uses SPIRAM when available for large operations
14. **Timeout Protection**: Download timeouts prevent infinite hanging

## Troubleshooting

### Common Issues

1. **No Update Check Occurring**
   - Verify `fwAutoUpgrade=true` in config_v3.txt
   - Check that device has internet connectivity at 00:00:00
   - Review logs for configuration parsing errors

2. **GitHub API Errors**
   - Check internet connectivity
   - Verify GitHub repository URL is accessible
   - Check for API rate limiting (rare for this usage pattern)

3. **Download Failures**
   - Ensure sufficient SD card space
   - Verify network stability during download
   - Check download timeout settings (currently 60 seconds)

4. **OTA Update Failures**
   - Verify firmware file integrity
   - Check available flash memory
   - Review OTA partition configuration

### Log Messages

Monitor serial output for these key messages:

```
[INFO] Daily firmware update check triggered
[INFO] Current version: v1.0.0
[INFO] Latest version: v1.0.1
[INFO] New firmware version available, starting download...
[INFO] OTA update completed successfully, restarting...
```

## Development Notes

### File Structure

- `firmware_update.h` - Header file with function declarations
- `firmware_update.cpp` - Main implementation
- Integration in `msp-firmware.ino` - Daily timer logic

### Dependencies

- ArduinoJson library for GitHub API response parsing
- HTTPClient for downloading files
- Update library for OTA functionality
- SD library for file operations

### GitHub Release Requirements

For the automatic update to work, GitHub releases must:
1. Have semantic version tags (e.g., `v1.0.0`)
2. Include direct binary firmware files as release assets
3. Binary files must be named `update_vX.X.X.bin` (matching the release tag)

### Asset Selection Logic

The system looks for specific firmware assets in this order:
1. **Primary**: `update_vX.X.X.bin` (where X.X.X matches the release tag)
2. **Fallback**: If direct binary not found, logs error and skips update

Example release assets:
- ✅ `update_v1.0.0.bin` (ESP32 firmware binary - REQUIRED)
- ✅ `update_v1.0.0.bin.sig` (optional detached signature for enhanced security)
- ❌ `msp-firmware-v1.0.0-macos.zip` (no longer used - ZIP extraction removed)
- ❌ `Source code (zip)` (ignored)
- ❌ `Source code (tar.gz)` (ignored)

### Binary Firmware Format

The firmware binary must be:
- Valid ESP32 application binary (generated by ESP-IDF build system)
- Contains proper ESP32 header with magic number (0xE9)
- Includes ESP-IDF app descriptor with version information
- Size appropriate for target ESP32 flash partition (typically 1-2MB)

### Security Considerations

- GitHub API requests use HTTPS
- No authentication tokens stored (uses public API)
- OTA updates follow standard ESP32 security practices
- Firmware validation occurs before installation

## Implementation Architecture

### Key Functions

#### Core FOTA Functions
- `bHalFirmware_checkForUpdates()` - Daily GitHub check and download
- `bHalFirmware_downloadToSDCard()` - Download firmware to SD card
- `bHalFirmware_validateAndFlashFromSD()` - Apply update from SD card
- `bHalFirmware_performOTAUpdate()` - ESP-IDF native OTA execution
- `bHalFirmware_compareVersions()` - Semantic version comparison

#### Version Management
- `bHalFirmware_extractVersionFromFile()` - Extract version from ESP32 binary
- `bHalFirmware_validateCurrentFirmware()` - Mark current firmware as valid
- `bHalFirmware_markFirmwareValid()` - Prevent rollback of working firmware

#### Security Functions (Enhanced Mode)
- `bHalFirmware_verifyFirmwareSignature()` - Cryptographic signature validation
- `bHalFirmware_verifyFirmwareHash()` - SHA256 integrity verification
- `bHalFirmware_verifyDetachedSignature()` - External signature file validation

### Integration Points

The FOTA system integrates with:
- **Main Loop**: Daily update checks at midnight
- **Boot Sequence**: SD card firmware check on startup
- **Network Task**: Configuration parsing for `fwAutoUpgrade` flag
- **Display Task**: Update progress indication
- **SD Card**: Firmware storage and cleanup

## Future Enhancements

Potential improvements for production use:

1. **Enhanced Progress Display**: Real-time update progress on OLED display
2. **Incremental Updates**: Support delta/incremental updates to reduce download size
3. **Update Scheduling**: Allow custom update schedules beyond daily at midnight
4. **Automatic Health Checks**: Self-validation after firmware updates
5. **Multiple Release Channels**: Support for stable/beta/alpha release channels
6. **Remote Configuration**: Allow update settings to be changed remotely
7. **Update History**: Log of successful and failed update attempts

## Configuration Example

Complete `config_v3.txt` example with firmware update enabled:

```
#ssid=YourWiFiSSID;
#password=YourWiFiPassword;
#device_id=device001;
#wifi_power=17dBm;
#o3_zero_value=0;
#average_measurements=5;
#average_delay(seconds)=300;
#sea_level_altitude=122.0;
#upload_server=milanosmartpark.info;
#mics_calibration_values=RED:100,OX:100,NH3:100;
#mics_measurements_offsets=RED:0,OX:0,NH3:0;
#compensation_factors=compH:0.0,compT:0.000,compP:0.0000;
#use_modem=false;
#modem_apn=;
#ntp_server=pool.ntp.org;
#timezone=CET-1CEST;
#fwAutoUpgrade=true;
```