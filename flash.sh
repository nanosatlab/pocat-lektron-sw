#!/bin/bash

# Flash script for STM32 Nucleo using st-link
# Usage: ./flash.sh

set -e  # Exit on error

BUILD_DIR="build"
ELF_FILE="$BUILD_DIR/pocat_lektron_sw.elf"
BIN_FILE="$BUILD_DIR/pocat_lektron_sw.bin"
FLASH_ADDRESS="0x08000000"

echo "========================================="
echo "  STM32 Nucleo Flash Script"
echo "========================================="

# Check if build directory exists
if [ ! -d "$BUILD_DIR" ]; then
    echo "Error: Build directory not found!"
    echo "Please run ./build.sh first"
    exit 1
fi

# Check if ELF file exists
if [ ! -f "$ELF_FILE" ]; then
    echo "Error: $ELF_FILE not found!"
    echo "Please run ./build.sh first"
    exit 1
fi

# Check if st-flash is installed
if ! command -v st-flash &> /dev/null; then
    echo "Error: st-flash not found!"
    echo "Please install it with:"
    echo "  sudo apt install stlink-tools  # Debian/Ubuntu"
    echo "  sudo pacman -S stlink          # Arch Linux"
    exit 1
fi

# Check if arm-none-eabi-objcopy is installed
if ! command -v arm-none-eabi-objcopy &> /dev/null; then
    echo "Error: arm-none-eabi-objcopy not found!"
    echo "Please install arm-none-eabi toolchain"
    exit 1
fi

# Convert ELF to BIN
echo "Converting ELF to BIN..."
arm-none-eabi-objcopy -O binary "$ELF_FILE" "$BIN_FILE"
echo "Created: $BIN_FILE"

# Get file size
BIN_SIZE=$(stat -c%s "$BIN_FILE")
echo "Binary size: $BIN_SIZE bytes"

# Flash the board
echo ""
echo "Flashing to Nucleo board..."
echo "Address: $FLASH_ADDRESS"
st-flash write "$BIN_FILE" "$FLASH_ADDRESS"

echo ""
echo "========================================="
echo "  Flashing completed successfully!"
echo "========================================="
