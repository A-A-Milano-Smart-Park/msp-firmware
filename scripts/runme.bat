@echo off
setlocal EnableDelayedExpansion

REM ============================================================================
REM MSP Firmware Flash Script for Windows 10/11
REM ============================================================================
REM Description: Flash Milano Smart Park firmware to ESP32 with user-selected flash size
REM Requirements: esptool.exe (included in release package)
REM Compatibility: Windows 10, Windows 11
REM ============================================================================

REM Configuration variables
set "CHIP=esp32"
set "BAUD=921600"
set "FLASH_MODE=dio"
set "FLASH_FREQ=80m"
set "FLASH_SIZE="
set "FLASH_CONFIG="

cls
echo.
echo ================================================
echo        MSP FIRMWARE FLASH TOOL FOR WINDOWS
echo ================================================
echo.

REM Flash size selection
echo Select your ESP32 flash size:
echo.
echo   1) 4MB Flash - Dual OTA partitions (1.75MB each), No SPIFFS
echo   2) 8MB Flash - Dual OTA partitions (2.7MB each), 1MB SPIFFS
echo.

:SELECT_FLASH_SIZE
set /p "choice=Enter your choice (1 or 2): "

if "%choice%"=="1" (
    set "FLASH_SIZE=4MB"
    set "FLASH_CONFIG=4MB Flash, Dual OTA partitions (1.75MB each), No SPIFFS"
    goto FLASH_SELECTED
) else if "%choice%"=="2" (
    set "FLASH_SIZE=8MB"
    set "FLASH_CONFIG=8MB Flash, Dual OTA partitions (2.7MB each), 1MB SPIFFS"
    goto FLASH_SELECTED
) else (
    echo ERROR: Invalid choice. Please enter 1 or 2.
    echo.
    goto SELECT_FLASH_SIZE
)

:FLASH_SELECTED
echo.
echo Selected: %FLASH_SIZE% flash configuration
echo.

REM Display configuration
echo Flash Configuration:
echo   • Chip: ESP32
echo   • Flash Size: !FLASH_SIZE!
echo   • Configuration: !FLASH_CONFIG!
echo   • Baud Rate: 921600 bps
echo   • Mode: DIO
echo   • Frequency: 80MHz
echo.

REM Check for esptool
echo Checking for esptool.exe...
if not exist "esptool.exe" (
    echo ERROR: esptool.exe not found in current directory.
    echo Please ensure esptool.exe is in the same folder as this script.
    pause
    exit /b 1
)
echo esptool.exe found and ready
echo.

REM Check for required files
echo Verifying firmware files...
set "missing_files=0"

for %%f in (msp-firmware.ino.bootloader.bin msp-firmware.ino.partitions.bin boot_app0.bin msp-firmware.ino.bin) do (
    if not exist "%%f" (
        echo ERROR: Required file missing: %%f
        set /a missing_files+=1
    ) else (
        for %%s in ("%%f") do (
            set size=%%~zs
            set /a size_kb=!size!/1024
            echo   ✓ %%f (!size_kb!KB)
        )
    )
)

if !missing_files! gtr 0 (
    echo.
    echo ERROR: Cannot proceed with missing files.
    echo Please ensure all firmware files are in the current directory.
    pause
    exit /b 1
)

echo All firmware files verified
echo.

REM Warning message
echo WARNING: This will completely erase your ESP32 and flash new firmware!
echo Make sure your ESP32 is connected and in download mode if required.
echo.
pause

REM Step 1: Erase flash
echo.
echo [STEP 1/4] Erasing ESP32 flash memory...
echo This will remove all existing firmware and data...
echo.

esptool.exe --chip %CHIP% --baud %BAUD% --before default_reset --after hard_reset erase_flash

if errorlevel 1 (
    echo.
    echo ERROR: Failed to erase flash memory
    pause
    exit /b 1
)

echo.
echo Flash memory erased successfully

REM Step 2: Write firmware
echo.
echo [STEP 2/4] Writing firmware to ESP32...
echo Writing bootloader, partitions, boot_app0, and application...
echo.

esptool.exe --chip %CHIP% --baud %BAUD% --before default_reset --after hard_reset write_flash -z --flash_mode %FLASH_MODE% --flash_freq %FLASH_FREQ% --flash_size !FLASH_SIZE! 0x1000 msp-firmware.ino.bootloader.bin 0x8000 msp-firmware.ino.partitions.bin 0xe000 boot_app0.bin 0x10000 msp-firmware.ino.bin

if errorlevel 1 (
    echo.
    echo ERROR: Failed to write firmware
    pause
    exit /b 1
)

echo.
echo Firmware written successfully

REM Step 3: Completion
echo.
echo [STEP 3/3] Flashing process completed!
echo.

echo ================================================
echo        ✓ FIRMWARE FLASHING SUCCESSFUL!
echo ================================================
echo.

echo Your ESP32 has been successfully flashed with MSP firmware!
echo.

echo Device Information:
echo   • Flash Size: !FLASH_SIZE!
if "!FLASH_SIZE!"=="4MB" (
    echo   • Partition Layout: Dual OTA (1.75MB each)
    echo   • SPIFFS: Disabled (maximized app space)
) else (
    echo   • Partition Layout: Dual OTA (2.7MB each)
    echo   • SPIFFS: 1MB (file system enabled)
)
echo   • OTA Updates: Enabled with rollback protection

echo.
echo Next Steps:
echo   1. Disconnect and reconnect your ESP32
echo   2. Monitor serial output to verify boot process
echo   3. Check device logs for successful initialization

echo.
echo Serial monitoring tools:
echo   • PuTTY - Set to Serial, 115200 baud
echo   • Arduino IDE Serial Monitor - 115200 baud
echo   • Windows Device Manager - Check COM port

pause