# This CMakeLists is only executed if BUILD_TESTING is ON
project(pocat_tests)

# 1. Compile the Unity testing framework into a reusable static library
add_library(unity STATIC framework/unity/unity.c)
target_include_directories(unity PUBLIC framework/unity)

# 2. Add our specific test executable
add_executable(test_ht_handling 
    suites/test_ht_handling.c
    support/fake_flash_memory.c
    mocks/mock_obdh_requests.c
    mocks/mock_pocket_plus.c
)

# 3. Tell the compiler where to find headers, PRIORITIZING OUR MOCKS
target_include_directories(test_ht_handling PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/support
    ${CMAKE_CURRENT_SOURCE_DIR}/mocks/stubs_project
    ${CMAKE_CURRENT_SOURCE_DIR}/mocks/stubs_hal
    ${CMAKE_SOURCE_DIR}/src/subsystems/comms
    ${CMAKE_SOURCE_DIR}/include/subsystems/comms
    ${CMAKE_SOURCE_DIR}/include/subsystems/obdh
    ${CMAKE_SOURCE_DIR}/include/utils
)

# 4. Link the test against the Unity library
target_link_libraries(test_ht_handling PRIVATE unity)

# 5. Register the test so it runs when we type "ctest" or "make test"
add_test(NAME HT_Handling_Tests COMMAND test_ht_handling)
