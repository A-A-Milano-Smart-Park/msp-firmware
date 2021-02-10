# Milano Smart Park Firmware for Arduino IDE

A&A Milano Smart Park Project

Firmware developed with the Arduino IDE by Norman Mulinacci @ 2021

The project runs on Espressif's ESP32-DevkitC with ESP32-WROVER-B module

## Building and flashing from source (using the Arduino IDE):

### Required Core (you can also download it through the Arduino IDE):

- [Arduino core for the ESP32](https://github.com/espressif/arduino-esp32)
    + To download the core through the Arduino IDE, you need to add the following URLs in File -> Settings -> Additional URLs:
    https://dl.espressif.com/dl/package_esp32_index.json, http://arduino.esp8266.com/stable/package_esp8266com_index.json

### Required external libraries (you can also download them through the Arduino IDE):

- [U8g2 Arduino library](https://github.com/olikraus/U8g2_Arduino)
- [SSLClient Library](https://github.com/OPEnSLab-OSU/SSLClient)
- [BSEC Arduino library](https://github.com/BoschSensortec/BSEC-Arduino-library)
	+ In order for the linker to work properly, you need to perform the modifications described in the BSEC page. They work fine in Arduino IDE v1.8.13.
- [PMS Library](https://github.com/fu-hsi/pms)
- [MiCS6814-I2C-Library](https://github.com/eNBeWe/MiCS6814-I2C-Library)

### Build settings (under the Tools tab):

- Board: "ESP32 Dev Module" (under ESP32 Arduino)
- Upload Speed: "921600"
- CPU Frequency: "240MHz (WiFi/BT)"
- Flash Frequency: "80MHz"
- Flash Mode: "QIO"
- Flash Size: "4MB (32Mb)"
- Partition Scheme: "Default 4MB with spiffs (1.2MB APP/1.5MB SPIFFS)"
- Core Debug Level: "None"
- PSRAM: "Enabled"

## Building from source (using `Makefile`):

On supported build platforms (i.e. Linux, MacOs), you can use `make` to build
the firmware.  The provided `Makefile` includes targets for installing dependencies
as well.

To download all required Arduino build tools and dependencies (core and
libraries) clone this repo and run the following command from within the cloned
directory:

```
make env
```

Then, to build the firmware, simply run:

```
make
```

The firmware binary and all build artifacts will be located under the `var/build`
directory. Run `make help` for details on all targets supported by the `Makefile`.

To run the build to completion, a working Python interpreter must be installed
on your build system, along with the
[`pyserial`](https://pypi.org/project/pyserial/) library.

## Flashing from binary releases (Windows instructions):

1. Connect the ESP32 board to a USB port on your PC. Check that it's been detected correctly:
   it should appear as "Silicon Labs CP210x USB to UART Bridge (COMx)".
   If not, download the drivers manually: 
	+ for Windows 10: https://www.silabs.com/documents/public/software/CP210x_Universal_Windows_Driver.zip
	+ for older Windows versions: https://www.silabs.com/documents/public/software/CP210x_Windows_Drivers.zip
	
2. Extract the "esptool_win" folder from the zip file.

3. Run "RUNME.BAT". The script will automatically scan for the right COM port and then erase, flash and verify the board.
   If it stays on "Connecting..." for too long, try again on a different USB port.

4. If it verifies OK, you are done!
