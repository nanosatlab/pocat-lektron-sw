
/* ---- Includes ---- */

#include "sx126x.h"
#include "FreeRTOS.h"
#include "task.h"
#include "comms.h"
#include "radio.h"



/* ---- Macros and constants ---- */

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

//     Transmit message types 
#define ACK_M     							2
#define DATA_M  							3

//     CAD parameters
#define CAD_SYMBOL_NUM          LORA_CAD_02_SYMBOL
#define CAD_DET_PEAK            23
#define CAD_DET_MIN             1


/* ---- Type definitions ---- */

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

typedef struct {
    bool cadMode;
    bool callbackFinished;
    bool cadRx;
    bool txAck;
    bool txPayload;
} CommsFlags_t;

typedef struct {
    uint32_t RF_F; 
    uint32_t sleepTime;
    uint32_t rxTime;
    uint16_t ackTime;
} CommsSettings_t;


/* ---- Module-level variables ---- */

// COMMS State Machine starts in startup state
static CommsState_t CommsState = STARTUP; 

// Radio event handler struct
static RadioEvents_t RadioEvents;

static CommsPackets_t CommsPackets = {
    .packetWindow = 5
};

static CommsFlags_t CommsFlags = {
    .cadMode = true, // set to false in original code
    .cadRx = false,
    .callbackFinished = false,
    .txAck = false,
    .txPayload = false
};

// COMMS configuration structure inicialization
static CommsSettings_t CommsSettings = {
    .RF_F = 868000000, //NAME???
    .sleepTime = 10000, // Sleep time in milliseconds
    .rxTime = 2000,
    .ackTime = 4000
};


/* ---- Private function prototypes ---- */
void NextState(void);
void ProcessRadioCallbacks(void);
void Startup(void);
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

/* ---- Public function definitions ---- */


// Function prototypes
void setupComms(void);


// DEFINITIONS

void comms_task(void *pv_parameters)
{

    setupComms();

    for(;;) 
    {
        switch(CommsState)
        {   

            case STARTUP:
                Startup(); break; // Done // Correspondiente a Startup en cmake_comms

            case SLEEP:
                Sleep(); break; // Done // Correspondiente a una mitad de Sleep en cmake_comms.
                // Falta COMMS_DEBUG_MODE

            case RECEIVE:
                Receive(); break; // Done // Correspondiente a la otra mitad de Sleep en cmake_comms, 
                // añadiendole la recepción de ACKs que eso forma parte de RX en cmake_comms

            case TRANSMIT:
                Transmit(); break; // To do
            
            case STANDBY:
                StandBy(); break; // To do

        }

        NextState();

    }
}

void setupComms(void)
{
    // Apply the default configuration
}

void NextState(void) // CAMBIOS DE ESTADO SE HACEN AQUÍ
{
    // This function is called at the end of each state to determine the next state
    switch(CommsState)
    {
        case STARTUP:
            CommsState = SLEEP; break;

        case SLEEP:
            CommsState = RECEIVE; break;

        case TRANSMIT:
            CommsState = SLEEP; break;

        case STANDBY:
            CommsState = SLEEP; break;

        default:
            ProcessRadioCallbacks(); break; // default si el cambio de estado depende del resultado de un callback
            // en este caso el estado se cambia al final de cada callback
    }
}

// MIRAR ??
void ProcessRadioCallbacks(void) 
{
    while (!CommsFlags.callbackFinished) {
        Radio.IrqProcess();
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    CommsFlags.callbackFinished = false;
}

// Startup state
void Startup(void)
{
    BoardInitMcu();   
    RadioEvents.TxDone = OnTxDone; 
    RadioEvents.RxDone = OnRxDone; 
    RadioEvents.TxTimeout = OnTxTimeout;
    RadioEvents.RxTimeout = OnRxTimeout;
    RadioEvents.RxError = OnRxError;
    RadioEvents.CadDone = OnCadDone;
    Radio.Init(&RadioEvents);  // Initializes the Radio
    SX1262Config(11,1,CommsSettings.RF_F);   // Configures the transceiver
    SX126xConfigureCad( CAD_SYMBOL_NUM,CAD_DET_PEAK,CAD_DET_MIN,0); // Set up channel activity detection parameters
}

// Sleep state 
//    COMMS_DEBUG_MODE -> constante <- HAVE TO IMPLEMENT
void Sleep(void) 
{   
    Radio.Sleep();
    vTaskDelay(pdMS_TO_TICKS(CommsSettings.sleepTime));
}

// Receive state 
//    COMMS_DEBUG_MODE -> constante <- HAVE TO IMPLEMENT
//    HAVE TO IMPLEMENT ACK RECEPTION
void Receive(void) 
{
    if (CommsFlags.cadMode) {
        if (CommsFlags.cadRx) {
            Radio.Rx(CommsSettings.rxTime); // ** this case 
            CommsFlags.cadRx = false;
        }
        else {
            Radio.StartCad();
            vTaskDelay(pdMS_TO_TICKS(CommsSettings.rxTime));
        }
    }
    else {
        Radio.Rx(CommsSettings.rxTime);
		vTaskDelay(pdMS_TO_TICKS(CommsSettings.rxTime)); // ** and this case look the same 
    }
}

void Transmit(void) 
{
    if (CommsFlags.txAck)
    {
            //
    }
    if (CommsFlags.txPayload)
    {
            //
    }
}

// Queda UPLOAD_COMMS_CONFIG y COMMS_UPLOAD_PARAMS y OBC_SOFT_REBOOT
void StandBy(void) 
{
    // switch(CommsFlags.tlcReceived)
    // {

    //     default:
    // }
}

void OnRxDone( uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr )
{
    memset(CommsPackets.RxData,0,sizeof(CommsPackets.RxData));
    memcpy(CommsPackets.RxData, payload, size);
    Deinterleave(CommsPackets.RxData,size);

    // ??
    // RssiValue = rssi; 
    // SnrValue = snr;
    
    //??
    //RssiMoy = (((RssiMoy*RxCorrectCnt)+RssiValue)/(RxCorrectCnt+1));
    //SnrMoy = (((SnrMoy*RxCorrectCnt)+SnrValue)/(RxCorrectCnt+1));

    // ??
    //xEventGroupSetBits(xEventGroup, COMMS_RXIRQFlag_EVENT);
    if (CommsPackets.RxData[0]==0xC8 && CommsPackets.RxData[1]==0x9D)
    {
        CommsState = ProcessTelecommand();
    }
    else
    {
		// COMMSNotUs++;
		memset(CommsPackets.RxData,0,sizeof(CommsPackets.RxData));
        Radio.Standby();
		CommsState = STANDBY;
    }
}

void OnTxDone( void ) 
{
    // to do
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
    if (channelActivityDetected == true) {
        CommsFlags.cadRx = true;
        CommsState = RECEIVE; // If channel activity is detected stay in RECEIVE state
    }
    else {
        Radio.Standby(); //
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

CommsState_t ProcessTelecommand() // function processes telecommand from RxData
{
    
    telecommandId_t tlcReceived = CommsPackets.RxData[2];
    switch (tlcReceived) {

        case PING:
            CommsFlags.txAck = true; //ACK acts as a ping
            return TRANSMIT;

        case PAYLOAD_SEND_DATA: // acabar??
            // plsize=40; // PARA QUE ??
            // packetwindow=5; // D MOMENTO ES LA UNICA SITUACION I POR ESO NO SE USA
            CommsFlags.txPayload = true;
            return TRANSMIT;

        default:
            return SLEEP;
    }
}

//ack_m or OP?? simply name convention
void TxPrepare(uint8_t messageType) {
    uint32_t unixTime32 = (uint32_t) RtcGetTimerValue();
    switch(messageType)
    {
        case ACK_M:
            // look into it

        case DATA_M:
            // look into it

		    break;


    }
    CommsPackets.TxData[0] = (unixTime32 >> 24) & 0xFF;
    CommsPackets.TxData[1] = (unixTime32 >> 16) & 0xFF;
    CommsPackets.TxData[2] = (unixTime32 >> 8) & 0xFF;
    CommsPackets.TxData[3] = unixTime32 & 0xFF;
    CommsPackets.TxData[4] = 0;
    CommsPackets.TxData[5] = messageType;

    // COMENTED BECAUSE TOTALPACKETSIZE NOT DEFINED Interleave((uint8_t*) CommsPackets.TxData, totalpacketsize); // aqui he simplificado el proceso que se hacia en cmake_comms,
    // antes se hacia un memcpy de TxData a un buffer Encoded_Packet. Alomejor es necesario hacerlo asi más adelante
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

