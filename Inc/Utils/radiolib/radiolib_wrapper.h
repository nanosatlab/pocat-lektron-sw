#ifndef INC_WRAPPER_H
#define INC_WRAPPER_H

#ifdef __cplusplus

#include "main.h"
#include "RadioLib.h"

extern "C" {
#endif

void RadioLib_Init(RadioEvents_t *events); // Inicialitza la radio i assigna els callbacks

void RadioLib_SetChannel(uint32_t freq); // Configura la frequencia

void RadioLib_SetTxConfig(     // El CR de radioLib es diferente al de SX1262Config, hay que sumarle + 4, Configura els parametres de TX
    uint8_t sf, // Spreading Factor, controla la sensibilitat i el abast
    uint8_t cr, // Coding Rate, controla la correccio d'errors
    int8_t power, // Potencia de transmissio
    uint8_t bw_code, // Bandwidth, controla l'amplada de banda
    bool iqInverted, // Inversio de la senyal IQ (evitar colisions)
    bool crcOn, // Activar CRC (checksum per errors)
    uint16_t preambleLen); //Sequencia la sincronitzacio

void RadioLib_SetRxConfig( // Configura els parametres de RX
    uint8_t sf, // Spreading Factor, controla la sensibilitat i el abast
    uint8_t cr, // Coding Rate, controla la correccio d'errors
    uint8_t bw_code, // Bandwidth, controla l'amplada de banda
    bool iqInverted, // Inversio de la senyal IQ (evitar colisions)
    bool crcOn, // Activar CRC (checksum per errors)
    uint16_t preambleLen); //Sequencia la sincronitzacio

void RadioLib_Send(uint8_t *buf, uint16_t len); // Envia les dades

void RadioLib_Rx(uint32_t timeoutMs); // Entra en mode recepcio durant timeoutMs milisegons

void RadioLib_Sleep(); // Entra en mode sleep

void RadioLib_Standby(); // Entra en mode standby

void RadioLib_StartCad(); // Comença la detecció de canal

void RadioLib_IrqProcess(); // Funcio que no fa res pq radiolib ja fa la seva gestio, pero aixi corregim menys codi


typedef struct {
    void (*TxDone)(void); // Transmissio acabada
    void (*RxDone)(uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr); // Recepcio acabada
    void (*TxTimeout)(void); // Timeout de transmissio
    void (*RxTimeout)(void); // Timeout de recepcio
    void (*RxError)(void); // Error de recepcio
    void (*CadDone)(bool channelActivityDetected); // Deteccio de canal acabada
} RadioEvents_t;



#ifdef __cplusplus
}
#endif

#endif