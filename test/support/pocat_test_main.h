#ifndef POCAT_TEST_MAIN_H
#define POCAT_TEST_MAIN_H
/* Shared command-line handling for suites (Unity built with UNITY_USE_COMMAND_LINE_ARGS).
 *   <suite> -l          list test names and exit
 *   <suite> -f <text>   run only tests whose name contains <text>
 *   <suite> -n <name>   run only the test with exactly this name
 *   <suite> -x <text>   exclude tests whose name contains <text>
 * Usage in a suite:
 *   int main(int argc, char **argv) {
 *       if (pocat_parse_args(argc, argv) != 0) return 1;
 *       POCAT_BEGIN(); POCAT_RUN_TEST(test_x); ... return POCAT_END();
 *   }
 */
#include "unity.h"
#include <stdio.h>
#include <string.h>

static int pocat_list_only = 0;

static inline int pocat_parse_args(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-l") == 0) { pocat_list_only = 1; return 0; }
    }
    return UnityParseOptions(argc, argv);   /* -f <substr>, -n <exact>, -x <exclude> */
}

#define POCAT_BEGIN() do { if (!pocat_list_only) { UNITY_BEGIN(); } } while (0)
#define POCAT_END()   (pocat_list_only ? 0 : UNITY_END())

#define POCAT_RUN_TEST(fn) do { \
        if (pocat_list_only) { printf("%s\n", #fn); break; } \
        Unity.CurrentTestName = #fn; \
        if (UnityTestMatches()) { RUN_TEST(fn); } \
    } while (0)

#endif
