#include <stdio.h>

extern "C" {

    int prova(void) {
        printf("1\n");
        return 0;   // 0 = OK
    }

    int prova_send(const char* msg) {
        printf("2");
        return 0;   // 0 = OK
    }

}
