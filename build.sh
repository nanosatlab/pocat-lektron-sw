#!/bin/bash

# Let's build the project
rm -rf build
mkdir build
cd build
cmake ..
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
