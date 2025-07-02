// //#include <clock.h>
// #include "comms.h"

// typedef enum                        //Possible States of the State Machine
// {
// 	STARTUP,
//     STDBY,
//     RX,
//     TX,
//     SLEEP,
// } COMMS_States;

// COMMS_States COMMS_State=STARTUP;
// /************  PACKETS  ************/

// uint8_t RxData[48];        //Tx and Rx decoded
// uint8_t TxPacket[48];

// uint8_t payloadData[48];
// uint8_t Encoded_Packet[48];

// uint8_t packet_number=1;
// //uint8_t packet_start=1;
// uint8_t plsize=0;   
// uint8_t packet_window=0;
// uint8_t TLCReceived=0;

// TimerTime_t currentUnixTime=0;
// uint32_t unixTime32=0;


// //uint8_t packet_to_send[48] = {MISSION_ID,POCKETQUBE_ID,BEACON}; //Test packet
// //uint8_t eps_config_array[4];
// //uint8_t pl_config_array[8];

// uint8_t totalpacketsize=0;

// /*************  FLAGS  *************/

// int TLCReceived_Flag=0;
// int CADRX_Flag=0;
// int BedTime_Flag=0;
// int Wait_ACK_Flag=1; //Per fer la primera prova aixi està
// int TXACK_Flag=1; //Per fer la primera prova aixi està
// int GoTX_Flag=0;
// int Tx_PL_Data_Flag=0;
// int Beacon_Flag=0;
// int TXStopped_Flag=0;
// //int TxConfig_Data_Flag=0;

// /***********  COUNTERS  ***********/

// //TimerHandle_t xTimerBeacon;

// uint16_t COMMSRxErrors=0;
// uint16_t COMMSRxTimeouts=0;
// uint16_t COMMSNotUs=0;
// uint8_t TLE_counter=1;
// uint8_t ADCS_counter=1;
// //uint16_t window_counter=1;
// //uint8_t telemetry_counter[TLCOUNTER_MAX]={0};
// //uint32_t current_telemetry_adress=TELEMETRY_LEGACY_ADDR;

// /*************  SIGNAL  *************/

// int8_t RssiValue = 0;
// int8_t SnrValue = 0;
// int16_t RssiMoy = 0;
// int8_t SnrMoy = 0;

// /*************  CONFIG  *************/
// int CADMODE_Flag=0;
// int COMMS_DEBUG_MODE=1; // Debug mode: continuous reception, requires CADMode disabled,

// uint16_t packetwindow=1; //packets
// uint32_t rxTime=2000; //ms
// uint16_t ACKTimeout=4000; //ms
// uint32_t sleepTime=1000; //ms
// uint32_t RF_F=868000000; // Hz
// uint8_t SF=11;
// uint8_t CR=1; // 4/5


// uint8_t debugsize=0;


// //uint8_t comms_config_array[8]={SF,CR,((RF_F/86800000)!=1),LORA_BANDWIDTH,rxTime/100, sleepTime/100,CADMODE_Flag,COMMS_DEBUG_MODE};

// uint8_t comms_config_array[10]={}; //Falta implementar

void COMMS_StateMachine(void)
{
    COMMS_State = STARTUP; // Initialize state machine to STARTUP state

    BoardInitMcu();
    StartRadio();

    for(;;){

        COMMS_RX_OBCFlags(); //Aquest funcio s'utilitza per comprovar si hi ha algun flag de l'OBC que s'hagi de processar

        switch(COMMS_State)
        {
            case STARTUP:
                BoardInitMcu();
                StartRadio();
                COMMS_State = RX; // Per fer testos de rebre el ack
                break;

            case STDBY:
                // switch(TLCReceived){
                //     //Gestió de paquets de configuracio, de moment no fa falta implementar
                // }
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

void process_telecommand(uint8_t tlc_data[]) {
	TLCReceived=tlc_data[2];
	switch (TLCReceived){

		case PING:
			TXACK_Flag=1;
			GoTX_Flag=1;
		break;
    }

    if (TXStopped_Flag)
	{
		GoTX_Flag=0;
	}
	
	if (GoTX_Flag)
	{
		GoTX_Flag=0;
		COMMS_State=TX;
    }
}

void COMMS_RX_OBCFlags() {

    uint32_t notifValue;

    // if (xTaskNotifyWait(0, 0xFFFFFFFF, &notifValue, 0) == pdTRUE) { //Aqui agafem el valor de la notificació
    //     if (notifValue & ANTENNA_DEPLOYMENT_NOTI) { //Aquest es un exemple de com es gestionaria
    //         Beacon_Flag = 1;
    //         GoTX_Flag = 1;
    //         COMMS_State = TX;
    //     }
    // }
}



//FUNCIONS DE CODIFICACIÓ-----------------------------------------------------------------------------------------------------

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

//FUNCIONS DE CONFIGURACIÓ-----------------------------------------------------------------------------------------------------


void SX126xConfigureCad(RadioLoRaCadSymbols_t cadSymbolNum, uint8_t cadDetPeak, uint8_t cadDetMin , uint32_t cadTimeout)
{
    SX126xSetDioIrqParams( 	IRQ_CAD_DONE | IRQ_CAD_ACTIVITY_DETECTED, IRQ_CAD_DONE | IRQ_CAD_ACTIVITY_DETECTED,
                            IRQ_RADIO_NONE, IRQ_RADIO_NONE );
    SX126xSetCadParams(cadSymbolNum, cadDetPeak, cadDetMin, LORA_CAD_RX, cadTimeout);
    //THE TOTAL CAD TIMEOUT CAN BE EQUAL TO RX TIMEOUT (IT SHALL NOT BE HIGHER THAN 4 SECONDS)
}

void SX1262Config(uint8_t SF,uint8_t CR ,uint32_t RF_F){

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

void SX1262TLCConfig(uint8_t config_data[])
	{
    if (config_data[4]==RF_ID1 && (RF_F/86800000)!=1) //128
    {
    	RF_F=86800000;
    	Radio.SetChannel(RF_F);
    }
    else if (config_data[4]==RF_ID2  && (RF_F/91500000)!=1) //255
    {
    	RF_F=91500000;
    	Radio.SetChannel(RF_F);
    }

    if ((10<=config_data[5]) | (config_data[5]<=14))
    {
    	SF=config_data[5]; //TBD
    }

    if ((1<=config_data[6]) | (config_data[6]<=4))
    {
    	CR=config_data[6]; //TBD
    }

    Radio.SetTxConfig( MODEM_LORA, TX_OUTPUT_POWER, 0, LORA_BANDWIDTH, SF, CR,
                                       LORA_PREAMBLE_LENGTH, LORA_FIX_LENGTH_PAYLOAD_ON,
                                       true, 0, 0, LORA_IQ_INVERSION_ON, 3000 );

    Radio.SetRxConfig( MODEM_LORA, LORA_BANDWIDTH, SF, CR, 0, LORA_PREAMBLE_LENGTH,
                                       LORA_SYMBOL_TIMEOUT, LORA_FIX_LENGTH_PAYLOAD_ON,
                                       0, true, 0, 0, LORA_IQ_INVERSION_ON, true );

	}

void COMMSTLCConfig(uint8_t config_data[])
	{
	 if ((10000>(config_data[3]*100)) | ((config_data[3]*100)>1000)){rxTime=config_data[3];} //Thresholded in case of error rxTime
	 if ((10000>(config_data[4]*100)) | ((config_data[4]*100)>1000)){sleepTime=config_data[4];} //Thresholded in case of error sleepTime
	 if ((0<config_data[5] && config_data[5]<127)){CADMODE_Flag=1;} //CAD Mode (On/Off)
	 if ((128<config_data[5] && config_data[5]<255)){CADMODE_Flag=0;}
	 if ((0<config_data[6] && config_data[6]<255)){packet_window=config_data[6];}

	}

void StartRadio(){

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

//FUNCIONS DE VARIABLES DE CONFIGURACIÓ-----------------------------------------------------------------------------------------------------

void OnRxError()
{
    Radio.Standby();
    COMMS_State = RX;
    COMMSRxErrors++;
}

void OnRxTimeout( )
{
    COMMS_State = SLEEP;
    COMMSRxTimeouts++;
    ADCS_counter=1;
    TLE_counter=1;
}

void OnTxTimeout()
{
    Radio.Standby();
    COMMS_State = STDBY;
}

void OnCadDone(bool channelActivityDetected)
{
    if(channelActivityDetected == true)
    {
        COMMS_State=SLEEP;
        CADRX_Flag=1;
        BedTime_Flag=0;
    }
    else
    {
        Radio.Standby();
        COMMS_State=SLEEP;
    }
}


void OnTxDone()
{
	if (packet_number==packet_window)
	{
		Tx_PL_Data_Flag=0;
		COMMS_State=RX;
	}
}

void OnRxDone( uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr )
{

	memset(RxData,0,sizeof(RxData));
    uint8_t *RxPacket =(uint8_t *) malloc(size);
    if (RxPacket == NULL) {
        exit(EXIT_FAILURE);
    }

    memcpy(RxPacket,payload,size);

    deinterleave((uint8_t*) RxPacket,size);
    memcpy(RxData,RxPacket,size);

    free(RxPacket);

    RssiValue = rssi;
    SnrValue = snr;



    //RssiMoy = (((RssiMoy*RxCorrectCnt)+RssiValue)/(RxCorrectCnt+1));
    //SnrMoy = (((SnrMoy*RxCorrectCnt)+SnrValue)/(RxCorrectCnt+1));

    //xEventGroupSetBits(xEventGroup, COMMS_RXIRQFlag_EVENT);

    if (RxData[0]==0xC8 && RxData[1]==0x9D)
    {
        TLCReceived_Flag=1;
        COMMS_State=RX;
    }
    else
    {
		memset(RxData,0,sizeof(RxData));
		COMMS_State=STDBY;
	    Radio.Standby();
		COMMSNotUs++;
    }

}