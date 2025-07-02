#ifndef COMMS_INTERNAL_H_
#define COMMS_INTERNAL_H_

#include "sx126x.h"

/**
 * @brief Possible states of the communications state machine.
 */
typedef enum {
    STARTUP,
    STDBY,
    RX,
    TX,
    SLEEP
} COMMS_States;

/**
 * @brief Configuration parameters.
 */
typedef struct {
    uint32_t RF_F; // Hz
} COMMS_Settings_t;


/*!
 * \brief Function to initialize the Radio and its parameters
 */

void InitializeRadio(void);

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


#endif /* COMMS_INTERNAL_H_ */
