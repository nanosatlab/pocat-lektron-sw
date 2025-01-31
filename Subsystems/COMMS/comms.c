/*
 * comms2.c
 *
 *  Created on: Jul 10, 2024
 *      Author: Óscar Pavón
 *  This is a full rework of previous COMMS
 *  Previous work by Artur Cot & Julia Tribó
 */

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

uint8_t TxPacket[48]={MISSION_ID,POCKETQUBE_ID};
uint8_t payloadData[48];
uint8_t Encoded_Packet[48];

uint8_t packet_number=1;
uint8_t packet_start=1;
uint8_t plsize=39;
uint8_t packet_window=255;
uint8_t TLCReceived=0;


uint8_t packet_to_send[48] = {MISSION_ID,POCKETQUBE_ID,BEACON}; //Test packet

/*************  FLAGS  *************/

int TLCReceived_Flag=0;
int CADRX_Flag=0;
int BedTime_Flag=0;
int Wait_ACK_Flag=0;
int TXACK_Flag=0;
int GoTX_Flag=0;
int Tx_PL_Data_Flag=0;
int Beacon_Flag=0;
int TXStopped_Flag=0;
int TxConfig_Data_Flag=0;

/***********  COUNTERS  ***********/

TimerHandle_t xTimerBeacon;

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



void COMMS_StateMachine( void )
{
	COMMS_State=STARTUP;
    /* Target board initialization*/

    BoardInitMcu();

    /* Radio initialization */
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


    for(;;)
    {
    	//COMMS_RX_OBCFlags(); // Function that checks the notifications sent by OBC to COMMS.

    	Radio.IrqProcess();     //Checks the interruptions
    	vTaskDelay(pdMS_TO_TICKS(200)); //Delay TBD

        switch(COMMS_State)
        {
            case RX:

            	if (Wait_ACK_Flag)
            	{
            		Radio.Rx(ACKTimeout);
            		Wait_ACK_Flag=0;
            	}
            	if (TLCReceived_Flag)
            	{
                	process_telecommand(RxData);
                	TLCReceived_Flag=0;
            	}
            	else
            	{
					COMMS_State=STDBY;
					BedTime_Flag=0;
            	}
                break;
            case TX:
            	if (Beacon_Flag)
				{
            		Beacon_Flag=0;
            		TxPrepare(BEACON_OP);
            		Radio.Send(packet_to_send,48);
                	vTaskDelay(pdMS_TO_TICKS(Radio.TimeOnAir(MODEM_LORA,54)));
                	COMMS_State=SLEEP;
				}

            	if (TXACK_Flag)
            	{
            		TXACK_Flag=0;
            		TxPrepare(ACK_OP);
            		Radio.Send(packet_to_send,3);
                	vTaskDelay(pdMS_TO_TICKS(Radio.TimeOnAir(MODEM_LORA,6)));
                	COMMS_State=SLEEP;
            	}

            	if (Tx_PL_Data_Flag)
            	{
            		for (window_counter=1;window_counter<=packet_window;window_counter++)
            		{

						TxPrepare(DATA_OP);
						Radio.Send(Encoded_Packet,plsize+9);
						vTaskDelay(pdMS_TO_TICKS(Radio.TimeOnAir(MODEM_LORA,54)));
						packet_number++;
            		}
            	}

            	break;
            case STDBY:
            	switch(TLCReceived)
            	{
            	case (UPLOAD_COMMS_CONFIG):
                    Radio.Standby();
            		SX1262TLCConfig(RxData);
                    COMMS_State=TX;
                    Beacon_Flag=1;
                    break;
            	case(COMMS_UPLOAD_PARAMS):
					Radio.Standby();
					COMMSTLCConfig(RxData);
					COMMS_State=TX;
                    Beacon_Flag=1;
            		break;
            	case (OBC_SOFT_REBOOT):
					Radio.Standby();
					COMMS_State=STARTUP;
            		break;
            	default:
            		COMMS_State=SLEEP;
            		break;

            	}
            	break;
            case SLEEP:
            	if (BedTime_Flag && !COMMS_DEBUG_MODE)
            	{
            		Radio.Sleep();
					vTaskDelay(pdMS_TO_TICKS(sleepTime));
					BedTime_Flag=0;
            	}
            	else
            	{
                   	if (CADMODE_Flag && CADRX_Flag)
                    	{
                    		Radio.Rx(rxTime);
                    		CADRX_Flag=0;
                    	}
                   	else if (CADMODE_Flag)
					{
						Radio.StartCad();
						vTaskDelay(pdMS_TO_TICKS(rxTime));
					}
            		else
            		{
            			if(COMMS_DEBUG_MODE)
            			{
							Radio.Rx(rxTime);
							vTaskDelay(pdMS_TO_TICKS(rxTime));
            			}
            			else
            			{
            				Radio.Rx(0);
            			}
            		}
            	BedTime_Flag=1;
            	}
            	break;
            case STARTUP:
                /* Target board initialization*/

                BoardInitMcu();

                /* Radio initialization */
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
            default:
            	COMMS_State=SLEEP;
                break;
        }
    }
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


    memset(RxData, 0, size);
    deinterleave((uint8_t*) payload,(uint8_t*) RxData);

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


void process_telecommand(uint8_t tlc_data[]) {
	TLCReceived=tlc_data[2];
	switch (TLCReceived){

		case PING:
			TXACK_Flag=1; //ACK acts as a ping
			GoTX_Flag=1;
		break;

		case TRANSIT_TO_NM:
			Beacon_Flag=1;
			GoTX_Flag=1;
		break;

		case TRANSIT_TO_CM:
			Beacon_Flag=1;
			GoTX_Flag=1;
		break;

		case TRANSIT_TO_SSM:
			Beacon_Flag=1;
			GoTX_Flag=1;
		break;

		case TRANSIT_TO_SM:
			Beacon_Flag=1;
			GoTX_Flag=1;
		break;

		case UPLOAD_ADCS_CALIBRATION:
			/* tbd
			if(ADCS_counter == 1 && tlc_data[2]==86){
				Send_to_WFQueue(&tlc_data[3], CALIBRATION_PACKET_SIZE, MAGNETO_MATRIX_ADDR, COMMSsender);
				ADCS_counter++;
				Wait_ACK_Flag=1;
			}

			else if(ADCS_counter == 2 && tlc_data[2]==164){
				Send_to_WFQueue(&tlc_data[3], 3, MAGNETO_MATRIX_ADDR + 3, COMMSsender);
				Send_to_WFQueue(&tlc_data[6], 12, MAGNETO_OFFSET_ADDR, COMMSsender);
				Send_to_WFQueue(&tlc_data[18], CALIBRATION_PACKET_SIZE-3-12, GYRO_POLYN_ADDR, COMMSsender);
				ADCS_counter++;
				Wait_ACK_Flag=1;
			}
			else if(ADCS_counter == 3 && tlc_data[2]==255){
				Send_to_WFQueue(&tlc_data[3], 6, GYRO_POLYN_ADDR + 6, COMMSsender);
				Send_to_WFQueue(&tlc_data[9], CALIBRATION_PACKET_SIZE-6-3, PHOTODIODES_OFFSET_ADDR, COMMSsender);
				//3 bytes of 0's
				GoTX_Flag=1;
				Beacon_Flag=1;
				ADCS_counter=1;
			}
			*/
		  break;

		case UPLOAD_ADCS_TLE:
		if ((TLE_counter==1 && tlc_data[2]==86) ||(TLE_counter==2 && tlc_data[2]==164) ){

			Send_to_WFQueue(&tlc_data[3],TLE_PACKET_SIZE,TLE_ADDR1+(TLE_counter-1)*TLE_PACKET_SIZE,COMMSsender);
			TLE_counter++;
			Wait_ACK_Flag=1;
		}
		else if (TLE_counter==3 && tlc_data[2]==255){

			Send_to_WFQueue(&tlc_data[3],1,TLE_ADDR1+2*TLE_PACKET_SIZE,COMMSsender);
			Send_to_WFQueue(&tlc_data[4],TLE_PACKET_SIZE-1,TLE_ADDR2,COMMSsender);
			TLE_counter=1;
			GoTX_Flag=1;
			TXACK_Flag=1;
		}
		break;


		case UPLOAD_COMMS_CONFIG: //Transciever configuration
			COMMS_State=STDBY;
		break;

		case COMMS_UPLOAD_PARAMS: // COMMS Flags/Counters/etc config.
			COMMS_State=STDBY;
		break;
		case UPLOAD_UNIX_TIME:
			//Send_to_WFQueue((uint8_t*) tlc_data[3], 4 , SET_RTC_TIME_ADDR, COMMSsender); Pending Pol implementation of new adresses, then uncomment
			Beacon_Flag=1;
			GoTX_Flag=1;
		break;
		case UPLOAD_EPS_TH:
			//Send_to_WFQueue((uint8_t*) tlc_data[3], 4 , NOMINAL_TH_ADDR, COMMSsender); Pending Pol implementation of new adresses, then uncomment
			Beacon_Flag=1;
			GoTX_Flag=1;
		  //TBD
		break;
		case UPLOAD_PL_CONFIG:
			//Send_to_WFQueue((uint8_t*) tlc_data[3], 8 , RFI_CONFIG_ADDR, COMMSsender); Pending Pol implementation of new adresses, then uncomment
		  //TBD RFI_CONFIG_ADDR
		  break;
		case DOWNLINK_CONFIG:
			plsize=19;
			GoTX_Flag=1;
			TxConfig_Data_Flag=1;
		  break;
		case EPS_HEATER_ENABLE:
		  printf("Executing: enable EPS heater TBD\n");
		  //TBD
		  break;
		case EPS_HEATER_DISABLE:
		  printf("Executing: disable EPS heater TBD\n");
		  //TBD
		  break;
		case POL_PAYLOAD_SHUT:
		  printf("Executing: shut down payload TBD\n");
		  break;

		case POL_ADCS_SHUT:
		  printf("Executing: shut down ADCS TBD\n");
		  break;

		case POL_BURNCOMMS_SHUT:
		  printf("Executing: shut down burn comms TBD\n");
		  break;

		case POL_HEATER_SHUT:
		  printf("Executing: shut down heater TBD\n");
		  break;

		case POL_PAYLOAD_ENABLE:
		  printf("Executing: enable payload TBD\n");
		  break;

		case POL_ADCS_ENABLE:
		  printf("Executing: enable ADCS TBD\n");
		  break;

		case POL_BURNCOMMS_ENABLE:
		  printf("Executing: enable burn comms TBD\n");
		  break;

		case POL_HEATER_ENABLE:
		  printf("Executing: enable heater TBD\n");
		  break;

		case CLEAR_PL_DATA:
		  //
		  break;
		case CLEAR_FLASH:
		  //
		  break;

		case CLEAR_HT:
		  //
		  break;

		case COMMS_STOP_TX:
			xTimerStop(xTimerBeacon,0);
			TXStopped_Flag=1;
		break;

		case COMMS_RESUME_TX:
			xTimerStart(xTimerBeacon,0);
			TXStopped_Flag=0;
		break;

		case COMMS_IT_DOWNLINK:
			Beacon_Flag=1;
			GoTX_Flag=1;
		break;

		case COMMS_HT_DOWNLINK:

		  //TBD
		  break;

		case PAYLOAD_SCHEDULE:
			//vTaskResume(PAYLOAD_Task); Should be a notification to OBC to enable the task
			Send_to_WFQueue(&tlc_data[3], 4, PL_TIME_ADDR, COMMSsender);
			Send_to_WFQueue(&tlc_data[7], 1, PHOTO_RESOL_ADDR, COMMSsender);
			Send_to_WFQueue(&tlc_data[8], 1, PHOTO_COMPRESSION_ADDR, COMMSsender);
			xTaskNotify(OBC_Handle, TAKEPHOTO_NOTI, eSetBits); //Notification to OBC

			Send_to_WFQueue(&tlc_data[9], 8, PL_RF_TIME_ADDR, COMMSsender);
			Send_to_WFQueue(&tlc_data[17], 1, F_MIN_ADDR, COMMSsender);
			Send_to_WFQueue(&tlc_data[18], 1, F_MAX_ADDR, COMMSsender);
			Send_to_WFQueue(&tlc_data[19], 1, DELTA_F_ADDR, COMMSsender);
			Send_to_WFQueue(&tlc_data[20], 1, INTEGRATION_TIME_ADDR, COMMSsender);
			GoTX_Flag=1;
			TXACK_Flag=1;
		break;

		case PAYLOAD_DEACTIVATE:
			//vTaskSuspend(PAYLOAD_Task); Should be a notification to OBC to disable the task
			Beacon_Flag=1;
			GoTX_Flag=1;
		break;

		case PAYLOAD_SEND_DATA:
			plsize=39;
			packetwindow=5;
			GoTX_Flag=1;
			Tx_PL_Data_Flag=1;
		break;

		case OBC_HARD_REBOOT:
		  //
		  break;
		case OBC_SOFT_REBOOT:
			HAL_NVIC_SystemReset();
			GoTX_Flag=1;
			Beacon_Flag=1;
		break;
		case OBC_PERIPH_REBOOT:
		  //TBD
		  break;
		case OBC_DEBUG_MODE:
		  //TBD
		  break;
		default:
			COMMS_State=SLEEP;
		break;
	}
	if (TXStopped_Flag)
	{
		GoTX_Flag=0;
	}
	//Now send notification to OBC
	if (GoTX_Flag)
	{
		GoTX_Flag=0;
		COMMS_State=TX;
		SX126xSetFs(); //Probably useless (15us)
	}

}


void TxPrepare(uint8_t operation){
	TimerTime_t currentUnixTime = RtcGetTimerValue();
	uint32_t unixTime32 = (uint32_t)currentUnixTime;
	switch(operation)
	{

		case BEACON_OP:
			TxPacket[2] = COMMS_IT_DOWNLINK; //should be packet ID
			break;
		case ACK_OP:
			TxPacket[2] = PAYLOAD_SEND_DATA;
			TxPacket[3] = (unixTime32 >> 24) & 0xFF;
			TxPacket[4] = (unixTime32 >> 16) & 0xFF;
			TxPacket[5] = (unixTime32 >> 8) & 0xFF;
			TxPacket[6] = unixTime32 & 0xFF;
			TxPacket[7] = 1; //should be TLC result
			TxPacket[8] = 0xFF;
			break;
		case DATA_OP:
			TxPacket[2] = PAYLOAD_SEND_DATA;
			TxPacket[3] = (unixTime32 >> 24) & 0xFF;
			TxPacket[4] = (unixTime32 >> 16) & 0xFF;
			TxPacket[5] = (unixTime32 >> 8) & 0xFF;
			TxPacket[6] = unixTime32 & 0xFF;
			TxPacket[7] = packet_number;
			TxPacket[plsize+8] = 0xFF;

			Read_Flash(PHOTO_ADDR + packet_number*plsize, (uint8_t *)payloadData, plsize); //Change to  payload_data_adress (pol( requena))
			memmove(TxPacket+8,payloadData,plsize);
			interleave((uint8_t*) TxPacket,(uint8_t*) Encoded_Packet);
			Wait_ACK_Flag=1;
			break;

		case DOWNLINK_CONFIG_OP:
			TxPacket[2] = DOWNLINK_CONFIG;

			TxPacket[3] = (unixTime32 >> 24) & 0xFF;
			TxPacket[4] = (unixTime32 >> 16) & 0xFF;
			TxPacket[5] = (unixTime32 >> 8) & 0xFF;
			TxPacket[6] = unixTime32 & 0xFF;
			TxPacket[plsize+8] = 0xFF;

			uint8_t comms_config_array[7]={SF,CR,((RF_F/86800000)!=1),LORA_BANDWIDTH,rxTime/100,CADMODE_Flag, sleepTime};
			uint8_t eps_config_array[4];
			uint8_t pl_config_array[8];

			//Read_Flash(NOMINAL_TH_ADDR, (uint8_t *)eps_config_array, 4); To be tested pol classic
			//Read_Flash(RFI_CONFIG_ADDR, (uint8_t *)pl_config_array, 8);

			memmove(TxPacket+7,comms_config_array,sizeof(comms_config_array));
			memmove(TxPacket+7+sizeof(comms_config_array),eps_config_array,sizeof(eps_config_array));
			memmove(TxPacket+7+sizeof(comms_config_array)+sizeof(eps_config_array),pl_config_array,sizeof(pl_config_array));

			break;

		default:
			Radio.Standby();
			COMMS_State=STDBY;
			memset(TxPacket,0,sizeof(TxPacket));
			memset(payloadData,0,sizeof(payloadData));
			break;
	}

}




void interleave(uint8_t *input, uint8_t *output) {
    for (int j = 0; j < 48; j++) {
        int row = j % 6;
        int column = j / 6;
        int original_index = row * 8 + column;
        output[j] = input[original_index];
    }
}

void deinterleave(uint8_t *input, uint8_t *output) {
    for (int i = 0; i < 48; i++) {
        int row = i / 8;
        int column = i % 8;
        int interleaved_index = column * 6 + row;
        output[i] = input[interleaved_index];
    }
}

void beacon_time(){
	Beacon_Flag=1;
	COMMS_State=TX;
}




void store_telemetry(){
	//Design 1: Reserve a space for the counter and flash two times
	uint8_t telemetry_data[BEACON_PL_LEN]={0};
	if (telemetry_counter[0]==0)
	{
		Read_Flash(TELEMETRY_LEGACY_ADDR,&telemetry_counter,TLCOUNTER_MAX);
		for (int i=0;i<TLCOUNTER_MAX;i++)
		{
			if (telemetry_counter[i]==0)
			{
				telemetry_counter[0]=i;
				break;
			}
		}
	}
	//Telemetry data should be polled from sensors and stored in the telemetry_data[BEACON_PL_LEN] array

	if (telemetry_counter[0]==TLCOUNTER_MAX)
	{
		erase_page(TELEMETRY_LEGACY_ADDR);
		telemetry_counter[0]=0;
	}
	telemetry_counter[0]++;
	current_telemetry_adress=(TELEMETRY_LEGACY_ADDR+telemetry_counter[0]*BEACON_PL_LEN+TLCOUNTER_MAX);
	Send_to_WFQueue(&telemetry_data,sizeof(telemetry_data),current_telemetry_adress,COMMSsender);
	Send_to_WFQueue(&telemetry_counter[0],sizeof(telemetry_counter[0]),TELEMETRY_LEGACY_ADDR+telemetry_counter[0],COMMSsender); //Design 1

	//Design 2: Flash it with every telemetry save at the cost of further reading iterations
	/*
	uint8_t telemetry_data[BEACON_PL_LEN+1]=0;
	if (telemetry_counter==0)
	{
		for(int i=0;i<TLCOUNTER_MAX;i++)
		{
			Read_Flash(TELEMETRY_LEGACY_ADDR+i*(BEACON_PL_LEN+1),&telemetry_counter,1);
			if (telemetry_counter!=0)
			{
				break;
			}

		}
	}
	telemetry_data[BEACON_PL_LEN]=telemetry_counter;
	Send_to_WFQueue(&telemetry_data,sizeof(telemetry_data),TELEMETRY_LEGACY_ADDR+telemetry_counter*(BEACON_PL_LEN+1)+TLCOUNTER_MAX,COMMSSender);
	*/
}

