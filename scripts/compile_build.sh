#!/bin/bash

# Change to the parent directory
cd ..

# Remove the existing build directory (if it exists)
rm -r build

# Create a new build directory
mkdir build

# Change to the build directory
cd build

# Configure the project with CMake
cmake ..

# Compile the project with make
make