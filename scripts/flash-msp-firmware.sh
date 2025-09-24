#!/bin/bash

# ============================================================================
# MSP Firmware Flash Script for macOS/Linux (4MB ESP32 devices)
# ============================================================================
# Description: Flash Milano Smart Park firmware to ESP32 with 4MB flash
# Requirements: esptool installed via pip3 (run install-pip-esptool.sh first)
# Flash Config: 4MB Flash, Dual OTA partitions (1.75MB x2), No SPIFFS
# ============================================================================

# Color definitions for beautiful output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
PURPLE='\033[0;35m'
CYAN='\033[0;36m'
WHITE='\033[1;37m'
GRAY='\033[0;37m'
NC='\033[0m' # No Color

# Configuration
CHIP="esp32"
BAUD="921600"
FLASH_MODE="dio"
FLASH_FREQ="80m"

# Flash size will be selected by user
FLASH_SIZE=""
FLASH_CONFIG=""

# Function to print colored headers
print_header() {
    echo -e "\n${CYAN}════════════════════════════════════════════════════════════════${NC}"
    echo -e "${WHITE}$1${NC}"
    echo -e "${CYAN}════════════════════════════════════════════════════════════════${NC}\n"
}

# Function to print step information
print_step() {
    echo -e "${BLUE}[STEP $1/4]${NC} ${WHITE}$2${NC}"
}

# Function to print success message
print_success() {
    echo -e "\n${GREEN}✓ $1${NC}"
}

# Function to print error message
print_error() {
    echo -e "\n${RED}✗ ERROR: $1${NC}"
}

# Function to print info message
print_info() {
    echo -e "${YELLOW}ℹ $1${NC}"
}

# Main header
print_header "MSP FIRMWARE FLASH TOOL"

# Flash size selection
echo -e "${WHITE}Select your ESP32 flash size:${NC}\n"
echo -e "  ${BLUE}1)${NC} ${WHITE}4MB Flash${NC} ${GRAY}- Dual OTA partitions (1.75MB each), No SPIFFS${NC}"
echo -e "  ${BLUE}2)${NC} ${WHITE}8MB Flash${NC} ${GRAY}- Dual OTA partitions (2.7MB each), 1MB SPIFFS${NC}\n"

while true; do
    read -p "Enter your choice (1 or 2): " choice
    case $choice in
        1)
            FLASH_SIZE="4MB"
            FLASH_CONFIG="4MB Flash, Dual OTA partitions (1.75MB each), No SPIFFS"
            break
            ;;
        2)
            FLASH_SIZE="8MB"
            FLASH_CONFIG="8MB Flash, Dual OTA partitions (2.7MB each), 1MB SPIFFS"
            break
            ;;
        *)
            print_error "Invalid choice. Please enter 1 for 4MB or 2 for 8MB."
            ;;
    esac
done

print_success "Selected: $FLASH_SIZE flash configuration"

echo -e "\n${GRAY}Flash Configuration:${NC}"
echo -e "  • Chip: ${PURPLE}ESP32${NC}"
echo -e "  • Flash Size: ${PURPLE}$FLASH_SIZE${NC}"
echo -e "  • Configuration: ${PURPLE}$FLASH_CONFIG${NC}"
echo -e "  • Baud Rate: ${PURPLE}921,600 bps${NC}"
echo -e "  • Mode: ${PURPLE}DIO${NC}"
echo -e "  • Frequency: ${PURPLE}80MHz${NC}\n"

# Check if esptool is available
print_info "Checking for esptool installation..."
if ! command -v python3 >/dev/null 2>&1; then
    print_error "python3 not found. Please install Python 3."
    exit 1
fi

if ! python3 -m esptool version >/dev/null 2>&1; then
    print_error "esptool not found. Please run: pip3 install esptool"
    print_info "Or use the provided script: ./install-pip-esptool.sh"
    exit 1
fi

print_success "esptool found and ready"

# Check for required files
print_info "Verifying firmware files..."
required_files=("msp-firmware.ino.bootloader.bin" "msp-firmware.ino.partitions.bin" "boot_app0.bin" "msp-firmware.ino.bin")
missing_files=0

for file in "${required_files[@]}"; do
    if [ ! -f "$file" ]; then
        print_error "Required file missing: $file"
        missing_files=$((missing_files + 1))
    else
        size=$(stat -f%z "$file" 2>/dev/null || stat -c%s "$file" 2>/dev/null)
        size_kb=$((size / 1024))
        echo -e "  ${GREEN}✓${NC} $file ${GRAY}(${size_kb}KB)${NC}"
    fi
done

if [ $missing_files -gt 0 ]; then
    print_error "Cannot proceed with missing files. Please ensure all firmware files are in the current directory."
    exit 1
fi

print_success "All firmware files verified"

# Warning message
echo -e "\n${YELLOW}⚠ WARNING: This will completely erase your ESP32 and flash new firmware!${NC}"
echo -e "${GRAY}Make sure your ESP32 is connected and in download mode if required.${NC}\n"

read -p "Press Enter to continue or Ctrl+C to abort..."

# Step 1: Erase flash
print_step "1" "Erasing ESP32 flash memory..."
echo -e "${GRAY}This will remove all existing firmware and data...${NC}\n"

if python3 -m esptool --chip "$CHIP" --baud "$BAUD" --before default_reset --after hard_reset erase_flash; then
    print_success "Flash memory erased successfully"
else
    print_error "Failed to erase flash memory"
    exit 1
fi

# Step 2: Write firmware
print_step "2" "Writing firmware to ESP32..."
echo -e "${GRAY}Writing bootloader, partitions, boot_app0, and application...${NC}\n"

if python3 -m esptool --chip "$CHIP" --baud "$BAUD" --before default_reset --after hard_reset write_flash -z \
    --flash_mode "$FLASH_MODE" --flash_freq "$FLASH_FREQ" --flash_size "$FLASH_SIZE" \
    0x1000 msp-firmware.ino.bootloader.bin \
    0x8000 msp-firmware.ino.partitions.bin \
    0xe000 boot_app0.bin \
    0x10000 msp-firmware.ino.bin; then
    print_success "Firmware written successfully"
else
    print_error "Failed to write firmware"
    exit 1
fi

# Step 3: Completion
print_step "3" "Flashing process completed!"

# Final success message
print_header "✓ FIRMWARE FLASHING SUCCESSFUL!"

echo -e "${GREEN}Your ESP32 has been successfully flashed with MSP firmware!${NC}\n"

echo -e "${WHITE}Device Information:${NC}"
echo -e "  • Flash Size: ${GREEN}$FLASH_SIZE${NC}"
if [ "$FLASH_SIZE" = "4MB" ]; then
    echo -e "  • Partition Layout: ${GREEN}Dual OTA (1.75MB each)${NC}"
    echo -e "  • SPIFFS: ${YELLOW}Disabled${NC} (maximized app space)"
else
    echo -e "  • Partition Layout: ${GREEN}Dual OTA (2.7MB each)${NC}"
    echo -e "  • SPIFFS: ${GREEN}1MB${NC} (file system enabled)"
fi
echo -e "  • OTA Updates: ${GREEN}Enabled${NC} with rollback protection"

echo -e "\n${WHITE}Next Steps:${NC}"
echo -e "  1. ${CYAN}Disconnect and reconnect${NC} your ESP32"
echo -e "  2. ${CYAN}Monitor serial output${NC} to verify boot process"
echo -e "  3. ${CYAN}Check device logs${NC} for successful initialization"

echo -e "\n${GRAY}Serial monitoring commands:${NC}"
echo -e "  • ${WHITE}screen /dev/cu.SLAB_USBtoUART 115200${NC}"
echo -e "  • ${WHITE}minicom -D /dev/cu.SLAB_USBtoUART -b 115200${NC}"

print_header "Happy coding with Milano Smart Park! 🌟"
