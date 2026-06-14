#!/bin/bash

# Usage: ./build.sh [--mock] [--clean]
#   --mock   Build with the mock radio (no hardware required)
#   --clean  Remove the build directory before building (force full rebuild)

RADIO_MOCK=OFF
EPS_TESTS=OFF
CLEAN=0
for arg in "$@"; do
    case "$arg" in
        --mock)  RADIO_MOCK=ON ;;
        --tests) EPS_TESTS=ON ;;
        --clean) CLEAN=1 ;;
    esac
done

# Let's build the project
if [ "$CLEAN" -eq 1 ]; then
    rm -rf build
fi
mkdir -p build
cd build
cmake .. -DRADIO_MOCK=$RADIO_MOCK -DEPS_TESTS=$EPS_TESTS
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
