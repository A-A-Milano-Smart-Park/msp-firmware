#!/bin/sh
#
# run flashing script (needs esptool installed from pip)
python3 -m esptool --chip esp32 --baud 921600 --before default_reset --after hard_reset erase_flash
python3 -m esptool --chip esp32 --baud 921600 --before default_reset --after hard_reset write_flash -z --flash_mode dio --flash_freq 80m --flash_size 4MB 0x1000 msp-firmware.ino.bootloader.bin 0x8000 msp-firmware.ino.partitions.bin 0xe000 boot_app0.bin 0x10000 msp-firmware.ino.bin
python3 -m esptool --chip esp32 --baud 921600 --before default_reset --after hard_reset verify_flash --flash_size 4MB 0x1000 msp-firmware.ino.bootloader.bin 0x8000 msp-firmware.ino.partitions.bin 0xe000 boot_app0.bin 0x10000 msp-firmware.ino.bin
