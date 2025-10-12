#include <stdio.h>
#include "radiolib_wrapper.h"

extern "C" {

    SX1262 radio = new Module(10, 2, 3, 9);

    int prova(void) {
        printf("1\n");
        return 0;   // 0 = OK
    }

    int prova_send(const char* msg) {
        printf("2");
        return 0;   // 0 = OK
    }

}
