#include <stdio.h>
#include "radiolib_wrapper.h"
#include "stm32_radiolib_hal.h"

// Instantiate C++ outside of extern "C"
static stm32RadioLibHal hal(&hspi2);  

static Module mod(&hal, 1, 2, 3, 4); // TODO: change pins to actual ones (these are made up)
static SX1262 radio(&mod);

extern "C" { // to stop name mangling

    

}
