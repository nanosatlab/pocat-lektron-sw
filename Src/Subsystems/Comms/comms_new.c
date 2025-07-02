#include "Subsystems/comms_new.h"
#include "radio.h"
#include "comms_internal.h"
#include "comms_config.h"

// COMMS State Machine starts in startup state
static COMMS_States COMMS_State = STARTUP; 


// Radio event handler struct
static RadioEvents_t RadioEvents;

// COMMS configuration structure inicialization
static COMMS_Settings_t COMMS_Settings = {
    .RF_F = 868000000
};


void COMMS_Task(void)
{
    for(;;) 
    {
        Radio.IrqProcess();  // Poll radio for pending interrupts and execute corresponding callbacks.
        vTaskDelay(pdMS_TO_TICKS(200)); // Delay To Be Determined
        switch(COMMS_State)
        {
            case STARTUP:
                InitializeRadio(); 
                COMMS_State = SLEEP; 
                break;

            case RX:

                // WHAT CAN I DO WITH THIS FLAG???? TLCRECEIVED_Flag!! BETTER WAY OF DOING THIS?
               if (TLCReceived_Flag) 
               {
                   process_telecommand(RxData_Comms);
                   TLCReceived_Flag=0;
               }
            
        }
    }

}

void InitializeRadio(void)
{
    BoardInitMcu();   
    // RadioEvents.TxDone = OnTxDone; 
    RadioEvents.RxDone = OnRxDone; 
    // RadioEvents.TxTimeout = OnTxTimeout;
    // RadioEvents.RxTimeout = OnRxTimeout;
    // RadioEvents.RxError = OnRxError;
    // RadioEvents.CadDone = OnCadDone;
    Radio.Init(&RadioEvents);  // Initializes the Radio
    SX1262Config(11,1,COMMS_Settings.RF_F);   // Configures the transceiver
    SX126xConfigureCad( CAD_SYMBOL_NUM,CAD_DET_PEAK,CAD_DET_MIN,0); // Set up channel activity detection parameters
}


void SX1262Config(uint8_t SF, uint8_t CR, uint32_t RF_F)
{
    /* Reads the SF, CR and time between packets variables from memory */
    /* Configuration of the LoRa frequency and TX and RX parameters */
    Radio.SetChannel(RF_F);
    Radio.SetTxConfig( MODEM_LORA, TX_OUTPUT_POWER, 0, LORA_BANDWIDTH, SF, CR,
                                    LORA_PREAMBLE_LENGTH, LORA_FIX_LENGTH_PAYLOAD_ON,
                                    true, 0, 0, LORA_IQ_INVERSION_ON, 3000 );

    Radio.SetRxConfig( MODEM_LORA, LORA_BANDWIDTH, SF, CR, 0, LORA_PREAMBLE_LENGTH,
                                    LORA_SYMBOL_TIMEOUT, LORA_FIX_LENGTH_PAYLOAD_ON,
                                    0, true, 0, 0, LORA_IQ_INVERSION_ON, true );

}

void SX126xConfigureCad(RadioLoRaCadSymbols_t cadSymbolNum, uint8_t cadDetPeak, uint8_t cadDetMin , uint32_t cadTimeout)
{   
    SX126xSetDioIrqParams( 	IRQ_CAD_DONE | IRQ_CAD_ACTIVITY_DETECTED, IRQ_CAD_DONE | IRQ_CAD_ACTIVITY_DETECTED,
                                    IRQ_RADIO_NONE, IRQ_RADIO_NONE );

    SX126xSetCadParams(cadSymbolNum, cadDetPeak, cadDetMin, LORA_CAD_RX, cadTimeout);
    //THE TOTAL CAD TIMEOUT CAN BE EQUAL TO RX TIMEOUT (IT SHALL NOT BE HIGHER THAN 4 SECONDS)
}