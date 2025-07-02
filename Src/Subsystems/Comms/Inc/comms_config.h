#ifndef COMMS_CONFIG_H_
#define COMMS_CONFIG_H_


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

//CAD parameters
#define CAD_SYMBOL_NUM          LORA_CAD_02_SYMBOL
#define CAD_DET_PEAK            23
#define CAD_DET_MIN             1

#endif /* COMMS_CONFIG_H_ */