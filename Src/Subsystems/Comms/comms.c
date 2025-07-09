#include "comms.h"
#include "comms_internal.h"
#include "radio.h"

// COMMS State Machine starts in startup state
static CommsState_t CommsState = STARTUP; 

// Radio event handler struct
static RadioEvents_t RadioEvents;

static CommsPackets_t CommsPackets;

static CommsPackets_t Packets = {
    .TlcReceived = 0
};

static CommsFlags_t CommsFlags = {
    .tlcReceived = false,
    .cadMode = false,
    .cadRx = false,
    .callbackFinished = false,
    .txAck = false
};

// COMMS configuration structure inicialization
static CommsSettings_t Comms_Settings = {
    .RF_F = 868000000, //NAME???
    .sleepTime = 10000, // Sleep time in milliseconds
};

void CommsTask(void)
{
    for(;;) 
    {
        switch(CommsState)
        {   

            case STARTUP:
                CommsState = Startup(); break;

            case PROCESS:
                CommsState = Process(); break;

            case RECEIVE:
                CommsState = Receive(); break;

            case TRANSMIT:
                CommsState = Transmit(); break;

            case SLEEP:
                CommsState = Sleep(); break;
            
            case STANDBY:
                CommsState = StandBy(); break;

        }
        RadioIrqProcess();  // Poll radio for pending interrupts and execute corresponding callbacks.
    }

}

// Startup state
CommsState_t Startup(void)
{
    BoardInitMcu();   
    RadioEvents.TxDone = OnTxDone; 
    RadioEvents.RxDone = OnRxDone; 
    RadioEvents.TxTimeout = OnTxTimeout;
    RadioEvents.RxTimeout = OnRxTimeout;
    RadioEvents.RxError = OnRxError;
    RadioEvents.CadDone = OnCadDone;
    Radio.Init(&RadioEvents);  // Initializes the Radio
    SX1262Config(11,1,COMMS_Settings.RF_F);   // Configures the transceiver
    SX126xConfigureCad( CAD_SYMBOL_NUM,CAD_DET_PEAK,CAD_DET_MIN,0); // Set up channel activity detection parameters
    return SLEEP; // After initialization, the state machine goes to sleep
}

CommsState_t Process(void) 
{
    // ?? no acabo de entender el flujo de este estado
    // if (Wait_ACK_Flag)
    // {
    //     Radio.Rx(ACKTimeout);
    //     Wait_ACK_Flag=0;
    // }
    
    if (CommsFlags.tlcReceived)
    {
        processTelecommand();
        CommsFlags.tlcReceived = false;
    }
    else
    {
        // ????
        COMMS_State = STANDBY;
        // BedTime_Flag=0;, asumo que esto no hace falta de momento
    }
    // return??
} 

CommsState_t Receive(void) 
{
    if (CommsFlags.cadMode) {
        if (CommsFlags.cadRx) {
            Radio.Rx(rxTime);
            CommsFlags.cadRx = false;
        }
        else {
            Radio.StartCad();
            vTaskDelay(pdMS_TO_TICKS(rxTime));
        }
    }
    return SLEEP;
    // ??  else <- aqui queda algo que no entiendo... 
}

CommsState_t Transmit(void) 
{
    if (CommsFlags.txAck)
    {
        CommsFlags.txAck = false;
        TxPrepare(ACK_OP); //prepares TxData
        Radio.Send(Packets.TxData,3);
        vTaskDelay(pdMS_TO_TICKS(Radio.TimeOnAir(MODEM_LORA,6)));
        return SLEEP;
    }
}
// Sleep state 
//    COMMS_DEBUG_MODE -> constante <- HAVE TO IMPLEMENT
CommsState_t Sleep(void) 
{
    Radio.Sleep();
    vTaskDelay(pdMS_TO_TICKS(COMMS_Settings.sleepTime));
    return RECEIVE;
}

// Queda UPLOAD_COMMS_CONFIG y COMMS_UPLOAD_PARAMS
CommsState_t StandBy(void) 
{
    switch(CommsFlags.tlcReceived)
    {
        case (OBC_SOFT_REBOOT):
            Radio.Standby();
            return STARTUP;

        default:
            return SLEEP;
    }
}

void OnRxDone( uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr )
{
    memset(Packets.RxData,0,sizeof(Packets.RxData));
    memcpy(Packets.RxData, payload, size);
    Deinterleave(Packets.RxData,size);

    // ??
    // RssiValue = rssi; 
    // SnrValue = snr;
    
    //??
    //RssiMoy = (((RssiMoy*RxCorrectCnt)+RssiValue)/(RxCorrectCnt+1));
    //SnrMoy = (((SnrMoy*RxCorrectCnt)+SnrValue)/(RxCorrectCnt+1));

    // ??
    //xEventGroupSetBits(xEventGroup, COMMS_RXIRQFlag_EVENT);

    // what is 0xC8 and 0x9D ?? 
    if (Packets.RxData[0]==0xC8 && Packets.RxData[1]==0x9D)
    {
        CommsFlags.tlcReceived = true;
        CommsState = PROCESS;
    }
    else // ?? , no entiendo esta transicion
    {
        memset(Packets.RxData,0,sizeof(Packets.RxData));
        Radio.Standby();
        CommsState = STANDBY;
    }
}

void OnTxDone( void ) 
{
    if (packet_number==packet_window)
    {
        Tx_PL_Data_Flag=0;
        CommsState = RECEIVE;
    }
}

// COMMSRxErrors
void OnRxError( void )
{
    Radio.Standby();
    CommsState = RECEIVE;
}

// COMMSRxTimeouts
// ADCS_counter = 1;
// TLE_counter = 1;
void OnRxTimeout( void)
{
    CommsState = SLEEP;
}

void OnTxTimeout( void )
{
    Radio.Standby();
    CommsState = STANDBY;
}

void OnCadDone( bool channelActivityDetected)
{
    if(channelActivityDetected == true)
    {
        CommsState = SLEEP;
        CommsFlags.cadRx = true;
        // BedTime_Flag=0; // ?????
    }
    else
    {
        Radio.Standby();
        CommsState = SLEEP;
    }
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

void RadioIrqProcess(void) 
{
    while (!CommsFlags.callbackFinished) {
        Radio.IrqProcess();
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    CommsFlags.callbackFinished = false;
}

bool CallbackFinished(void) { CommsFlags.callbackFinished = true; } // set flag to indicate callback is finished

void ProcessTelecommand() // function processes telecommand from RxData
{
    switch (Packets.TlcReceived){

        case PING:
        CommsFlags.txAck = true; //ACK acts as a ping
        GoTX_Flag=1;
        break;
    }
}

void TxPrepare(uint8_t operation){
    TimerTime_t currentUnixTime = RtcGetTimerValue();
    uint32_t unixTime32 = (uint32_t)currentUnixTime;

    switch(operation)
    {
        case ACK_OP:
            totalpacketsize=48;
            plsize=41;
    }
    Packets.TxData[0] = (unixTime32 >> 24) & 0xFF;
    Packets.TxData[1] = (unixTime32 >> 16) & 0xFF;
    Packets.TxData[2] = (unixTime32 >> 8) & 0xFF;
    Packets.TxData[3] = unixTime32 & 0xFF;
    Packets.TxData[4] = 0;
    Packets.TxData[5] = operation;

    Interleave((uint8_t*) Packets.TxData, totalpacketsize);
}

void Interleave(uint8_t *inputarr, int size) 
{
    // Check that the size is a multiple of 6.
    if (size % 6 != 0) {
        return;
    }

    int groupSize = size / 6;

    // Allocate temporary array to hold the interleaved result.
    uint8_t *temp =(uint8_t *) malloc(size * sizeof(uint8_t));
    if (temp == NULL) {
        exit(EXIT_FAILURE);
    }

    // For each index within the groups,
    // pick one element from each of the 6 groups.
    for (int i = 0; i < groupSize; i++) {
        for (int j = 0; j < 6; j++) {
            temp[i * 6 + j] = inputarr[j * groupSize + i];
        }
    }

    // Copy the interleaved elements back into the original array.
    memcpy(inputarr, temp, size);

    free(temp);
}

void Deinterleave(uint8_t *inputarr, int size) 
{
    if (size % 6 != 0) {
        return;
    }

    int groupSize = size / 6;
    uint8_t *temp =(uint8_t *) malloc(size * sizeof(uint8_t));
    if (temp == NULL) {
        exit(EXIT_FAILURE);
    }

    // Reconstruct the original groups.
    // For each group index i and for each group j:
    // The interleaved array holds the jth element of group j at position i*6 + j.
    // We restore it to temp[ j * groupSize + i ].
    for (int i = 0; i < groupSize; i++) {
        for (int j = 0; j < 6; j++) {
            temp[j * groupSize + i] = inputarr[i * 6 + j];
        }
    }

    memcpy(inputarr, temp, size);
    free(temp);
}

