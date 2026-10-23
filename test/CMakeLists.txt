# This CMakeLists is only executed if BUILD_TESTING is ON (see root CMakeLists.txt).
project(pocat_tests C)

set(TEST_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(SRC_DIR  ${CMAKE_SOURCE_DIR}/src)
option(POCAT_TEST_SANITIZERS "Build host tests with ASan+UBSan" ON)

# 1. Unity, compiled once as a library (command-line args enable -l/-f/-n/-x, see support/pocat_test_main.h)
add_library(unity STATIC framework/unity/unity.c)
target_include_directories(unity PUBLIC framework/unity)
target_compile_definitions(unity PUBLIC UNITY_USE_COMMAND_LINE_ARGS)

# 2. Test-only PSK (NOT the flight key). psk.h.in uses @PSK_BYTES@.
set(PSK_BYTES "0x00u, 0x01u, 0x02u, 0x03u, 0x04u, 0x05u, 0x06u, 0x07u, 0x08u, 0x09u, 0x0Au, 0x0Bu, 0x0Cu, 0x0Du, 0x0Eu, 0x0Fu")
configure_file(${CMAKE_SOURCE_DIR}/include/subsystems/comms/psk.h.in
               ${CMAKE_CURRENT_BINARY_DIR}/generated/psk.h @ONLY)

# 3. Include path. Order matters: the stubs shadow the real HAL/FreeRTOS headers.
set(POCAT_TEST_COMMON_INCLUDES
    ${CMAKE_CURRENT_BINARY_DIR}/generated      # test psk.h wins over any dev psk.h
    ${TEST_DIR}/support ${TEST_DIR}/mocks
    ${TEST_DIR}/mocks/stubs_hal ${TEST_DIR}/mocks/stubs_rtos
    ${PROJECT_INCLUDE_DIRECTORIES}             # real headers, from root CMakeLists
    ${SRC_DIR}/utils ${SRC_DIR}/subsystems/obc ${SRC_DIR}/subsystems/obdh
    ${SRC_DIR}/subsystems/comms ${SRC_DIR}/subsystems/eps ${SRC_DIR}/subsystems/adcs
    ${SRC_DIR}/subsystems/payloads ${SRC_DIR}/subsystems/transceiver)  # so suites can #include "x.c"

add_custom_target(pocat_tests)   # run_tests.sh builds only this (never the default ARM target)

# pocat_add_suite(<name> [LABEL l] [NO_FAKES] [NO_SANITIZE] [SOURCES ...] [INCLUDES dirs-searched-first ...])
#   <name> must match suites/<name>.c. Source paths are relative to test/ or absolute.
function(pocat_add_suite NAME)
  cmake_parse_arguments(S "NO_FAKES;NO_SANITIZE" "LABEL" "SOURCES;INCLUDES" ${ARGN})
  set(srcs ${TEST_DIR}/suites/${NAME}.c ${S_SOURCES})
  if(NOT S_NO_FAKES)
    list(APPEND srcs ${TEST_DIR}/mocks/fake_rtos.c ${TEST_DIR}/mocks/fake_hal.c)
  endif()
  add_executable(${NAME} ${srcs})
  target_include_directories(${NAME} PRIVATE ${S_INCLUDES} ${POCAT_TEST_COMMON_INCLUDES})
  target_link_libraries(${NAME} PRIVATE unity)
  target_compile_options(${NAME} PRIVATE -Wall -Wextra -Wno-unused-parameter -g -O0)
  if(POCAT_TEST_SANITIZERS AND NOT S_NO_SANITIZE)
    target_compile_options(${NAME} PRIVATE -fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer)
    target_link_options(${NAME} PRIVATE -fsanitize=address,undefined)
  endif()
  add_test(NAME ${NAME} COMMAND ${NAME})
  if(S_LABEL)
    set_tests_properties(${NAME} PROPERTIES LABELS "${S_LABEL}")
  endif()
  add_dependencies(pocat_tests ${NAME})
endfunction()

# 4. Suites (one line each; run_tests.sh needs no change when you add one)
pocat_add_suite(test_ht_handling LABEL comms NO_FAKES
  INCLUDES ${TEST_DIR}/mocks/stubs_project    # stub flash.h shadows the real one ONLY for this suite
  SOURCES support/fake_flash_memory.c mocks/mock_obdh_requests.c mocks/mock_pocket_plus.c)
pocat_add_suite(test_notifications LABEL infra
  SOURCES ${SRC_DIR}/utils/notifications.c)
pocat_add_suite(test_task_management LABEL obc            # suite #includes task_management.c + health.c
  SOURCES ${SRC_DIR}/utils/notifications.c mocks/stub_task_entries.c support/sim_tasks.c)
pocat_add_suite(test_state_machine LABEL obc              # suite #includes state_machine.c + task_management.c + health.c
  SOURCES ${SRC_DIR}/utils/notifications.c mocks/stub_task_entries.c support/sim_tasks.c
          mocks/fake_clock.c mocks/fake_obdh_requests.c)
pocat_add_suite(test_comms_frame LABEL comms              # suite #includes auth.c (static sha256/hmac)
  SOURCES ${SRC_DIR}/subsystems/comms/frame.c ${SRC_DIR}/subsystems/comms/dedup.c)
