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
       const int state = radio.setFrequency(freq);
       if(state != RADIOLIB_ERR_NONE) {
           // Handle error (could add error callback or logging)
           //printf("Error en la configuracio de la frequencia: %d\n", state);
       }
    }

    void RadioLib_SetTxConfig(uint8_t sf, uint8_t cr, int8_t power,
                          uint8_t bw_code, bool iqInverted,
                          bool crcOn, uint16_t preambleLen) {
        const int state;
        state = radio.setSpreadingFactor(sf);
        if(state != RADIOLIB_ERR_NONE) {
            // Handle error (could add error callback or logging)
            //printf("Error en la configuracio del SF: %d\n", state);
        }
        state = radio.setCodingRate(cr + 4); //Radiolib es sumen 4 al CR
        if(state != RADIOLIB_ERR_NONE) {
            // Handle error (could add error callback or logging)
            //printf("Error en la configuracio del CR: %d\n", state);
        }
        state = radio.setTxPower(power);
        if(state != RADIOLIB_ERR_NONE) {
            // Handle error (could add error callback or logging)
            //printf("Error en la configuracio del Power: %d\n", state);
        }
        state = radio.setBandwidth(bw_code); //Ha de estar en kHz, no en indexos com Semtech
        if(state != RADIOLIB_ERR_NONE) {
            // Handle error (could add error callback or logging)
            //printf("Error en la configuracio del BW: %d\n", state);
        }
        state = radio.setIQInverted(iqInverted);
        if(state != RADIOLIB_ERR_NONE) {
            // Handle error (could add error callback or logging)
            //printf("Error en la configuracio del IQ: %d\n", state);
        }
        state = radio.setCRC(crcOn);
        if(state != RADIOLIB_ERR_NONE) {
            // Handle error (could add error callback or logging)
            //printf("Error en la configuracio del CRC: %d\n", state);
        }
        state = radio.setPreambleLength(preambleLen);
        if(state != RADIOLIB_ERR_NONE) {
            // Handle error (could add error callback or logging)
            //printf("Error en la configuracio del Preamble Length: %d\n", state);
        }
    }

    void RadioLib_SetRxConfig(uint8_t sf, uint8_t cr, uint8_t bw_code,
                          bool iqInverted, bool crcOn,
                          uint16_t preambleLen) {
        const int state;
        state = radio.setSpreadingFactor(sf);
        if(state != RADIOLIB_ERR_NONE) {
            // Handle error (could add error callback or logging)
            //printf("Error en la configuracio del SF: %d\n", state);
        }
        state = radio.setCodingRate(cr + 4); //Radiolib es sumen 4 al CR
        if(state != RADIOLIB_ERR_NONE) {
            // Handle error (could add error callback or logging)
            //printf("Error en la configuracio del SF: %d\n", state);
        }
        state = radio.setBandwidth(bw_code); //Ha de estar en kHz, no en indexos com Semtech
        if(state != RADIOLIB_ERR_NONE) {
            // Handle error (could add error callback or logging)
            //printf("Error en la configuracio del SF: %d\n", state);
        }
        state = radio.setIQInverted(iqInverted);
        if(state != RADIOLIB_ERR_NONE) {
            // Handle error (could add error callback or logging)
            //printf("Error en la configuracio del SF: %d\n", state);
        }
        state = radio.setCRC(crcOn);
        if(state != RADIOLIB_ERR_NONE) {
            // Handle error (could add error callback or logging)
            //printf("Error en la configuracio del SF: %d\n", state);
        }
        state = radio.setPreambleLength(preambleLen);
        if(state != RADIOLIB_ERR_NONE) {
            // Handle error (could add error callback or logging)
            //printf("Error en la configuracio del SF: %d\n", state);
        }
    }

    int16_t RadioLib_Rx(uint32_t timeoutMs) {
        //return radio.startReceive(timeoutMs); Aquesta es la manera de ferho no bloquejant
        int st = radio.receive(nullptr, 0, timeoutMs); //Aquesta es la manera de ferho bloquejant
        if(st == RADIOLIB_ERR_NONE) {
            uint16_t len = radio.getPacketLength();
            uint8_t buf[len];

            int stData = radio.readData(buf, len);
            if(stData == RADIOLIB_ERR_NONE) {
                int16_t rssi = radio.getRSSI();
                int8_t snr = radio.getSNR();
                if(radioEventsPtr && radioEventsPtr->RxDone) {
                    radioEventsPtr->RxDone(buf, len, rssi, snr);
                }
            } else {
                if(radioEventsPtr && radioEventsPtr->RxError) {
                    radioEventsPtr->RxError();
                }
            }

        }
        else if(st == RADIOLIB_ERR_RX_TIMEOUT) {
            if(radioEventsPtr && radioEventsPtr->RxTimeout) {
                radioEventsPtr->RxTimeout();
            }
        }
        else if(st == RADIOLIB_ERR_CRC_MISMATCH) {
            if(radioEventsPtr && radioEventsPtr->RxError) {
                radioEventsPtr->RxError();
            }
        }
        else{
            // Other errors can be handled here
        }
        return st;
    }


}
