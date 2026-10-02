#!/bin/bash
# Stop execution if any command fails
set -e

echo "================================="
echo "   Building Native Unit Tests"
echo "================================="

# Create and enter a dedicated build folder for tests
mkdir -p build_test
cd build_test
rm -f CMakeCache.txt


# Run CMake, explicitly enabling the testing infrastructure
cmake -DBUILD_TESTING=ON ..

# Compile ONLY the test target (do not try to build the STM32 elf)
make -j$(nproc) test_ht_handling

echo ""
echo "================================="
echo "       Executing Tests"
echo "================================="

# Run the tests. If a test fails, print the exact error output.
ctest --output-on-failure
