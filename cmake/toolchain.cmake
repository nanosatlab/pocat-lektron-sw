set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(TOOLCHAIN_PREFIX arm-none-eabi-)

set(CMAKE_C_COMPILER ${TOOLCHAIN_PREFIX}gcc)
set(CMAKE_CXX_COMPILER ${TOOLCHAIN_PREFIX}g++)
set(CMAKE_ASM_COMPILER ${TOOLCHAIN_PREFIX}gcc)
set(CMAKE_OBJCOPY ${TOOLCHAIN_PREFIX}objcopy)
set(CMAKE_SIZE ${TOOLCHAIN_PREFIX}size)

set(CMAKE_C_FLAGS "-fdata-sections -ffunction-sections" CACHE STRING "C compiler flags")
set(CMAKE_CXX_FLAGS "-fdata-sections -ffunction-sections -fno-rtti -fno-exceptions -fno-threadsafe-statics" CACHE STRING "C++ compiler flags")
set(CMAKE_ASM_FLAGS "-fdata-sections -ffunction-sections" CACHE STRING "ASM compiler flags")
set(CMAKE_EXE_LINKER_FLAGS "-Wl,--gc-sections --specs=nano.specs" CACHE STRING "Linker flags")

set(CMAKE_EXECUTABLE_SUFFIX ".elf")

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

