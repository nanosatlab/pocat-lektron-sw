# Post-build guard for the flight image: fails the build, and deletes the ELF so
# it cannot be flashed, if any symbol from the on-target test firmware
# (eps_mock.c, test_eps.c, UNIT_TEST hooks) was linked into it.
#
# Usage: cmake -DNM=<nm> -DELF=<flight.elf> -P check_flight_symbols.cmake

execute_process(
    COMMAND ${NM} ${ELF}
    OUTPUT_VARIABLE symbols
    RESULT_VARIABLE nm_result
)
if(NOT nm_result EQUAL 0)
    message(FATAL_ERROR "check_flight_symbols: ${NM} failed on ${ELF}")
endif()

set(test_symbol_regex
    "[^\n]* ([A-Za-z0-9_]*_Mock[A-Za-z0-9_]*|mock_[A-Za-z0-9_]+|g_mock_[A-Za-z0-9_]+|EPS_Test_[A-Za-z0-9_]+|test_runner|run_hw_suite|hw_gauge_[A-Za-z0-9_]+|hw_measure_[A-Za-z0-9_]+)\n")
string(REGEX MATCHALL "${test_symbol_regex}" hits "${symbols}")

if(hits)
    file(REMOVE ${ELF})
    string(REPLACE ";" "" hits "${hits}")
    message(FATAL_ERROR
        "Flight image contains test/mock symbols; ${ELF} was deleted:\n${hits}")
endif()
