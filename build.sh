#!/bin/bash

# Usage: ./build.sh [--mock] [--eps-tests] [--clean] [--test-instr]
#   --mock       Build with the mock radio (no hardware required)
#   --eps-tests  Build the on-target EPS test firmware (pocat_eps_tests.elf)
#   --clean       Remove the build directory before building (force full rebuild)
#   --test-instr  Enable bench test campaign timing instrumentation
#                 (auth tag / CAD scan microsecond timing over USART2,
#                 see ir-report/test-campaign.md). Off by default.
#
# Requires POCAT_PSK (32 hex chars = 16-byte PSK, §4.7) to be set in the
# environment.  build.sh generates include/subsystems/comms/psk.h from the
# template psk.h.in; psk.h is excluded from VCS (see .gitignore).

RADIO_MOCK=OFF
EPS_TESTS=OFF
CLEAN=0
TEST_INSTR=OFF
for arg in "$@"; do
    case "$arg" in
        --mock)        RADIO_MOCK=ON ;;
        --eps-tests)   EPS_TESTS=ON ;;
        --clean)       CLEAN=1 ;;
        --test-instr)  TEST_INSTR=ON ;;
    esac
done

# ---- Generate psk.h from POCAT_PSK ----
PSK_HEX="${POCAT_PSK:-}"
if [ -z "$PSK_HEX" ]; then
    echo "ERROR: POCAT_PSK is not set (must be 32 hex chars = 16-byte PSK)" >&2
    exit 1
fi
if [ "${#PSK_HEX}" -ne 32 ]; then
    echo "ERROR: POCAT_PSK must be exactly 32 hex characters (got ${#PSK_HEX})" >&2
    exit 1
fi
# Convert hex pairs to C byte literals: 0x00, 0x01, ...
PSK_BYTES=""
for i in $(seq 0 2 30); do
    byte="0x${PSK_HEX:$i:2}u"
    if [ $i -lt 30 ]; then
        PSK_BYTES="${PSK_BYTES}${byte}, "
    else
        PSK_BYTES="${PSK_BYTES}${byte}"
    fi
done
PSK_H="include/subsystems/comms/psk.h"
sed "s|@PSK_BYTES@|${PSK_BYTES}|" include/subsystems/comms/psk.h.in > "${PSK_H}"
echo "Generated ${PSK_H}"

# Let's build the project
if [ "$CLEAN" -eq 1 ]; then
    rm -rf build
fi
mkdir -p build
cd build
cmake .. -DRADIO_MOCK=$RADIO_MOCK -DPOCAT_TEST_INSTRUMENTATION=$TEST_INSTR -DEPS_TESTS=$EPS_TESTS
make -j4    # 4 threads (Adjust to your number of cores)
cd ..

# # Move the generated file to the root directory
# SOURCE="build/pocat_sw.elf"
# DESTINATION="pocat_sw.elf"
# mv "$SOURCE" "$DESTINATION"

# # Remove the build directory
# if [ ! -d "build/pocat_sw.elf" ]; then
#     rm -r build
# else
#     echo "Error: The file was not moved to the root directory"
# fi
