# Milano Smart Park Firmware for Arduino IDE

A&A Milano Smart Park Project

Firmware developed with the Arduino IDE v2 by Norman Mulinacci @ 2023

The project runs on Espressif's ESP32-DevkitC with ESP32-WROVER-B module

## Building and flashing from source (using the Arduino IDE):

### Required Core (you can also download it through the Arduino IDE):

- [Arduino core for the ESP32](https://github.com/espressif/arduino-esp32) version 2.0.17
    + To download the core through the Arduino IDE, you need to add the following URLs in File -> Settings -> Additional URLs:
    https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json

### Required external libraries (you can also download them through the Arduino IDE):

Libraries listed below can be installed through the Arduino IDE Library Manager:
- [U8g2 Arduino library](https://github.com/olikraus/U8g2_Arduino) version 2.34.22
- [SSLClient Library](https://github.com/OPEnSLab-OSU/SSLClient) version 1.6.11
- [BSEC Arduino library](https://github.com/BoschSensortec/BSEC-Arduino-library) version 1.8.1492
- [PMS Library](https://github.com/fu-hsi/pms) version 1.1.0
- [TinyGSM Library](https://github.com/vshymanskyy/TinyGSM) version 0.11.7
- [ArduinoJson] (https://arduinojson.org/?utm_source=meta&utm_medium=library.properties) version 7.4.2

You will also need to install a modified version of [MiCS6814-I2C-MOD-Library](https://github.com/eNBeWe/MiCS6814-I2C-Library/network) which is not available through the Arduino Library Manager and [must be imported manually](https://www.arduino.cc/en/Guide/Libraries#importing-a-zip-library):
- [MiCS6814-I2C-MOD-Library](https://github.com/A-A-Milano-Smart-Park/MiCS6814-I2C-MOD-Library)

If you already have the official or any another version of the MiCS6814-I2C-Library installed in your IDE environment, you'll need to first remove it. Libraries are installed to folders under `{sketchbook folder}/libraries`. You can find the location of your sketchbook folder in the Arduino IDE at **File > Preferences > Sketchbook location**. Just delete the appropriate library directory.

### Build settings (under the Tools tab):

- Board --> esp32 --> "ESP32 Dev Module"

P.S: When building, Core Debug Level can be set from "None" to "Verbose" to have a less or a more detailed serial output.

## Building from source (using `Makefile`):

On supported build platforms (i.e. Linux, MacOs), you can use `make` to build
the firmware.  The provided `Makefile` includes targets for installing all required
dependencies as well.

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

## Flashing from binary releases (Windows 64bit instructions):

1. Connect the ESP32 board to a USB port on your PC.

2. Check that it's been detected correctly: it should appear as `Silicon Labs CP210x USB to UART Bridge (COMx)` (check in Windows Device Management).
   If not, download the drivers and install them manually through Device Management:
	+ for Windows 10/11: https://www.silabs.com/documents/public/software/CP210x_Universal_Windows_Driver.zip
	+ for older Windows versions: https://www.silabs.com/documents/public/software/CP210x_Windows_Drivers.zip

3. Download the latest release, extract it and run `runme.bat`. The script will automatically scan for the right COM port and then erase, flash and verify the board.
   If it stays on "Connecting..." for too long, hold the "BOOT" button of the ESP32 board and try again.
   If it still doesn't work, try on a different USB port.

4. If it verifies OK, you are done!

## Flashing from binary releases (macOS instructions):

1. Connect the ESP32 board to a USB port on your Mac.

2. Download and install the drivers for macOS: https://www.silabs.com/documents/public/software/Mac_OSX_VCP_Driver.zip
When installing, you need to authorize them in macOS Security Settings.
Check that the device appearsin macOS System Information, otherwise disconnect and reconnect the USB cable. If it still doesn't find it, try a different USB port.

3. Download the latest release, extract it. In the Terminal, move to the msp-firmware folder using the `cd` command; then make the `.sh` files executable with these commands: `chmod +x install-pip-esptool.sh` and `chmod +x flash-msp-firmware.sh`.

4. Run the install script with this terminal command: `./install-pip-esptool.sh` (you might need to authorize its execution in macOS Security Settings). This will check if Python3 is installed (it might ask to install Developer Tools: if it does, install them), and it will install pip and the esptool program needed for the flashing process.

5. After the installation is done, run the install script with this terminal command: `./flash-msp-firmware.sh` (you might need to authorize its execution in macOS Security Settings). The script will erase, flash and verify the board.
   If it stays on "Connecting..." for too long, hold the "BOOT" button of the ESP32 board and try again.
   If it still doesn't work, try on a different USB port.

6. If it verifies OK, you are done!
