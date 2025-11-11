#include <stdio.h>
#include "radiolib_wrapper.h"
#include "stm32_radiolib_hal.h"

// Instantiate C++ outside of extern "C"
static stm32RadioLibHal hal(&hspi2);  

static Module mod(&hal, 1, 2, 3, 4); // TODO: change pins to actual ones (these are made up) NSS, DIO1, BUSY, RESET
static SX1262 radio(&mod);

static RadioEvents_t* radioEventsPtr = nullptr;

extern "C" { // to stop name mangling

    void RadioLib_Init(RadioEvents_t *events) {
        radioEventsPtr = events;
        const int state = radio.begin(); //S'initzalitza la radio amb els valors per defecte: Freq = 434.0MHz, BW = 125kHz, SF = 9, CR = 4/7, SyncWord = private network, Power = 10dBm, PreambleLength = 8 symbols
        if(state != RADIOLIB_ERR_NONE) {
            // Handle error (could add error callback or logging)
            //printf("Error en la inicialitzacio de la radio: %d\n", state);
        }
        else{
            // Successfully initialized
            //printf("Radio inicialitzada correctament.\n");
        }
    }

    void RadioLib_SetChannel(uint32_t freq) {
        radio.setFrequency(freq);
    }

    void RadioLib_SetTxConfig(uint8_t sf, uint8_t cr, int8_t power) {
        
    }


}
