cmake_minimum_required(VERSION 3.16)

# Load toolchain
set(CMAKE_TOOLCHAIN_FILE ${CMAKE_CURRENT_SOURCE_DIR}/cmake/toolchain.cmake)

project(pocat_lektron_sw)
set(PROJECT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/Src)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(MAIN_SOURCE_FILE main.c)

set(MCU_FAMILY STM32L4xx)   # MCU Family
set(MCU_MODEL STM32L476xx)  # MCU Model

set(CPU_PARAMETERS
    -mcpu=cortex-m4
    -mthumb
    -mfpu=fpv4-sp-d16
    -mfloat-abi=hard)

set(STARTUP_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/cmake/startup_stm32l476rgtx.s)
set(MCU_LINKER_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/cmake/STM32L476RGTX_FLASH.ld)

set(EXECUTABLE ${CMAKE_PROJECT_NAME}.elf)
enable_language(C ASM CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)
# set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD 20) # changed from 17 to 20 (radiolib is in c++20)

# Include folders

set(CUBEMX_INCLUDE_DIRECTORIES
    ${CMAKE_CURRENT_SOURCE_DIR}/Inc/Drivers/${MCU_FAMILY}_HAL_Driver/Inc
    ${CMAKE_CURRENT_SOURCE_DIR}/Inc/Drivers/${MCU_FAMILY}_HAL_Driver/Inc/Legacy
    ${CMAKE_CURRENT_SOURCE_DIR}/Inc/Drivers/CMSIS/Device/ST/${MCU_FAMILY}/Include
    ${CMAKE_CURRENT_SOURCE_DIR}/Inc/Drivers/CMSIS/Include
)

set(PROJECT_INCLUDE_DIRECTORIES
    ${CMAKE_CURRENT_SOURCE_DIR}/Inc/
    # ${CMAKE_CURRENT_SOURCE_DIR}/Inc/Common
    ${CMAKE_CURRENT_SOURCE_DIR}/Inc/Hal
    # ${CMAKE_CURRENT_SOURCE_DIR}/Inc/Interrupts
    # ${CMAKE_CURRENT_SOURCE_DIR}/Inc/Subsystem
    # ${CMAKE_CURRENT_SOURCE_DIR}/Inc/Tasks
    ${CMAKE_CURRENT_SOURCE_DIR}/Inc/Utils
    ${CMAKE_CURRENT_SOURCE_DIR}/Inc/Utils/Radiolib
    ${CMAKE_CURRENT_SOURCE_DIR}/Inc/Subsystems
    ${CMAKE_CURRENT_SOURCE_DIR}/Inc/Semtech/Inc
)

set(FREERTOS_INCLUDE_DIRECTORIES
    ${CMAKE_CURRENT_SOURCE_DIR}/Inc/Drivers/FreeRTOS/include
    ${CMAKE_CURRENT_SOURCE_DIR}/Inc/Drivers/FreeRTOS/CMSIS_RTOS
    ${CMAKE_CURRENT_SOURCE_DIR}/Inc/Drivers/FreeRTOS/portable/GCC/ARM_CM4F
)

set(RADIOLIB_INCLUDE_DIRECTORIES
    ${CMAKE_CURRENT_SOURCE_DIR}/Inc/RadioLib/src
)
	
# Gather sources #MIRAR
file(GLOB_RECURSE STM32CUBEMX_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/Inc/Drivers/*.c
)

file(GLOB_RECURSE PROJECT_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/*.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Inc/Semtech/*.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/Utils/*.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/Utils/Radiolib/*.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/Subsystems/*.c
)

file(GLOB_RECURSE FREERTOS_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/Inc/Drivers/FreeRTOS/*.c
)

file(GLOB_RECURSE RADIOLIB_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/Inc/RadioLib/src/modules/*.cpp
    ${CMAKE_SOURCE_DIR}/Inc/RadioLib/src/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Inc/RadioLib/src/protocols/*.cpp
    # Add other needed source directories
)

# Executable files
add_executable(${EXECUTABLE}
    ${STM32CUBEMX_SOURCES}
    ${FREERTOS_SOURCES}
    ${PROJECT_SOURCES}
    ${RADIOLIB_SOURCES}
    ${STARTUP_SCRIPT}
)


# Embedded macros(defines)
target_compile_definitions(${EXECUTABLE} PRIVATE
    ${MCU_MODEL}
    USE_HAL_DRIVER
)

# Add header directories (AFTER add_executable !!)
target_include_directories(${EXECUTABLE} PRIVATE
    ${CUBEMX_INCLUDE_DIRECTORIES}
    ${FREERTOS_INCLUDE_DIRECTORIES}
    ${PROJECT_INCLUDE_DIRECTORIES}
    ${RADIOLIB_INCLUDE_DIRECTORIES}
)

# Ensure Debug build type is set
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

# Compiler options
target_compile_options(${EXECUTABLE} PRIVATE
    ${CPU_PARAMETERS}
    -Wall
    -Wextra
    -Wpedantic
    -Wno-unused-parameter
    $<$<CONFIG:Debug>:-Og -g3 -ggdb>	# Ensure proper debugger flags
    $<$<CONFIG:Release>:-O2 -g0>	# Optimize in release mode
)


# Ensure debugging symbols in the final executable
target_link_options(${EXECUTABLE} PRIVATE
    -T${MCU_LINKER_SCRIPT}
    ${CPU_PARAMETERS}
    -Wl,-Map=${CMAKE_PROJECT_NAME}.map
    --specs=nosys.specs
    -Wl,--start-group
    -lc
    -lm
    -Wl,--end-group
    -Wl,--print-memory-usage
    -g3 -ggdb  # Ensures GDB debugging info
)

add_custom_command(TARGET ${EXECUTABLE} POST_BUILD
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${EXECUTABLE}>
)

