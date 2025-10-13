#ifndef INC_WRAPPER_H
#define INC_WRAPPER_H

#ifdef __cplusplus

#include "main.h"
#include "RadioLib.h"

extern "C" {
#endif


int prova(void);
int prova_send(const char* msg);



#ifdef __cplusplus
}
#endif

#endif