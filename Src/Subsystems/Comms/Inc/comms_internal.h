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

// Transmit options 
#define ACK_OP  							2

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
        PROCESS,
        SLEEP,
        STANDBY,
} CommsState_t;

typedef enum {
    OBC_SOFT_REBOOT,
    UPLOAD_COMMS_CONFIG,
    PING,
} telecommandId_t;

typedef struct {
    uint8_t RxData[48];
    uint8_t TxData[48];
    uint8_t TlcReceived;     
} CommsPackets_t;

/**
 * @brief Flags used in the COMMS state machine.
 */
typedef struct {
    bool tlcReceived; // Hz
    bool cadMode;
    bool callbackFinished;
    bool cadRx;
    bool txAck;
} CommsFlags_t;

/**
 * @brief Configuration settings
 */
typedef struct {
    uint32_t RF_F; // Hz
    uint32_t sleepTime
} CommsSettings_t;

/*!
 * \brief Function to initialize the Radio and its parameters
 */

CommsState_t Startup(void);

// EXPLANATION

CommsState_t Sleep(void);

/*!
 * \brief Function to be executed on Radio Rx Done event
 */
void OnRxDone( uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr ); 

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


// Description to do
bool IsCallbackFinished(void);

#endif /* COMMS_INTERNAL_H_ */
