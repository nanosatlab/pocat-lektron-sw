#ifndef COMMS_INTERNAL_H_
#define COMMS_INTERNAL_H_

#include "sx126x.h"
#include "FreeRTOS.h"
#include "task.h"

/**
 * @brief Configuration parameters.
 */

#define TX_OUTPUT_POWER                     18        // dBm

#define LORA_BANDWIDTH                      0         // [0: 125 kHz,
                                                      //  1: 250 kHz,
                                                      //  2: 500 kHz,
                                                      //  3: Reserved]
#define LORA_PREAMBLE_LENGTH                8//108    // Same for Tx and Rx
#define LORA_PREAMBLE_LENGTH                8//108    // Same for Tx and Rx
#define LORA_SYMBOL_TIMEOUT                 100       // Symbols
#define LORA_FIX_LENGTH_PAYLOAD_ON          false
#define LORA_IQ_INVERSION_ON                false

// Transmit message types 
#define ACK_M     							2
#define DATA_M  							3

//CAD parameters
#define CAD_SYMBOL_NUM          LORA_CAD_02_SYMBOL
#define CAD_DET_PEAK            23
#define CAD_DET_MIN             1


/**
 * @brief Possible states of the communications state machine.
 */
typedef enum {
        STARTUP,
        TRANSMIT,
        RECEIVE,
        SLEEP,
        STANDBY,
} CommsState_t;

typedef enum {
    PING,
    PAYLOAD_SEND_DATA,
} telecommandId_t;

typedef struct {
    uint8_t RxData[48];
    uint8_t TxData[48];
    uint8_t packetWindow;
} CommsPackets_t;

/**
 * @brief Flags used in the COMMS state machine.
 */
typedef struct {
    bool cadMode;
    bool callbackFinished;
    bool cadRx;
    bool txAck;
    bool txPayload;
} CommsFlags_t;

/**
 * @brief Configuration settings
 */
typedef struct {
    uint32_t RF_F; // Hz
    uint32_t sleepTime;
    uint32_t rxTime;
    uint16_t ackTime;
} CommsSettings_t;

void NextState(void);

void ProcessRadioCallbacks(void);

/*!

 * \brief Function to initialize the Radio and its parameters
 */

void Startup(void);

// EXPLANATION

void Sleep(void);

void Receive(void);

void Transmit(void);

void StandBy(void);

/*!
 * \brief Function to be executed on Radio Rx Done event
 */
void OnRxDone( uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr ); 

/*!
 * \brief Function to be executed on Radio Tx Done event
 */
void OnTxDone( void );


/*!
 * \brief Function executed on Radio Tx Timeout event
 */
void OnTxTimeout( void );

/*!
 * \brief Function executed on Radio Rx Timeout event
 */
void OnRxTimeout( void );

/*!
 * \brief Function executed on Radio Rx Error event
 */
void OnRxError( void );

/*!
 * \brief Function executed on Radio CAD Done event
 */
void OnCadDone( bool channelActivityDetected);


/**
 * @brief Configures the SX1262 module with specified parameters.
 *
 * @param SF Spreading factor.
 * @param CR Coding rate.
 * @param RF_F RF frequency.
 */
void SX1262Config(uint8_t SF, uint8_t CR, uint32_t RF_F);


/*!
 * \brief Function configuring CAD parameters
 * \param [in]  cadSymbolNum   The number of symbol to use for CAD operations
 *                             [LORA_CAD_01_SYMBOL, LORA_CAD_02_SYMBOL,
 *                              LORA_CAD_04_SYMBOL, LORA_CAD_08_SYMBOL,
 *                              LORA_CAD_16_SYMBOL]
 * \param [in]  cadDetPeak     Limit for detection of SNR peak used in the CAD
 * \param [in]  cadDetMin      Set the minimum symbol recognition for CAD
 * \param [in]  cadTimeout     Defines the timeout value to abort the CAD activity
 */
void SX126xConfigureCad( RadioLoRaCadSymbols_t cadSymbolNum, uint8_t cadDetPeak, uint8_t cadDetMin , uint32_t cadTimeout);

CommsState_t ProcessTelecommand();

void TxPrepare(uint8_t messageType);

void Interleave(uint8_t *inputarr, int size);

void Deinterleave(uint8_t *inputarr, int size);

#endif /* COMMS_INTERNAL_H_ */
