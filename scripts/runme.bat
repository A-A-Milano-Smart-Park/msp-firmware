@echo off
setlocal EnableDelayedExpansion

REM ============================================================================
REM MSP Firmware Flash Script for Windows 10/11 (Interactive ESP32 Flasher)
REM ============================================================================
REM Description: Flash Milano Smart Park firmware to ESP32 with user-selected flash size
REM Requirements: esptool.exe (included in release package)
REM Compatibility: Windows 10, Windows 11
REM ============================================================================

REM Enable color support for Windows 10+
for /f "tokens=4-5 delims=. " %%i in ('ver') do set VERSION=%%i.%%j
if "%version%" geq "10.0" (
    REM Enable ANSI escape sequences for colors
    reg add HKEY_CURRENT_USER\Console /v VirtualTerminalLevel /t REG_DWORD /d 0x00000001 /f >nul 2>&1
)

REM Color codes (ANSI escape sequences)
set "RED=[91m"
set "GREEN=[92m"
set "YELLOW=[93m"
set "BLUE=[94m"
set "PURPLE=[95m"
set "CYAN=[96m"
set "WHITE=[97m"
set "GRAY=[37m"
set "RESET=[0m"
set "BOLD=[1m"

REM Configuration variables
set "CHIP=esp32"
set "BAUD=921600"
set "FLASH_MODE=dio"
set "FLASH_FREQ=80m"
set "FLASH_SIZE="
set "FLASH_CONFIG="

REM Clear screen for clean start
cls

REM Main header
echo.
echo %CYAN%================================================================================%RESET%
echo %WHITE%%BOLD%                    MSP FIRMWARE FLASH TOOL FOR WINDOWS                    %RESET%
echo %CYAN%================================================================================%RESET%
echo.

REM Flash size selection
echo %WHITE%Select your ESP32 flash size:%RESET%
echo.
echo   %BLUE%1)%RESET% %WHITE%4MB Flash%RESET% %GRAY%- Dual OTA partitions (1.75MB each), No SPIFFS%RESET%
echo   %BLUE%2)%RESET% %WHITE%8MB Flash%RESET% %GRAY%- Dual OTA partitions (2.7MB each), 1MB SPIFFS%RESET%
echo.

:SELECT_FLASH_SIZE
set /p "choice=%WHITE%Enter your choice (1 or 2): %RESET%"

if "%choice%"=="1" (
    set "FLASH_SIZE=4MB"
    set "FLASH_CONFIG=4MB Flash, Dual OTA partitions (1.75MB each), No SPIFFS"
    goto FLASH_SELECTED
) else if "%choice%"=="2" (
    set "FLASH_SIZE=8MB"
    set "FLASH_CONFIG=8MB Flash, Dual OTA partitions (2.7MB each), 1MB SPIFFS"
    goto FLASH_SELECTED
) else (
    echo %RED%✗ ERROR: Invalid choice. Please enter 1 for 4MB or 2 for 8MB.%RESET%
    echo.
    goto SELECT_FLASH_SIZE
)

:FLASH_SELECTED
echo.
echo %GREEN%✓ Selected: %FLASH_SIZE% flash configuration%RESET%
echo.

REM Display configuration
echo %GRAY%Flash Configuration:%RESET%
echo   • Chip: %PURPLE%ESP32%RESET%
echo   • Flash Size: %PURPLE%!FLASH_SIZE!%RESET%
echo   • Configuration: %PURPLE%!FLASH_CONFIG!%RESET%
echo   • Baud Rate: %PURPLE%921,600 bps%RESET%
echo   • Mode: %PURPLE%DIO%RESET%
echo   • Frequency: %PURPLE%80MHz%RESET%
echo.

REM Check for esptool
echo %YELLOW%ℹ Checking for esptool.exe...%RESET%
if not exist "esptool.exe" (
    echo %RED%✗ ERROR: esptool.exe not found in current directory.%RESET%
    echo %YELLOW%Please ensure esptool.exe is in the same folder as this script.%RESET%
    pause
    exit /b 1
)
echo %GREEN%✓ esptool.exe found and ready%RESET%
echo.

REM Check for required files
echo %YELLOW%ℹ Verifying firmware files...%RESET%
set "missing_files=0"

for %%f in (msp-firmware.ino.bootloader.bin msp-firmware.ino.partitions.bin boot_app0.bin msp-firmware.ino.bin) do (
    if not exist "%%f" (
        echo %RED%✗ ERROR: Required file missing: %%f%RESET%
        set /a missing_files+=1
    ) else (
        for %%s in ("%%f") do (
            set size=%%~zs
            set /a size_kb=!size!/1024
            echo   %GREEN%✓%RESET% %%f %GRAY%(!size_kb!KB)%RESET%
        )
    )
)

if !missing_files! gtr 0 (
    echo.
    echo %RED%✗ ERROR: Cannot proceed with missing files.%RESET%
    echo %YELLOW%Please ensure all firmware files are in the current directory.%RESET%
    pause
    exit /b 1
)

echo %GREEN%✓ All firmware files verified%RESET%
echo.

REM Warning message
echo %YELLOW%⚠ WARNING: This will completely erase your ESP32 and flash new firmware!%RESET%
echo %GRAY%Make sure your ESP32 is connected and in download mode if required.%RESET%
echo.
pause

REM Step 1: Erase flash
echo.
echo %BLUE%[STEP 1/4]%RESET% %WHITE%Erasing ESP32 flash memory...%RESET%
echo %GRAY%This will remove all existing firmware and data...%RESET%
echo.

esptool.exe --chip %CHIP% --baud %BAUD% --before default_reset --after hard_reset erase_flash

if errorlevel 1 (
    echo.
    echo %RED%✗ ERROR: Failed to erase flash memory%RESET%
    pause
    exit /b 1
)

echo.
echo %GREEN%✓ Flash memory erased successfully%RESET%

REM Step 2: Write firmware
echo.
echo %BLUE%[STEP 2/4]%RESET% %WHITE%Writing firmware to ESP32...%RESET%
echo %GRAY%Writing bootloader, partitions, boot_app0, and application...%RESET%
echo.

esptool.exe --chip %CHIP% --baud %BAUD% --before default_reset --after hard_reset write_flash -z --flash_mode %FLASH_MODE% --flash_freq %FLASH_FREQ% --flash_size !FLASH_SIZE! 0x1000 msp-firmware.ino.bootloader.bin 0x8000 msp-firmware.ino.partitions.bin 0xe000 boot_app0.bin 0x10000 msp-firmware.ino.bin

if errorlevel 1 (
    echo.
    echo %RED%✗ ERROR: Failed to write firmware%RESET%
    pause
    exit /b 1
)

echo.
echo %GREEN%✓ Firmware written successfully%RESET%

REM Step 3: Completion
echo.
echo %BLUE%[STEP 3/3]%RESET% %WHITE%Flashing process completed!%RESET%

REM Final success message
echo.
echo %CYAN%================================================================================%RESET%
echo %WHITE%%BOLD%                      ✓ FIRMWARE FLASHING SUCCESSFUL!                      %RESET%
echo %CYAN%================================================================================%RESET%
echo.

echo %GREEN%Your ESP32 has been successfully flashed with MSP firmware!%RESET%
echo.

echo %WHITE%Device Information:%RESET%
echo   • Flash Size: %GREEN%!FLASH_SIZE!%RESET%
if "!FLASH_SIZE!"=="4MB" (
    echo   • Partition Layout: %GREEN%Dual OTA (1.75MB each)%RESET%
    echo   • SPIFFS: %YELLOW%Disabled%RESET% (maximized app space^)
) else (
    echo   • Partition Layout: %GREEN%Dual OTA (2.7MB each)%RESET%
    echo   • SPIFFS: %GREEN%1MB%RESET% (file system enabled^)
)
echo   • OTA Updates: %GREEN%Enabled%RESET% with rollback protection

echo.
echo %WHITE%Next Steps:%RESET%
echo   1. %CYAN%Disconnect and reconnect%RESET% your ESP32
echo   2. %CYAN%Monitor serial output%RESET% to verify boot process
echo   3. %CYAN%Check device logs%RESET% for successful initialization

echo.
echo %GRAY%Serial monitoring tools:%RESET%
echo   • %WHITE%PuTTY%RESET% - Set to Serial, 115200 baud
echo   • %WHITE%Arduino IDE Serial Monitor%RESET% - 115200 baud
echo   • %WHITE%Windows Device Manager%RESET% - Check COM port

REM Keep window open to show results
pause
