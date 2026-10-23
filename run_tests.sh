#!/bin/bash
# bash run_tests.sh                    -> all suites
# bash run_tests.sh --list             -> list suites
# bash run_tests.sh --label <label>    -> suites with that ctest label (obc, comms, infra)
# bash run_tests.sh <regex>            -> ctest -R <regex>
# bash run_tests.sh <suite> -f <text>  -> run one suite binary, only tests whose name contains <text>
# bash run_tests.sh <suite> -l         -> list test names in a suite
set -e
mkdir -p build_test && cd build_test && rm -f CMakeCache.txt
cmake -DBUILD_TESTING=ON .. > /dev/null
make -j"$(nproc)" pocat_tests
case "$1" in
  "")      ctest --output-on-failure ;;
  --list)  ctest -N ;;
  --label) ctest --output-on-failure -L "$2" ;;
  *) if [ $# -ge 2 ] && [ -x "test/$1" ]; then s="$1"; shift; "./test/$s" "$@";
     else ctest --output-on-failure -R "$1"; fi ;;
esac
