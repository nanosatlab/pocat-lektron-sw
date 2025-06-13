#include <clock.h>
#include "comms.h"



typedef enum                        //Possible States of the State Machine
{
	STARTUP,
    STDBY,
    RX,
    TX,
    SLEEP,
}COMMS_States;

COMMS_States COMMS_State=STARTUP;
/************  PACKETS  ************/

uint8_t RxData[48];        //Tx and Rx decoded
uint8_t TxPacket[48];

uint8_t payloadData[48];
uint8_t Encoded_Packet[48];

uint8_t packet_number=0;
uint8_t packet_start=1;
uint8_t plsize=0;
uint8_t packet_window=255;
uint8_t TLCReceived=0;

TimerTime_t currentUnixTime=0;
uint32_t unixTime32=0;


uint8_t packet_to_send[48] = {MISSION_ID,POCKETQUBE_ID,BEACON}; //Test packet
uint8_t eps_config_array[4];
uint8_t pl_config_array[8];

uint8_t totalpacketsize=0;

/*************  FLAGS  *************/

int TLCReceived_Flag=0;
int CADRX_Flag=0;
int BedTime_Flag=0;
int Wait_ACK_Flag=1; //Per fer la primera prova aixi està
int TXACK_Flag=1; //Per fer la primera prova aixi està
int GoTX_Flag=0;
int Tx_PL_Data_Flag=0;
int Beacon_Flag=0;
int TXStopped_Flag=0;
int TxConfig_Data_Flag=0;

/***********  COUNTERS  ***********/

//TimerHandle_t xTimerBeacon;

uint16_t COMMSRxErrors=0;
uint16_t COMMSRxTimeouts=0;
uint16_t COMMSNotUs=0;
uint8_t TLE_counter=1;
uint8_t ADCS_counter=1;
uint16_t window_counter=1;
uint8_t telemetry_counter[TLCOUNTER_MAX]={0};
uint32_t current_telemetry_adress=TELEMETRY_LEGACY_ADDR;

/*************  SIGNAL  *************/

int8_t RssiValue = 0;
int8_t SnrValue = 0;
int16_t RssiMoy = 0;
int8_t SnrMoy = 0;

/*************  CONFIG  *************/
int CADMODE_Flag=0;
int COMMS_DEBUG_MODE=1; // Debug mode: continuous reception, requires CADMode disabled,

uint16_t packetwindow=1; //packets
uint32_t rxTime=2000; //ms
uint16_t ACKTimeout=4000; //ms
uint32_t sleepTime=1000; //ms
uint32_t RF_F=868000000; // Hz
uint8_t SF=11;
uint8_t CR=1; // 4/5


uint8_t debugsize=0;


//uint8_t comms_config_array[8]={SF,CR,((RF_F/86800000)!=1),LORA_BANDWIDTH,rxTime/100, sleepTime/100,CADMODE_Flag,COMMS_DEBUG_MODE};

uint8_t comms_config_array[10]={}; //Flta implementar

void COMMS_StateMachine(void)
{
    COMMS_State = STARTUP; // Initialize state machine to STARTUP state

    BoardInitMcu();
    RadioInit();

    for(;;){

        switch(COMMS_State)
        {
            case STARTUP:
                BoardInitMcu();
                RadioInit();
                COMMS_State = STDBY;
                break;

            case STDBY:
                switch(TLCReceived){
                    //Gestió de paquets de configuracio, de moment no fa falta implementar
                }
            case RX:

               if (Wait_ACK_Flag) //Per la primera prova, posem la radio en mode rebre "ACKTimeout" milisegons
            	{   
            		Radio.Rx(ACKTimeout); 
            		Wait_ACK_Flag=0;
            	}
                if (TLCReceived_Flag)
            	{
                	process_telecommand(RxData);
                	TLCReceived_Flag=0;
            	}
                break;

            case TX:

                if (TXACK_Flag)
            	{
            		TXACK_Flag=0;
            		TxPrepare(ACK_OP);
            		vTaskDelay(pdMS_TO_TICKS(Radio.TimeOnAir(MODEM_LORA,6))); //Small delay due to GS conmutaiton time.
            		Radio.Send(Encoded_Packet,3);
                	vTaskDelay(pdMS_TO_TICKS(Radio.TimeOnAir(MODEM_LORA,6)));
                	COMMS_State=SLEEP;
            	}
                break;

            case SLEEP:
                Radio.Sleep();
				vTaskDelay(pdMS_TO_TICKS(sleepTime));
				BedTime_Flag=0;
                break;

            default:
                COMMS_State = SLEEP; // Fallback to sleep state
                break;
        }
    }
    
}

void RadioInit(){

    RadioEvents.TxDone = OnTxDone; // standby
    RadioEvents.RxDone = OnRxDone; // standby
    RadioEvents.TxTimeout = OnTxTimeout;
    RadioEvents.RxTimeout = OnRxTimeout;
    RadioEvents.RxError = OnRxError;
    RadioEvents.CadDone = OnCadDone;
    Radio.Init(&RadioEvents);    //Initializes the Radio
    SX1262Config(11,1,RF_F);   //Configures the transceiver
    COMMS_State = SLEEP; //Radio is already in STDBY
    SX126xConfigureCad( CAD_SYMBOL_NUM,CAD_DET_PEAK,CAD_DET_MIN,0);

}

void process_telecommand(uint8_t tlc_data[]) {
	TLCReceived=tlc_data[2];
	switch (TLCReceived){

		case PING:
			TXACK_Flag=1; //ACK acts as a ping
			GoTX_Flag=1;
		break;
    }
}

void TxPrepare(uint8_t operation){
	currentUnixTime = RtcGetTimerValue();
	unixTime32 = (uint32_t)currentUnixTime;

	switch(operation)
	{
		case ACK_OP:
			totalpacketsize=48;
			plsize=41;
		break;

		default:
			Radio.Standby();
			COMMS_State=STDBY;
			memset(payloadData,0,sizeof(payloadData));
		break;
	}
	TxPacket[0] = (unixTime32 >> 24) & 0xFF;
	TxPacket[1] = (unixTime32 >> 16) & 0xFF;
	TxPacket[2] = (unixTime32 >> 8) & 0xFF;
	TxPacket[3] = unixTime32 & 0xFF;
	TxPacket[4] = 0;
	TxPacket[5] = operation;

    uint8_t *TxData =(uint8_t *) malloc(totalpacketsize);
    if (TxData == NULL) {
        exit(EXIT_FAILURE);
    }

    memcpy(TxData,TxPacket,totalpacketsize);
	interleave((uint8_t*) TxData, totalpacketsize);
	memcpy(Encoded_Packet,TxData,totalpacketsize);

	free(TxData);
}


void interleave(uint8_t *inputarr, int size) {
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

void deinterleave(uint8_t *inputarr, int size) {

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
