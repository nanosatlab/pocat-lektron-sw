#!/bin/bash
#
# Flash build/pocat_lektron_sw.elf to an STM32 Nucleo-L476RG through its ST-LINK.
#
# Usage (Linux/macOS shell, or Git Bash on Windows), from anywhere:
#   bash flash.sh                 auto-detect a flashing tool and flash
#   bash flash.sh --tool <name>   force one tool: cube | openocd | stlink
#   bash flash.sh --dry-run       show the tool and command, flash nothing
#
# No machine-specific paths: the script looks for a flashing tool on PATH and in
# the standard install locations, in this order:
#   1. cube     STM32_Programmer_CLI (STM32CubeProgrammer, or the copy bundled with STM32CubeIDE)
#   2. openocd  OpenOCD (on Windows also the MSYS2 package)
#   3. stlink   st-flash (stlink-tools)
# If your tool is installed somewhere else, add its folder to PATH.
#
# cube and openocd program the ELF directly, so only the sections of the image are
# written and the rest of the flash (parameters, historic telemetry) is left alone.
# st-flash needs a raw .bin: it is made with arm-none-eabi-objcopy, taken from PATH
# or, if missing, from the pocat-build-env Docker image.

set -e
cd "$(dirname "$0")"

ELF_FILE="build/pocat_lektron_sw.elf"
BIN_FILE="build/pocat_lektron_sw.bin"
FLASH_ADDRESS="0x08000000"

TOOL=""
DRY_RUN=0
while [ $# -gt 0 ]; do
    case "$1" in
        --tool)    TOOL="$2"; shift ;;
        --dry-run) DRY_RUN=1 ;;
        -h|--help) sed -n '3,20p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "Unknown argument: $1 (see bash flash.sh --help)" >&2; exit 1 ;;
    esac
    shift
done

if [ ! -f "$ELF_FILE" ]; then
    echo "Error: $ELF_FILE not found. Build the firmware first:" >&2
    echo '  docker run --rm -v "${PWD}:/app" pocat-build-env' >&2
    exit 1
fi

# Print the first argument that is an executable file. Unmatched globs are skipped.
first_executable() {
    local p
    for p in "$@"; do
        if [ -f "$p" ] && [ -x "$p" ]; then echo "$p"; return 0; fi
    done
    return 1
}

find_cube() {
    command -v STM32_Programmer_CLI 2>/dev/null && return 0
    first_executable \
        "/c/Program Files/STMicroelectronics/STM32Cube/STM32CubeProgrammer/bin/STM32_Programmer_CLI.exe" \
        /c/ST/STM32CubeIDE_*/STM32CubeIDE/plugins/com.st.stm32cube.ide.mcu.externaltools.cubeprogrammer.*/tools/bin/STM32_Programmer_CLI.exe \
        "$HOME/STMicroelectronics/STM32Cube/STM32CubeProgrammer/bin/STM32_Programmer_CLI" \
        /opt/st/stm32cubeide_*/plugins/com.st.stm32cube.ide.mcu.externaltools.cubeprogrammer.*/tools/bin/STM32_Programmer_CLI \
        "/Applications/STMicroelectronics/STM32Cube/STM32CubeProgrammer/STM32CubeProgrammer.app/Contents/MacOs/bin/STM32_Programmer_CLI"
}

find_openocd() {
    command -v openocd 2>/dev/null && return 0
    first_executable \
        /c/msys64/ucrt64/bin/openocd.exe \
        /c/msys64/mingw64/bin/openocd.exe
}

find_stlink() {
    command -v st-flash 2>/dev/null
}

# Pick the tool: the forced one, or the first one found.
TOOL_PATH=""
if [ -n "$TOOL" ]; then
    case "$TOOL" in
        cube)    TOOL_PATH="$(find_cube)" || true ;;
        openocd) TOOL_PATH="$(find_openocd)" || true ;;
        stlink)  TOOL_PATH="$(find_stlink)" || true ;;
        *) echo "Error: unknown tool '$TOOL' (use cube, openocd or stlink)" >&2; exit 1 ;;
    esac
    if [ -z "$TOOL_PATH" ]; then
        echo "Error: tool '$TOOL' was requested but is not installed or not on PATH" >&2
        exit 1
    fi
else
    for candidate in cube openocd stlink; do
        if TOOL_PATH="$(find_$candidate)" && [ -n "$TOOL_PATH" ]; then
            TOOL="$candidate"
            break
        fi
    done
    if [ -z "$TOOL" ]; then
        echo "Error: no flashing tool found. Install one of these (and add it to PATH if needed):" >&2
        echo "  - STM32CubeProgrammer (or STM32CubeIDE, which bundles it)   https://www.st.com/en/development-tools/stm32cubeprog.html" >&2
        echo "  - OpenOCD      Windows: MSYS2 'pacman -S mingw-w64-ucrt-x86_64-openocd' | Linux: 'sudo apt install openocd'" >&2
        echo "  - stlink-tools Linux: 'sudo apt install stlink-tools' | macOS: 'brew install stlink'" >&2
        exit 1
    fi
fi

case "$TOOL" in
    cube)    CMD=("$TOOL_PATH" -c port=SWD -w "$ELF_FILE" -v -rst) ;;
    openocd) CMD=("$TOOL_PATH" -f interface/stlink.cfg -f target/stm32l4x.cfg
                  -c "program $ELF_FILE verify reset exit") ;;
    stlink)  CMD=("$TOOL_PATH" --reset write "$BIN_FILE" "$FLASH_ADDRESS") ;;
esac

echo "Tool:    $TOOL ($TOOL_PATH)"
echo "Image:   $ELF_FILE"
echo "Command: ${CMD[*]}"
if [ "$DRY_RUN" -eq 1 ]; then
    echo "Dry run: nothing was flashed."
    exit 0
fi

if [ "$TOOL" = "stlink" ]; then
    echo "Converting ELF to BIN..."
    if command -v arm-none-eabi-objcopy > /dev/null 2>&1; then
        arm-none-eabi-objcopy -O binary "$ELF_FILE" "$BIN_FILE"
    else
        MSYS_NO_PATHCONV=1 docker run --rm -v "$(pwd -W 2>/dev/null || pwd):/app" pocat-build-env \
            arm-none-eabi-objcopy -O binary "$ELF_FILE" "$BIN_FILE"
    fi
fi

echo "Flashing the Nucleo board..."
"${CMD[@]}"
echo "Flashing completed."
