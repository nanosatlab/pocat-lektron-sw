#if 0
#ifndef RADIO_MOCK

#include "radiolib_wrapper.h"

/* C++ headers */
#include <RadioLib.h>
#include "stm32_radiolib_hal.h"


// Instantiate C++ outside of extern "C"
static stm32RadioLibHal hal(&hspi2);  

static Module mod(&hal, 1, 2, 3, 4); // TODO: change pins to actual ones (these are made up) NSS, DIO1, BUSY, RESET
static SX1262 radio(&mod);

static RadioEvents_t* radioEventsPtr = nullptr;

static float bwCodeToKHz(uint8_t bw_code) {
  switch(bw_code) {
    case 0: return 125.0f;
    case 1: return 250.0f;
    case 2: return 500.0f;
    default: return 125.0f;
  }
}

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

    void RadioLib_SetChannel(uint32_t freq_hz) {
       float freq_mhz = freq_hz / 1000000.0f;
       const int state = radio.setFrequency(freq_mhz); //esperem MHz
       if(state != RADIOLIB_ERR_NONE) {
           // Handle error (could add error callback or logging)
           //printf("Error en la configuracio de la frequencia: %d\n", state);
       }
    }

    void RadioLib_SetTxConfig(uint8_t sf, uint8_t cr, int8_t power,
                            uint8_t bw, int iqInverted,
                            int crcOn, uint16_t preambleLen)
    {
        int state;

        // Set output power
        state = radio.setOutputPower(power);
        if (state != RADIOLIB_ERR_NONE) {
            // Handle error
        }

        // Set spreading factor
        state = radio.setSpreadingFactor(sf);
        if (state != RADIOLIB_ERR_NONE) {
            // Handle error
        }

        // Set coding rate (RadioLib CR uses values 5–8)
        state = radio.setCodingRate(cr + 4);
        if (state != RADIOLIB_ERR_NONE) {
            // Handle error
        }

        // Set bandwidth
        state = radio.setBandwidth(bwCodeToKHz(bw));
        if (state != RADIOLIB_ERR_NONE) {
            // Handle error
        }

        // CRC enable/disable
        state = radio.setCRC(crcOn);
        if (state != RADIOLIB_ERR_NONE) {
            // Handle error
        }

        // Preamble length
        state = radio.setPreambleLength(preambleLen);
        if (state != RADIOLIB_ERR_NONE) {
            // Handle error
        }

        // Note: IQ inversion is not a separate method in RadioLib SX1262
        // If needed, handle in modulation settings or ignore.
    }

    void RadioLib_SetRxConfig(uint8_t sf, uint8_t cr, uint8_t bw,
                            int iqInverted, int crcOn,
                            uint16_t preambleLen)
    {
        int state;

        // Set spreading factor
        state = radio.setSpreadingFactor(sf);
        if (state != RADIOLIB_ERR_NONE) {
            // Handle error
        }

        // Set coding rate
        state = radio.setCodingRate(cr + 4);
        if (state != RADIOLIB_ERR_NONE) {
            // Handle error
        }

        // Set bandwidth
        state = radio.setBandwidth(bwCodeToKHz(bw));
        if (state != RADIOLIB_ERR_NONE) {
            // Handle error
        }

        // CRC enable/disable
        state = radio.setCRC(crcOn);
        if (state != RADIOLIB_ERR_NONE) {
            // Handle error
        }

        // Preamble length
        state = radio.setPreambleLength(preambleLen);
        if (state != RADIOLIB_ERR_NONE) {
            // Handle error
        }

        // Note: IQ inversion doesn’t have a separate setter in RadioLib SX1262
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
            // Hem de tractar la resta d'errors aqui
        }
        return st;
    }

    void RadioLib_Send(uint8_t *buf, uint16_t len) {
        int st = radio.transmit(buf, len);
        if(st == RADIOLIB_ERR_NONE) {
            if(radioEventsPtr && radioEventsPtr->TxDone) {
                radioEventsPtr->TxDone();
            }
        }
        else if(st == RADIOLIB_ERR_TX_TIMEOUT) {
            if(radioEventsPtr && radioEventsPtr->TxTimeout) {
                radioEventsPtr->TxTimeout();
            }
        }
        else{
            // Hem de tractar la resta d'errors aqui
        }
    }

    int16_t RadioLib_Sleep(void) {
        return radio.sleep();
    }

    int16_t RadioLib_Standby(void) {
        return radio.standby();
    }

    int16_t RadioLib_StartCad(void) {  // Es fa servir per CAD real
        int16_t st = radio.scanChannel();

        int detected = 0;
        if(st == RADIOLIB_LORA_DETECTED) {
            detected = 1;
        } else if(st == RADIOLIB_CHANNEL_FREE) {
           detected = 0;
        } else {
           // La resta d'errors s'han de tractar aqui
        }

        if(radioEventsPtr && radioEventsPtr->CadDone) {
            radioEventsPtr->CadDone(detected);
        }
        return st;
    }

    void RadioLib_IrqProcess(void) {
        // De moment no fa falta implementar res aqui ja que radiolib gestiona els interrupts internament.
        // No obstant, si ens posem en un mode no bloquejant, potser caldra implementar alguna cosa aqui.
    }




}

#endif /* RADIO_MOCK */
#endif /* 0 */