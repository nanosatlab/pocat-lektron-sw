#include <stdio.h>

extern "C" {

    int prova(void) {
        printf("Ha entrat a la primera\n");
        return 0;   // 0 = OK
    }

    int prova_send(const char* msg) {
        printf("segoooona");
        return 0;   // 0 = OK
    }

}
