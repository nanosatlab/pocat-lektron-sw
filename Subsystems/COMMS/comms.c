/*
 * comms.c
 *  Created on: Jul 10, 2024
 *      Author: Óscar Pavón
 *  Previous work by Artur Cot, Julia Tribó & Daniel Herencia
 */

#include <clock.h>
#include "comms.h"
//#include "stm32l4xx_hal.h" // Cambia según tu familia STM32

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
uint8_t comms_config_array[4];
uint8_t eps_config_array[3];
uint8_t pl_config_array[12];

uint8_t totalpacketsize=0;

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
int Downlink_Flag=0;

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
uint8_t DownlinkBeacon_Count = 0;
uint8_t Downlink_Index = 0;
uint8_t PL_packet_number = 0;

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
uint8_t flash_check;

//uint8_t comms_config_array[9]={SF,CR,RF_F,TX_OUTPUT_POWER,LORA_BANDWIDTH,rxTime / 100,sleepTime / 100,CADMODE_Flag,COMMS_DEBUG_MODE};
//uint8_t comms_config_array[10]={}; //Figure out how to do this


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

    	Radio.IrqProcess();		//Checks the interruptions
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
            		Radio.Send(Encoded_Packet,48);
                	vTaskDelay(pdMS_TO_TICKS(Radio.TimeOnAir(MODEM_LORA,54)));
                	COMMS_State=SLEEP;
				}

            	if (TXACK_Flag)
            	{
            		TXACK_Flag=0;
            		TxPrepare(ACK_OP);
            		vTaskDelay(pdMS_TO_TICKS(Radio.TimeOnAir(MODEM_LORA,6))); //Small delay due to GS conmutaiton time.
            		Radio.Send(Encoded_Packet,3);
                	vTaskDelay(pdMS_TO_TICKS(Radio.TimeOnAir(MODEM_LORA,6)));
                	COMMS_State=SLEEP;
            	}

            	if (Tx_PL_Data_Flag)
            	{
            		for (window_counter=1;window_counter<=packet_window;window_counter++)
            		{
						TxPrepare(DATA_OP);
						Radio.Send(Encoded_Packet,totalpacketsize);
						vTaskDelay(pdMS_TO_TICKS(Radio.TimeOnAir(MODEM_LORA,54)));
						packet_number++;
            		}
            	Tx_PL_Data_Flag=0;
            	}
            	if (TxConfig_Data_Flag)
            	{
            		TxConfig_Data_Flag=0;
            		TxPrepare(DOWNLINK_CONFIG_OP);
            		Radio.Send(Encoded_Packet,totalpacketsize);
                	vTaskDelay(pdMS_TO_TICKS(Radio.TimeOnAir(MODEM_LORA,totalpacketsize+6)));
                	COMMS_State=SLEEP;
            	}
            	if (Downlink_Flag && DownlinkBeacon_Count > 0)
            	{
            		Downlink_Flag = 0;

            		uint8_t telemetry_counter[TLCOUNTER_MAX] = {0};
            		Read_Flash(TELEMETRY_LEGACY_ADDR, telemetry_counter, TLCOUNTER_MAX);

            		uint8_t total = telemetry_counter[0];
            		uint8_t to_send = (DownlinkBeacon_Count <= total) ? DownlinkBeacon_Count : total;
            		//Si número de paquetes demandados es mayor al que hay almacenado en la Flash, se cambia el valor
            		//de DownlinkBeacon_Count al del total.

            		for (Downlink_Index = total - to_send; Downlink_Index < total; Downlink_Index++) {
            			TxPrepare(DOWNLINK_OP);
            		    Radio.Send(Encoded_Packet, 48);
            		    vTaskDelay(pdMS_TO_TICKS(Radio.TimeOnAir(MODEM_LORA, 54)));
            		}

            		COMMS_State = SLEEP;
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
            			if(!COMMS_DEBUG_MODE)
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
		store_telemetry();
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
//uint8_t tle_debug_byte = 0;
uint8_t tle_debug_array[138];

void COMMSTLCConfig(uint8_t config_data[])
	{
	 if ((10000>(config_data[3]*100)) | ((config_data[3]*100)>1000)){rxTime=config_data[3];} //Thresholded in case of error rxTime
	 if ((10000>(config_data[4]*100)) | ((config_data[4]*100)>1000)){sleepTime=config_data[4];} //Thresholded in case of error sleepTime
	 if ((0<config_data[5] && config_data[5]<127)){CADMODE_Flag=1;} //CAD Mode (On/Off)
	 if ((128<config_data[5] && config_data[5]<255)){CADMODE_Flag=0;}
	 if ((0<config_data[6] && config_data[6]<255)){packet_window=config_data[6];}

	}


void process_telecommand(uint8_t tlc_data[]) {

	//For every tlc, debug variable will be explained

	TLCReceived=tlc_data[2];
	switch (TLCReceived){

		case PING:
			TXACK_Flag=1; //ACK acts as a ping
			GoTX_Flag=1; //Forces COMMS_States = Tx
		break;
		/*OperatingMode phases: MODE_RX -> MODE_FS -> MODE_TX
		MODE_FS: The radio is in frequency synthesis mode, declared in sx126x.h
		When GoTX_Flag = 1 -> RadioTransmit() is called (declared in sx126x.c). Changes the physical state of the radio
		First turns the radio in MODE_FS to prepare the frequency,then goes to MODE_TX.*/

		case TRANSIT_TO_NM:
			currentState = _NOMINAL;
			Beacon_Flag=1;
			GoTX_Flag=1;
		break;

		case TRANSIT_TO_CM:
			currentState = _CONTINGENCY;
			Beacon_Flag=1;
			GoTX_Flag=1;
		break;

		case TRANSIT_TO_SSM:
			currentState = _SUNSAFE;
			Beacon_Flag=1;
			GoTX_Flag=1;
		break;

		case TRANSIT_TO_SM:
			currentState = _SURVIVAL;
			Beacon_Flag=1;
			GoTX_Flag=1;
		break;
		/*For case TRANSIT_TO_NM/CM/SSM/SM, Prior to the transition,
		a final beacon will be transmitted to inform about this event.*/

		case UPLOAD_ADCS_CALIBRATION:
			if(ADCS_counter == 1 && tlc_data[4]==86){
				Send_to_WFQueue(&tlc_data[4], CALIBRATION_PACKET_SIZE, MAGNETO_MATRIX_ADDR, COMMSsender);
				Send_to_WFQueue(&tlc_data[40], 3, MAGNETO_OFFSET_ADDR, COMMSsender);
				ADCS_counter++;
				Wait_ACK_Flag=1;
			}

			if(ADCS_counter == 2 && tlc_data[4]==164){
				Send_to_WFQueue(&tlc_data[4], 9, MAGNETO_OFFSET_ADDR+3, COMMSsender);
				Send_to_WFQueue(&tlc_data[13], CALIBRATION_PACKET_SIZE-12, GYRO_POLYN_ADDR, COMMSsender);
				Send_to_WFQueue(&tlc_data[37], 6, PHOTODIODES_OFFSET_ADDR, COMMSsender);
				ADCS_counter++;
				Wait_ACK_Flag=1;
			}
			if(ADCS_counter == 3 && tlc_data[4]==255){
				Send_to_WFQueue(&tlc_data[4], 18, PHOTODIODES_OFFSET_ADDR, COMMSsender);
				GoTX_Flag=1;
				Beacon_Flag=1;
				ADCS_counter=1;
			}
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

		case UPLOAD_ADCS_TLE: {

			if ((TLE_counter==1) ||(TLE_counter==2) ){ //tlc_data[2]  is ID telecommand

				Send_to_WFQueue(&tlc_data[3],TLE_PACKET_SIZE,TLE_ADDR1+(TLE_counter-1)*TLE_PACKET_SIZE,COMMSsender);
				TLE_counter++;
				memcpy(tle_debug_array, &tlc_data[3], TLE_PACKET_SIZE*2);
			}
			else if (TLE_counter==3){

				Send_to_WFQueue(&tlc_data[3],1,TLE_ADDR1+2*TLE_PACKET_SIZE,COMMSsender);
				Send_to_WFQueue(&tlc_data[4],TLE_PACKET_SIZE-1,TLE_ADDR2,COMMSsender); //2ª linia TLE, con sólo 33 bits + 1 bit que indica el final byte de la 1ª linia
				TLE_counter++;
				memcpy(tle_debug_array, &tlc_data[3], 1);
				memcpy(tle_debug_array, &tlc_data[4], TLE_PACKET_SIZE-1);

			}
			else if (TLE_counter == 4) {
				// Escribir los 35 B restantes de tlc_data[4]
				Send_to_WFQueue(&tlc_data[4], TLE_PACKET_SIZE, TLE_ADDR2 + (TLE_PACKET_SIZE - 1), COMMSsender); //34
				TLE_counter++;
				memcpy(tle_debug_array, &tlc_data[4], 34);
			}
			else if (TLE_counter == 5) { //67/69 bits
				//Se escribe los último 2 bytes de la 2ª linia
				Send_to_WFQueue(&tlc_data[4], 2,TLE_ADDR2 + (TLE_PACKET_SIZE - 1) + TLE_PACKET_SIZE,COMMSsender);
				TLE_counter = 1;
				GoTX_Flag = 1;   // ACK final
				TXACK_Flag = 1;
				memcpy(tle_debug_array, &tlc_data[4], 2);
			}
			break;
		}

		case UPLOAD_COMMS_CONFIG:{ //Transciever configuration
			COMMS_State = STDBY;

		    uint8_t output_power = tlc_data[3];
			uint8_t rf_f = tlc_data[4];
			uint8_t sf = tlc_data[5];
		    uint8_t cr  = tlc_data[6];

			if (tlc_data[3] == 10){
				Send_to_WFQueue(&tlc_data[3], 1,OUTPUT_POWER_ADDR, COMMSsender);
			}
			if (tlc_data[4] == 128){
				Send_to_WFQueue(&tlc_data[4], 1, FRF_ADDRR, COMMSsender);
			}
			if (tlc_data[5] == 11){
				Send_to_WFQueue(&tlc_data[5], 1, SF_ADDR, COMMSsender);
						}
			if (tlc_data[6] == 1){
				Send_to_WFQueue(&tlc_data[6], 1, CRC_ADDR, COMMSsender);
						}
		    TXACK_Flag = 1;

		break;}

		case COMMS_UPLOAD_PARAMS:{ // COMMS Flags/Counters/etc config.

			//COMMS_State=STDBY;
			COMMS_State=SLEEP;

		    uint8_t rx_time   = tlc_data[3];
		    uint8_t sleep_time= tlc_data[4];
		    uint8_t cad_mode  = tlc_data[5];

			Send_to_WFQueue(&tlc_data[3], 2, TIMEOUT_ADDR, COMMSsender);   // RX timeout & Sleep time
			Send_to_WFQueue(&tlc_data[5], 1, CADMODE_ADDR, COMMSsender);   // CADMODE (ON/OFF)

			if (tlc_data[5] == 128) {
			    CADMODE_Flag = 1; // ON
			} else if (tlc_data[5] == 255) {
			    CADMODE_Flag = 0; // OFF
			}
			Beacon_Flag=1;
			GoTX_Flag=1;

		break;}

		case UPLOAD_UNIX_TIME:{
		//Get UNIX timestamp (4B big-endian) from LSB-MSB to MSB-LSB
			uint8_t unixTime_bytes[4];
			unixTime_bytes[0] = tlc_data[3]; //MSB
			unixTime_bytes[1] = tlc_data[4];
			unixTime_bytes[2] = tlc_data[5];
			unixTime_bytes[3] = tlc_data[6]; // LMB

			Send_to_WFQueue(unixTime_bytes, 4, RTC_TIME_ADDR, COMMSsender);
		//UPDATE THE RTC [year:day:hour:minute:second format]????
		//responder al ground con un beacon
			Beacon_Flag=1;
			GoTX_Flag=1;
		break;}

		case UPLOAD_EPS_TH:{
			// 3 bytes for the battery thresholds of the nominal, the low and the critical states.
			uint8_t nominal_thr = tlc_data[3];
			uint8_t low_thr = tlc_data[4];
			uint8_t critical_thr = tlc_data[5];

			// Send to flash memory
			Send_to_WFQueue(&tlc_data[3], 3, EPS_TH_ADDR, COMMSsender);

			//Confirm a Ground with a beacon
			Beacon_Flag = 1;
			GoTX_Flag = 1;
		break;}

		case UPLOAD_PL_CONFIG:{
			//Which 12 P/L parameters??
			//Send_to_WFQueue((uint8_t*) tlc_data[3], 12 , RFI_CONFIG_ADDR, COMMSsender);
			Beacon_Flag = 1;
			GoTX_Flag = 1;
		  break;}

		case DOWNLINK_CONFIG:{ //19 Bytes //UPLINK?
			 // COMMS PARAM
			uint8_t rx_time   = tlc_data[3];
			uint8_t sleep_time= tlc_data[4];
			uint8_t cad_mode  = tlc_data[5];
			 // Battery threshold
			uint8_t nominal_thr = tlc_data[6];
			uint8_t low_thr = tlc_data[7];
			uint8_t critical_thr = tlc_data[8];

			 // PL config 12 bytes

			 Send_to_WFQueue(&tlc_data[3], 18, UPLINK_ADDR, COMMSsender);

			 GoTX_Flag=1;
			 TxConfig_Data_Flag=1; //Lee la flash y sustituye los nuevos parametros obtenidos por los anteriores
		  break;}
		case EPS_HEATER_ENABLE:
		  //TBD
		  break;
		case EPS_HEATER_DISABLE:
		  //TBD
		  break;
		case POL_PAYLOAD_SHUT:
		    POL_Control(POL_PAYLOAD, 0); // 0: Shut Down
		    Beacon_Flag = 1;
		    GoTX_Flag = 1;
		  break;

		case POL_ADCS_SHUT:
		    POL_Control(POL_ADCS, 1);
		    Beacon_Flag = 1;
		    GoTX_Flag = 1;
		  break;

		case POL_BURNCOMMS_SHUT:
		    POL_Control(POL_BURNCOMMS, 0);
		    Beacon_Flag = 1;
		    GoTX_Flag = 1;
		  break;

		case POL_HEATER_SHUT:
		    POL_Control(POL_HEATER, 0);
		    Beacon_Flag = 1;
		    GoTX_Flag = 1;
		  break;

		case POL_PAYLOAD_ENABLE:
		    POL_Control(POL_PAYLOAD, 1); // 1: Power On
		    Beacon_Flag = 1;
		    GoTX_Flag = 1;
		  break;

		case POL_ADCS_ENABLE:
		    POL_Control(POL_ADCS, 1);
		    Beacon_Flag = 1;
		    GoTX_Flag = 1;
		  break;

		case POL_BURNCOMMS_ENABLE:
		    POL_Control(POL_BURNCOMMS, 1);
		    Beacon_Flag = 1;
		    GoTX_Flag = 1;
		  break;

		case POL_HEATER_ENABLE:
		    POL_Control(POL_HEATER, 1);
		    Beacon_Flag = 1;
		    GoTX_Flag = 1;
		  break;

		case CLEAR_PL_DATA:
		//Erase every page reserved for payload data
			erase_page(PL_TIME_ADDR);  // Page 95 0x0802F800
			Beacon_Flag = 1;
			GoTX_Flag   = 1;
		  break;
		case CLEAR_FLASH://The flash memory is divided in two 512 KBytes
			// Recorre todas las páginas de Bank1 y las borra de flash.c
			for (uint32_t addr = FLASH_BASE; addr < FLASH_BASE + FLASH_BANK_SIZE; addr += FLASH_PAGE_SIZE) {
			    erase_page(addr); //Borra la página donde cae 'addr'
			}
			 // Señal de confirmación al Ground Station
			Beacon_Flag = 1;
			GoTX_Flag = 1;

		  break;

		case CLEAR_HT:{ //Clear all historic telemetry stored in the flash memory.
	        for (uint32_t addr = TELEMETRY_LEGACY_ADDR; addr <= FLASH_END_ADDR; addr += FLASH_PAGE_SIZE) {
	            erase_page(addr);
	        }
	        Beacon_Flag = 1;
	        GoTX_Flag = 1;
	    }
		break;

		case COMMS_STOP_TX:
			xTimerStop(xTimerBeacon,0);
			TXStopped_Flag=1;
		break;

		case COMMS_RESUME_TX:
			xTimerStart(xTimerBeacon,0);
			TXStopped_Flag=0;
		break;

		case COMMS_IT_DOWNLINK:{
			DownlinkBeacon_Count = 1;
			Downlink_Flag = 1;
			Beacon_Flag = 1;
		break;}

		case COMMS_HT_DOWNLINK:
			DownlinkBeacon_Count = tlc_data[4];
			Downlink_Flag = 1;
			Beacon_Flag = 1;
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
			xTaskNotify( PAYLOAD_Handle, 0x1E, eSetValueWithOverwrite );
		break;

		case PAYLOAD_DEACTIVATE:
			vTaskSuspend(PAYLOAD_Handle); //Notification to OBC to disable the task
			Beacon_Flag=1;
			GoTX_Flag=1;
		break;

		case PAYLOAD_SEND_DATA:
			PL_packet_number = tlc_data[3];
			GoTX_Flag=1;
			Tx_PL_Data_Flag=1;
		break;

		case OBC_HARD_REBOOT:
		    // Borrar toda la flash (por ejemplo, todo el banco 1)
			for (uint32_t addr = FLASH_BASE; addr < FLASH_BASE + FLASH_BANK_SIZE; addr += FLASH_PAGE_SIZE) {
		        erase_page(addr);
		    }
		    // Reiniciar sistema
		    HAL_NVIC_SystemReset();  // Esto hace un reset completo del micro
		  break;
		case OBC_SOFT_REBOOT:
			HAL_NVIC_SystemReset();
			GoTX_Flag=1;
			Beacon_Flag=1;
		break;
		case OBC_PERIPH_REBOOT:
		// Reboot peripherals (UART, I2C, SPI)
			HAL_UART_DeInit(&huart4);
			HAL_UART_Init(&huart4);

		    HAL_I2C_DeInit(&hi2c1);
		    HAL_I2C_Init(&hi2c1);

		    HAL_SPI_DeInit(&hspi2);
		    HAL_SPI_Init(&hspi2);

		    Beacon_Flag = 1;         // Opcional: enviar beacon confirmando
		    GoTX_Flag = 1;
		  break;
		case OBC_DEBUG_MODE:
		// Disable automatic transmissions
		    xTimerStop(xTimerBeacon, 0);
		    TXStopped_Flag = 1;

		// Display message via serial port (umbilical)
		    printf("DEBUG MODE ACTIVATED\n");
		    printf("COMMS DISABLED. PAYLOAD DISABLED.\n");
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
	currentUnixTime = RtcGetTimerValue();
	unixTime32 = (uint32_t)currentUnixTime;

	switch(operation)
	{
		case BEACON_OP:
			totalpacketsize=48;
			plsize=41;
			memcpy(TxPacket+6,packet_to_send,sizeof(packet_to_send)); //Should be changed to the IT, test right now
		break;

		case ACK_OP:
			totalpacketsize=48;
			plsize=41;
		break;

		case DATA_OP:
			totalpacketsize=48;
			plsize=39;

			TxPacket[3] = PL_packet_number;
			Read_Flash(PHOTO_ADDR + PL_packet_number*plsize, (uint8_t *)payloadData, plsize); //Change to  payload_data_adress at some point
			memmove(TxPacket+4,payloadData,plsize); // payload starts at TxPacket[4]
			Wait_ACK_Flag=1;
		break;

		case DOWNLINK_CONFIG_OP:
			//totalpacketsize=28;
			plsize=18;
			 // COMMS PARAM
			Read_Flash(TIMEOUT_ADDR, (uint8_t *)comms_config_array, 3);
			// EPS PARAM: Battery thresholds
			Read_Flash(EPS_TH_ADDR, (uint8_t *)eps_config_array, 3);
			// PL configuration 12 bytes
			Read_Flash(RFI_CONFIG_ADDR, (uint8_t *)pl_config_array, 12);
			// +3 pq |3B Header| |18B Data|
			memmove(TxPacket+3,comms_config_array,sizeof(comms_config_array));
			memmove(TxPacket+3+sizeof(comms_config_array),eps_config_array,sizeof(eps_config_array));
			memmove(TxPacket+3+sizeof(comms_config_array)+sizeof(eps_config_array),pl_config_array,sizeof(pl_config_array));
		break;
		case DOWNLINK_OP:
			 totalpacketsize = 48;
			 plsize = BEACON_PL_LEN;
			 uint8_t telemetry_counter[TLCOUNTER_MAX] = {0};
			 Read_Flash(TELEMETRY_LEGACY_ADDR, telemetry_counter, TLCOUNTER_MAX);

			 if (telemetry_counter[0] > 0 && Downlink_Index < telemetry_counter[0]) {
				 uint32_t addr = TELEMETRY_LEGACY_ADDR + TLCOUNTER_MAX + Downlink_Index * BEACON_PL_LEN;
			    Read_Flash(addr, TxPacket + 6, BEACON_PL_LEN);
			     }
			 else {
			 memset(TxPacket + 6, 0, BEACON_PL_LEN); // En caso de error o índice fuera de rango
			     }
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

void beacon_time(){
	Beacon_Flag=1;
	COMMS_State=TX;
}

void POL_Control(POL_type type, uint8_t state) {
    GPIO_TypeDef* port;
    uint16_t pin;

    switch (type) {
        case POL_PAYLOAD:
            port = PAYLOAD_POL_GPIO_Port;
            pin = PAYLOAD_POL_Pin;
            break;
        case POL_ADCS:
            port = ADCS_POL_GPIO_Port;
            pin = ADCS_POL_Pin;
            break;
        case POL_BURNCOMMS:
            port = BURNCOMMS_POL_GPIO_Port;
            pin = BURNCOMMS_POL_Pin;
            break;
        case POL_HEATER:
            port = HEATER_POL_GPIO_Port;
            pin = HEATER_POL_Pin;
            break;
        default:
            return; // Si el subsistema no existe, salir de la función
    }

    if (state) { // If 1 -> Enable
        HAL_GPIO_WritePin(port, pin, GPIO_PIN_SET);
    }
    else { // If 0 -> Shut Down
        HAL_GPIO_WritePin(port, pin, GPIO_PIN_RESET);
    }
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

extern UART_HandleTypeDef huart4; // O huart1, según tu configuración
int _write(int file, char *ptr, int len)
{
    for (int i = 0; i < len; i++) {
        HAL_UART_Transmit(&huart4, (uint8_t *)&ptr[i], 1, HAL_MAX_DELAY);
    }
    return len;
}


