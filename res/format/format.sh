#!/bin/bash

# List of directories to format
DIRECTORIES=("Inc/Subsystems" "Inc/Tasks" "Inc/Utils" "Src/Common" "Src/Subsystems" "Src/Tasks" "Src/Utils")

for DIR in "${DIRECTORIES[@]}"; do
    find "$DIR" -name '*.c' -o -name '*.h' | xargs clang-format -i
done

DIRECTORIES=("Inc/" "Src/")
for DIR in "${DIRECTORIES[@]}"; do
    find "$DIR" -name 'main.c' -o -name 'main.h' -not -path "$DIR/Common/*" | xargs clang-format -i
done

echo "Formatting complete."