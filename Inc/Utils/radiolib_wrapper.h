#ifndef INC_WRAPPER_H
#define INC_WRAPPER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "RadioLib.h"

int prova(void);
int prova_send(const char* msg);

#ifdef __cplusplus
}
#endif

#endif