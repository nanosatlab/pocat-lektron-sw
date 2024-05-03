/*
 * comms.c
 *
 *  Created on: 23 feb. 2022
 *      Author: Daniel Herencia Ruiz and Júlia Tribó
 *
 * \code
 *
 *    _______    ______    ____    ____    ____    ____     ______
 *   / ______)  /  __  \  |    \  /    |  |    \  /    |   / _____)
 *  / /         | |  | |  |  \  \/  /  |  |  \  \/  /  |  ( (____
 * ( (          | |  | |  |  |\    /|  |  |  |\    /|  |   \____ \
 *  \ \______   | |__| |  |  | \__/ |  |  |  | \__/ |  |   _____) )
 *   \_______)  \______/  |__|      |__|  |__|      |__|  (______/
 *
 *
 * \endcode
 */


#include <clock.h>
#include "comms.h"
#include "main.h"


/************  STATES  *************/

typedef enum                        //Possible States of the State Machine
{
    LOWPOWER,
    RX,
    RX_TIMEOUT,
    RX_ERROR,
    TX,
    TX_TIMEOUT,
    START_CAD,
}States_t;

typedef enum                        //CAD states
{
    CAD_FAIL,
    CAD_SUCCESS,
    PENDING,
}CadRx_t;

States_t State = LOWPOWER;          //Current state of the State Machine

/************  PACKETS  ************/

uint8_t Buffer[BUFFER_SIZE];        //Tx and Rx decoded
uint16_t BufferSize;				//decoded size
uint8_t decoded[BUFFER_SIZE];			//decodeded mes
uint8_t decoded_size;
uint8_t nack[WINDOW_SIZE];          //To store the last ack/nack received
uint8_t nack_size;
uint8_t last_telecommand[BUFFER_SIZE]; //Last telecommand RX


/*************  FLAGS  *************/

uint8_t error_telecommand = false;  //To transmit an error packet
uint8_t tx_flag = false;            //To allow transmission
uint8_t send_data = false;          //To send data packets to the GS
uint8_t beacon_flag = false;        //To transmit the beacon
uint8_t nack_flag = false;          //Retransmission necessary
uint8_t tle_telecommand = false;    //True when TLE telecommand received
uint8_t calibration_telecommand = false;    //True when calibration telecommand received
uint8_t telecommand_rx = false;     //To indicate that a telecommand has been received
uint8_t request_execution = false;  //To send the request execution packet
uint8_t protocol_timeout = false;   //True when the protocol timer ends
uint8_t reception_ack_mode = false; //True
uint8_t contingency = false;        //True if we are in contingency state (only RX allowed)
uint8_t isGS = true;				//True if telecommand is sent by GS, false if telecommand is sent by another sat



/***********  COUNTERS  ***********/

uint8_t request_counter = 0;        //Number of request execution packets sent (to execute a telecommand order)
uint8_t packet_number = 0;          //Data packet number
uint8_t num_config = 0;             //Configuration packet number
uint8_t num_telemetry = 0;          //Telemetry packet number
uint8_t window_packet = 0;          //TX window number
uint8_t nack_counter = 0;               //Position of the NACK array (packets already retransmitted)
uint8_t count=0;                        //Counter for loops
uint8_t protocol_timeout_counter = 0; //Number of times that the protocol timer (500 ms) has ended
                                      //This is used to have longer timers for higher SF
uint8_t tle_counter = 0;			//counts the number of tle packets received
uint8_t calibration_counter=0;		//counts the number of tle packets received

/********  LoRa PARAMETERS  ********/
uint8_t SF = LORA_SPREADING_FACTOR; //Spreading Factor
uint8_t CR = LORA_CODINGRATE;       //Coding Rate
uint16_t time_packets = 500;        //Time between data packets sent in ms


/*************  OTHER  *************/

int8_t RssiValue = 0;
int8_t SnrValue = 0;
CadRx_t CadRx = CAD_FAIL;
bool PacketReceived = false;
bool RxTimeoutTimerIrqFlag = false;
uint16_t channelActivityDetectedCnt = 0;
uint16_t RxCorrectCnt = 0;
uint16_t RxErrorCnt = 0;
uint16_t RxTimeoutCnt = 0;
uint16_t SymbTimeoutCnt = 0;
int16_t RssiMoy = 0;
int8_t SnrMoy = 0;
correct_convolutional *conv;
uint8_t byte_to_compare = 0xFF;

/*************************************************************************
 *                                                                       *
 *  Function:  CADTimeoutTimeoutIrq                                      *
 *  --------------------                                                 *
 *  Function called automatically when a CAD IRQ occurs                  *
 *                                                                       *
 *  returns: nothing                                                     *
 *                                                                       *
 *************************************************************************/
void CADTimeoutTimeoutIrq(void)
{
    Radio.Standby();
    //if (request_execution){
    //	State = TX;
    //} else{
	State = LOWPOWER;
    //}
    //State = START_CAD;
    //State = RX;
}

/*************************************************************************
 *                                                                       *
 *  Function:  RxTimeoutTimerIrq                                         *
 *  --------------------                                                 *
 *  Function called automatically when a Timeout interruption occurs     *
 *                                                                       *
 *  returns: nothing                                                     *
 *                                                                       *
 *************************************************************************/
void RxTimeoutTimerIrq(void)
{
    RxTimeoutTimerIrqFlag = true;
}

/*************************************************************************
 *                                                                       *
 *  Function:  configuration                                             *
 *  --------------------                                                 *
 *  function to configure the transceiver and the protocol parameters    *
 *                                                                       *
 *  returns: nothing                                                     *
 *                                                                       *
 *************************************************************************/
void configuration(void){

	//uint64_t read_variable; //Read flash function requieres variables of 64 bits

	/* Reads the SF, CR and time between packets variables from memory */
	SF = LORA_SPREADING_FACTOR;
	CR = LORA_CODINGRATE;
	time_packets = 500;

	/* Configuration of the LoRa frequency and TX and RX parameters */
    Radio.SetChannel( RF_FREQUENCY );

    Radio.SetTxConfig( MODEM_LORA, TX_OUTPUT_POWER, 0, LORA_BANDWIDTH, SF, CR,
                                   LORA_PREAMBLE_LENGTH, LORA_FIX_LENGTH_PAYLOAD_ON,
                                   true, 0, 0, LORA_IQ_INVERSION_ON, 3000 );

    Radio.SetRxConfig( MODEM_LORA, LORA_BANDWIDTH, SF, CR, 0, LORA_PREAMBLE_LENGTH,
                                   LORA_SYMBOL_TIMEOUT, LORA_FIX_LENGTH_PAYLOAD_ON,
                                   0, true, 0, 0, LORA_IQ_INVERSION_ON, true );

    /* Configuration of the CAD parameters */
    SX126xConfigureCad( CAD_SYMBOL_NUM,CAD_DET_PEAK,CAD_DET_MIN,CAD_TIMEOUT_MS);
    Radio.StartCad( );      //To initialize the CAD process

    conv = correct_convolutional_create(RATE_CON, ORDER_CON, correct_conv_r12_7_polynomial);


    State = RX;             //To initialize in RX state
};

void COMMS_RX_OBCFlags()
{
	uint32_t RX_COMMS_NOTIS;

	if (xTaskNotifyWait(0, 0xFFFFFFFF, &RX_COMMS_NOTIS, 0)==pdPASS)
	{
		if((RX_COMMS_NOTIS & WAKEUP_NOTI)==WAKEUP_NOTI)
		{
			// SEND WAKEUP NOTI TO GS
		}

		if((RX_COMMS_NOTIS & SUNSAFE_NOTI)==SUNSAFE_NOTI)
		{
			// SEND SUNSAFE NOTI TO GS
		}

		if((RX_COMMS_NOTIS & CONTINGENCY_NOTI)==CONTINGENCY_NOTI)
		{
			// SEND CONTINGENCY NOTI TO GS
		}

		xEventGroupSetBits(xEventGroup, COMMS_RXNOTI_EVENT);
	}
}

/*************************************************************************
 *                                                                       *
 *  Function:  COMMS_StateMachine                                              *
 *  --------------------                                                 *
 *  communication process state machine                                  *
 *  States:                                                              *
 *  - RX_TIMEOUT: when the reception ends                                *
 *  - RX_ERROR: when an error in the reception process occurs            *
 *  - RX: when a packet has been received or to start a RX process       *
 *  - TX: to transmit a packet                                           *
 *  - TX_TIMEOUT: when the transmission ends                             *
 *  - LOWPOWER: when the transceiver is not transmitting nor receiving   *
 *                                                                       *
 *  returns: nothing                                                     *
 *                                                                       *
 *************************************************************************/
void COMMS_StateMachine( void )
{
    /* Target board initialization*/

    BoardInitMcu();
    BoardInitPeriph();

    /* Radio initialization */
    RadioEvents.TxDone = OnTxDone; // standby
    RadioEvents.RxDone = OnRxDone; // standby
    RadioEvents.TxTimeout = OnTxTimeout;
    RadioEvents.RxTimeout = OnRxTimeout;
    RadioEvents.RxError = OnRxError;
    RadioEvents.CadDone = OnCadDone;

    /* Timer used to restart the CAD */
    TimerInit(&CADTimeoutTimer, CADTimeoutTimeoutIrq);
    TimerSetValue(&CADTimeoutTimer, CAD_TIMER_TIMEOUT);

    /* App timmer used to check the RX's end */

    TimerInit(&RxAppTimeoutTimer, RxTimeoutTimerIrq);
    TimerSetValue(&RxAppTimeoutTimer, RX_TIMER_TIMEOUT);

    Radio.Init(&RadioEvents);    //Initializes the Radio

    configuration();               //Configures the transceiver

    //EventBits_t EventBits;

    for(;;)                     //The only option to end the state machine is killing COMMS thread (by the OBC)
    {


    	COMMS_RX_OBCFlags(); // Function that checks the notifications sent by OBC to COMMS.

    	Radio.IrqProcess();       //Checks the interruptions

        switch(State)
        {
            case RX_TIMEOUT:
            {
                RxTimeoutCnt++;
                Radio.Standby();
				State = LOWPOWER;
                break;
            }
            case RX_ERROR:
            {
                RxErrorCnt++;
                PacketReceived = false;
                Radio.Standby();
				State = LOWPOWER;
            break;
            }
            case RX:
            {
                if(PacketReceived == true)
                {
                	if(Buffer[0]==0xC8)
                	{
						//DEINTERLEAVE
						unsigned char codeword_deinterleaved[127]; //127 is the maximum value it can get taking into account the tinygs maximum length is 255 and that the convolutional code adds redundant bytes
						int index = deinterleave(Buffer, BufferSize, codeword_deinterleaved);
						decoded_size = index-NPAR;
						//DECODE REED SOLOMON

						int erasures[16];
						int nerasures = 0;
						decode_data(codeword_deinterleaved, decoded_size);

						int syndrome = check_syndrome();
						/* check if syndrome is all zeros */
						if (syndrome == 0) {
							// no errs detected, codeword payload should match message
						} else {
							//nonzero syndrome, attempting correcting errors
							//result 0 not able to correct, result 1 corrected
							int __attribute__((unused)) result = correct_errors_erasures (codeword_deinterleaved,
															index,
															nerasures,
															erasures);
						}

						memcpy(decoded, codeword_deinterleaved,decoded_size);

						if (pin_correct(decoded[0], decoded[1]))
						{
							State = LOWPOWER;
							if (decoded[2] == TLE){
								Stop_timer_16();	//This is not necessary, put here for safety
								if (!tle_telecommand){	//First TLE packet
									State = RX;
									telecommand_rx = true;
									Send_to_WFQueue(&telecommand_rx, 1, COMMS_BOOL_ADDR, COMMSsender);
									if(tle_counter==3){
										tle_telecommand = true;
									}
									tle_counter++;
								}
								else{	//Is the last TLE packet (there are 2)
									tle_telecommand = false;
									State = LOWPOWER;	//Line unnecessary
									telecommand_rx = false;
									tle_counter=0;
								}
								process_telecommand(decoded[2], decoded[3]);	//Saves the TLE
							}
							if (decoded[2] == ADCS_CALIBRATION){
								Stop_timer_16();	//This is not necessary, put here for safety
								if (!calibration_telecommand){	//First TLE packet
									State = RX;
									telecommand_rx = true;
									if(calibration_counter==1){
										calibration_telecommand = true;
									}
									calibration_counter++;
								}
								else{	//Is the last TLE packet (there are 2)
									calibration_telecommand = false;
									State = LOWPOWER;	//Line unnecessary
									telecommand_rx = false;
									calibration_counter=0;
								}
								process_telecommand(decoded[2], decoded[3]);	//Saves the calibration
							}
							//else if (telecommand_rx==true){//Second telecommand RX consecutively

								//rx_attemps_counter = 0;


								//if (decoded[2] == last_telecommand[2]){	//Second telecommand received equal to the first CHANGE THIS TO CHECK THE WHOLE TELECOMMAND. USE VARIABLE compare_arrays
									//decoded[2] == (SEND_DATA || SEND_TELEMETRY || ACK_DATA || SEND_CALIBRATION || SEND_CONFIG);
									Stop_timer_16();
									if ((decoded[2] == RESET2) || (decoded[2] == EXIT_STATE) || (decoded[2] == TLE) || (decoded[2] == ADCS_CALIBRATION) || (decoded[2] == SEND_DATA) || (decoded[2] == SEND_TELEMETRY) || (decoded[2] == STOP_SENDING_DATA) || (decoded[2] == CHANGE_TIMEOUT) || decoded[2] == ACK_DATA || decoded[2] == NACK_TELEMETRY || decoded[2] == NACK_CONFIG || decoded[2] == ACTIVATE_PAYLOAD || decoded[2] == SEND_CONFIG|| decoded[2] == UPLINK_CONFIG ){
										telecommand_rx = false;
										process_telecommand(decoded[2], decoded[3]);
									}
									else {
										request_execution = true;
										State = TX;
									}
								//}

								if(decoded[2] == ACK1){	//Order execution ACK
									//rx_attemps_counter = 0;
									Stop_timer_16();
									request_counter = 0;
									request_execution = false;
									reception_ack_mode = false;
									telecommand_rx = false;
									process_telecommand(last_telecommand[2], last_telecommand[3]);
									State = RX;
								}
								/*
								else{	//Second telecommand received different from the first
									State = TX;
									telecommand_rx = false;
									error_telecommand = true;
									Stop_timer_16();
									//vTaskDelay(10);
									//manualDelayMS(10);
								}
								*/
							//}
								/*
							else{	//First telecommand RX
								memcpy(last_telecommand, decoded, BUFFER_SIZE);
								last_telecommand[0] = MISSION_ID;	//To avoid retransmitting the PIN
								last_telecommand[1] = POCKETQUBE_ID;
								tle_telecommand = false;
								telecommand_rx = true;
								State = RX;
								Start_timer_16();
							}
							*/
						}
						else{	//Pin not correct. If pin not correct it is assumed that the packet comes from another source. The protocol continues ignoring it
							State = TX;
							error_telecommand = true;
							Stop_timer_16();
							xEventGroupSetBits(xEventGroup, COMMS_WRONGPACKET_EVENT);
						}
 						PacketReceived = false;     // Reset flag
                	}
                }
                else	//If packet not received, restart reception process
                {
                    if (CadRx == CAD_SUCCESS)
                    {
                        channelActivityDetectedCnt++;   // Update counter
                        RxTimeoutTimerIrqFlag = false;
                        TimerReset(&RxAppTimeoutTimer);	// Start the Rx's's Timer

                    }
                    else
                    {
                        TimerStart(&CADTimeoutTimer);   // Start the CAD's Timer
                    }
                    Radio.Rx(RX_TIMEOUT_VALUE);	//Basic RX code
                    State = LOWPOWER;

                	if (reception_ack_mode){
                		reception_ack_mode = false;
                	}

                }
                break;
            }
            case TX:
            {
            	/* TO TEST TELECOMMANDS */
                State = LOWPOWER;
            	if (error_telecommand){	//Send error message
            		uint8_t packet_to_send[] = {MISSION_ID,POCKETQUBE_ID,ERROR};
            		Radio.Send(packet_to_send,sizeof(packet_to_send));
            		Radio.Send(packet_to_send,sizeof(packet_to_send));
                    error_telecommand = false;
            	} else if (request_execution){	//Send request for execute telecommand order

            		Radio.Send(last_telecommand,3);	//TEST ONLY SENDING ONE REQUEST (NORMALLY PACKETS ARE TX IN PAIRS
            		Radio.Send(last_telecommand,3);	//TEST ONLY SENDING ONE REQUEST (NORMALLY PACKETS ARE TX IN PAIRS
            		request_counter++;
            		reception_ack_mode = true;
            		State = RX;
            		Stop_timer_16();
            		Start_timer_16();
            	}
            	else if (tx_flag){
            		uint64_t read_photo[12];
            		uint8_t transformed[DATA_PACKET_SIZE];
            		if (window_packet < WINDOW_SIZE){
            			if (nack_flag){
            				if (nack_counter < nack_size){
            					Read_Flash(PHOTO_ADDR + nack[nack_counter]*DATA_PACKET_SIZE,  (uint8_t*)read_photo, sizeof(read_photo));
                    			decoded[3] = nack[nack_counter];	//Number of the retransmitted packet
                    			nack_counter++;

            				} else{ //When all packets have been retransmitted, we continue with the next one
            					nack_flag = false;
            					nack_counter = 0;
                    			packet_number = 0;
                    			window_packet = WINDOW_SIZE;
            				}
            			} else {
            				Read_Flash(PHOTO_ADDR + packet_number*DATA_PACKET_SIZE,  (uint8_t*)read_photo, sizeof(read_photo));
                			decoded[3] = packet_number;	//Number of the packet
                			packet_number++;

            			}


            			decoded[0] = MISSION_ID;	//Satellite ID
						decoded[1] = POCKETQUBE_ID;	//Poquetcube ID (there are at least 3)
						decoded[2] = SEND_DATA;
						memcpy(&transformed, read_photo, sizeof(transformed));
						for (uint8_t i=4; i<DATA_PACKET_SIZE+4; i++){
							decoded[i] = transformed[i-4];
						}
						TimerTime_t currentUnixTime = RtcGetTimerValue();
						uint32_t unixTime32 = (uint32_t)currentUnixTime;
						decoded[DATA_PACKET_SIZE+4] = (unixTime32 >> 24) & 0xFF;
						decoded[DATA_PACKET_SIZE+5] = (unixTime32 >> 16) & 0xFF;
						decoded[DATA_PACKET_SIZE+6] = (unixTime32 >> 8) & 0xFF;
						decoded[DATA_PACKET_SIZE+7] = unixTime32 & 0xFF;
						decoded[DATA_PACKET_SIZE+8] = 0xFF;	//Final of the packet indicator

						window_packet++;
						State = TX;

						Radio.Send( decoded, DATA_PACKET_SIZE +9);
						vTaskDelay(pdMS_TO_TICKS(3000));
						Radio.Send( decoded, DATA_PACKET_SIZE +9);

            		} else{
            			tx_flag = false;
            			packet_number = 0;
            			send_data = false;
            			window_packet = 0;
            			State = RX;
            		}

            	}

            	else if (beacon_flag){
					uint8_t packet_to_send[] = {MISSION_ID,POCKETQUBE_ID,BEACON};
					Radio.Send(packet_to_send,sizeof(packet_to_send));
					beacon_flag = false;
            	}

                break;
            }
            case TX_TIMEOUT:
            {
                State = LOWPOWER;
                break;
            }
            case LOWPOWER:
            default:
            	if (error_telecommand || tx_flag){
					State = TX;
				}
            	else if (reception_ack_mode || tle_telecommand){
            		State = RX;
            	}
            	else if (telecommand_rx==true){	//We have received at least one telecommand
            		if (request_execution ){	//In this case we have to TX request or wait for ACK
            			if (request_counter >= 3){	//If 3 request have been sent, we send an error message
            				request_execution = false;
            				error_telecommand = true;
            				telecommand_rx = false;
            				State = TX;
            				request_counter=0;
            				//rx_attemps_counter=0;
            				Stop_timer_16();
            			} else if (protocol_timeout){	//TX another request execution
            				protocol_timeout = false;
            				State = TX;
            			} else {	//Iterate till 500 ms approx
            				//rx_attemps_counter++;
            				State = RX;
            			}
            		} else{	//We want to Rx the second telecommand
            			/*Check the 160 value with the whole code and multithread, because maybe induce a delay*/
            			if (protocol_timeout){	//With this value all 2nd telecommand that arrive before 650 ms are received. 700 ms or more error packet is send
            				PacketReceived = true;
            				protocol_timeout = false;
            				Stop_timer_16();
            			}
						State = RX; //If Timeout passes and the 2nd telecommand is not received, goes to RX and will process the first, as if the second has been RX
					}
            	}
            	else if (beacon_flag){
            		State = TX;
            	}
            	else{
            		State = RX;
            	}

                // Set low power
                break;
        }


    }
}

/*************************************************************************
 *                                                                       *
 *  Function:  tx_beacon                                                 *
 *  --------------------                                                 *
 *  Activates the flags to TX the beacon                                 *
 *                                                                       *
 *  returns: nothing                                                     *
 *                                                                       *
 *************************************************************************/
void tx_beacon(void){
	if (State == RX || State == LOWPOWER){
		if (!reception_ack_mode && !tle_telecommand && !telecommand_rx){
			State = TX;
			beacon_flag = true;
		}
	}
}

/*************************************************************************
 *                                                                       *
 *  Function:  OnTxDone                                                  *
 *  --------------------                                                 *
 *  Activate the flag to indicate that the RX timeout has finished       *
 *                                                                       *
 *  returns: nothing                                                     *
 *                                                                       *
 *************************************************************************/
void comms_timmer(void){
	if (protocol_timeout_counter >= SF - 7){	//Timeout proportional to SF (high SF require more time)
		protocol_timeout = true;
		protocol_timeout_counter = 0;
	} else{
		protocol_timeout_counter = protocol_timeout_counter + 1;
	}
}

/*************************************************************************
 *                                                                       *
 *  Function:  OnTxDone                                                  *
 *  --------------------                                                 *
 *  when the transmission finish correctly                               *
 *                                                                       *
 *  returns: nothing                                                     *
 *                                                                       *
 *************************************************************************/
void OnTxDone( void )
{
    Radio.Standby( );
    if (tx_flag == 1){
        State = TX;
    } else{
        State = LOWPOWER;
    }
}

/*************************************************************************
 *                                                                       *
 *  Function:  OnRxDone                                                  *
 *  --------------------                                                 *
 *  processes the information when the reception has been done correctly *
 *  calculates the rssi and snr                                          *
 *                                                                       *
 *  payload: information received                                        *
 *  size: size of the payload                                            *
 *  rssi: rssi value                                                     *
 *  snr: snr value                                                       *
 *                                                                       *
 *  returns: nothing                                                     *
 *                                                                       *
 *************************************************************************/
void OnRxDone( uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr )
{
    Radio.Standby( );
    BufferSize = size;

    memset(Buffer, 0, BufferSize);

    memcpy(Buffer, payload, BufferSize);
    //uint8_t RXactual[BufferSize];
    //memcpy(RXactual, payload, BufferSize);
    RssiValue = rssi;
    SnrValue = snr;
    PacketReceived = true;
    RssiMoy = (((RssiMoy*RxCorrectCnt)+RssiValue)/(RxCorrectCnt+1));
    SnrMoy = (((SnrMoy*RxCorrectCnt)+SnrValue)/(RxCorrectCnt+1));
    State = RX;
    //testRX = 1;

    xEventGroupSetBits(xEventGroup, COMMS_RXIRQFlag_EVENT);
}

/*************************************************************************
 *                                                                       *
 *  Function:  OnTxTimeout                                               *
 *  --------------------                                                 *
 *  to process transmission timeout                                      *
 *                                                                       *
 *  returns: nothing                                                     *
 *                                                                       *
 *************************************************************************/
void OnTxTimeout( void )
{
    Radio.Standby( );
    State = TX_TIMEOUT;
}

/*************************************************************************
 *                                                                       *
 *  Function:  OnRxTimeout                                               *
 *  --------------------                                                 *
 *  to process reception timeout                                         *
 *                                                                       *
 *  returns: nothing                                                     *
 *                                                                       *
 *************************************************************************/
void OnRxTimeout( void )
{
    Radio.Standby();
    //State = TX;
    //error_telecommand=true;
    State = RX;
    //if (request_execution){
    	//State = TX;
    //}
    if(RxTimeoutTimerIrqFlag)
    {
        State = RX_TIMEOUT;
    }
    else
    {
        Radio.Rx(RX_TIMEOUT_VALUE);   //  Restart Rx
        SymbTimeoutCnt++;               //  if we pass here because of Symbol Timeout
        State = LOWPOWER;
    }
}

/*************************************************************************
 *                                                                       *
 *  Function:  OnRxErro                                                  *
 *  --------------------                                                 *
 *  function called when a reception error occurs                        *
 *                                                                       *
 *  returns: nothing                                                     *
 *                                                                       *
 *************************************************************************/
void OnRxError(void)
{
    Radio.Standby();
    State = RX_ERROR;
}

/*************************************************************************
 *                                                                       *
 *  Function:  OnCadDone                                                 *
 *  --------------------                                                 *
 *  Function to check if the CAD has been done correctly or not          *
 *                                                                       *
 *  channelActivityDetected: boolean that contains the CAD flat          *
 *                                                                       *
 *  returns: nothing                                                     *
 *                                                                       *
 *************************************************************************/
void OnCadDone(bool channelActivityDetected)
{
    Radio.Standby();

    if(channelActivityDetected == true)
    {
        CadRx = CAD_SUCCESS;
    }
    else
    {
        CadRx = CAD_FAIL;
    }
    State = RX;
}

/*************************************************************************
 *                                                                       *
 *  Function:  SX126xConfigureCad                                        *
 *  --------------------                                                 *
 *  Function Configure the Channel Activity Detection parameters and IRQ *
 *                                                                       *
 *  returns: nothing                                                     *
 *                                                                       *
 *************************************************************************/
void SX126xConfigureCad(RadioLoRaCadSymbols_t cadSymbolNum, uint8_t cadDetPeak, uint8_t cadDetMin , uint32_t cadTimeout)
{
    //SX126xSetDioIrqParams( 	IRQ_CAD_DONE | IRQ_CAD_ACTIVITY_DETECTED, IRQ_CAD_DONE | IRQ_CAD_ACTIVITY_DETECTED,
    //                        IRQ_RADIO_NONE, IRQ_RADIO_NONE );
    SX126xSetDioIrqParams( 	IRQ_RADIO_NONE, IRQ_CAD_DONE | IRQ_CAD_ACTIVITY_DETECTED,
                            IRQ_RADIO_NONE, IRQ_RADIO_NONE );
    //SX126xSetCadParams( cadSymbolNum, cadDetPeak, cadDetMin, LORA_CAD_ONLY, ((cadTimeout * 1000) / 15.625 ));
    SX126xSetCadParams(cadSymbolNum, cadDetPeak, cadDetMin, LORA_CAD_RX, ((cadTimeout * 15.625)/1000));
    //THE TOTAL CAD TIMEOUT CAN BE EQUAL TO RX TIMEOUT (IT SHALL NOT BE HIGHER THAN 4 SECONDS)
}

/*************************************************************************
 *                                                                       *
 *  Function:  pin_correct                                               *
 *  --------------------                                                 *
 *  check if the pin in the telecommand is correct                       *
 *                                                                       *
 *  pin_1: first byte of the pin                                         *
 *  pin_2: second byte of the pin                                        *
 *                                                                       *
 *  returns: true if correct                                             *
 *                                                                       *
 *************************************************************************/
bool pin_correct(uint8_t pin_1, uint8_t pin_2) {
	if (pin_1 == PIN1 && pin_2 == PIN2){
		isGS = true;
		return true;
	}
	else if (pin_1 == MISSION_ID && pin_2 == POQUETQUBE_ID2){
		isGS = false;
		return true;
	}
	return false;
}


/*************************************************************************
 *                                                                       *
 *  Function:  process_telecommand                                       *
 *  --------------------                                                 *
 *  processes the information contained in the packet depending on       *
 *  the telecommand received                                             *
 *                                                                       *
 *  header: number of telecomman                                         *
 *  info: information contained in the received packet                   *
 *                                                                       *
 *  returns: nothing                                                     *
 *                                                                       *
 *************************************************************************/
void process_telecommand(uint8_t header, uint8_t info) {
	//uint8_t info_write;	//Write_Flash functions requires uint64_t variables (64 bits) or arrays
	switch(header) {
	case RESET2:{
		HAL_NVIC_SystemReset();
		break;
	}

	case EXIT_STATE:{
		if(decoded[3]==0xF0){
			xTaskNotify(OBC_Handle, EXIT_CONTINGENCY_NOTI, eSetBits); //Notification to OBC
		}
		else if(decoded[3]==0x0F){
			xTaskNotify(OBC_Handle, EXIT_SUNSAFE_NOTI, eSetBits); //Notification to OBC
		}
		else if(decoded[4]==0xF0){
			xTaskNotify(OBC_Handle, EXIT_SURVIVAL_NOTI, eSetBits); //Notification to OBC
		}
		break;
	}

	case TLE:{

		if (tle_counter==1 ||tle_counter==2 ){
			Send_to_WFQueue(&decoded[3],TLE_PACKET_SIZE,TLE_ADDR1+(tle_counter-1)*TLE_PACKET_SIZE,COMMSsender);
		}
		else if (tle_counter==3){
			Send_to_WFQueue(&decoded[3],1,TLE_ADDR1+2*TLE_PACKET_SIZE,COMMSsender);
			Send_to_WFQueue(&decoded[4],TLE_PACKET_SIZE-1,TLE_ADDR2,COMMSsender);
		}
		else if(tle_counter==4||tle_counter==5){
			Send_to_WFQueue(&decoded[3],TLE_PACKET_SIZE,TLE_ADDR2+(tle_counter-3)*TLE_PACKET_SIZE-1,COMMSsender);
		}
		//xTaskNotify(OBC_Handle, TLE_NOTI, eSetBits); //Notification to OBC
		break;
		//For high SF 3 packets will be needed and the code should be adjusted
	}

	case SEND_DATA:{
		if (!contingency){
			tx_flag = true;	//Activates TX flag
			State = TX;
			send_data = true;
		}
		break;
	}
	case SEND_TELEMETRY:
	case NACK_TELEMETRY:{
		uint64_t read_telemetry[5];

		/*
		uint64_t temperature[4];
		uint64_t battery[4];
		uint64_t state[4];
		uint64_t gyroscopes[4];
		uint64_t magnetometer[4];
		uint64_t photodiodes[4];*/

		uint8_t transformed[TELEMETRY_PACKET_SIZE];	//Maybe is better to use 40 bytes, as multiple of 8
		if (!contingency){
			Read_Flash(PHOTO_ADDR, (uint8_t*)read_telemetry, sizeof(read_telemetry)); // change to TELEMETRY_ADDR
			Send_to_WFQueue((uint8_t*)read_telemetry, sizeof(read_telemetry), PHOTO_ADDR, COMMSsender);

			/*

			Read_Flash(TEMP_ADDR, &temperature, sizeof(temperature));
			Send_to_WFQueue(&temperature, sizeof(temperature), TEMP_ADDR, COMMSsender);
			Read_Flash(TEMP_ADDR, &battery, sizeof(battery));
			Send_to_WFQueue(&battery, sizeof(battery), TEMP_ADDR, COMMSsender);
			Read_Flash(TEMP_ADDR, &state, sizeof(state));
			Send_to_WFQueue(&state, sizeof(state), TEMP_ADDR, COMMSsender);
			Read_Flash(TEMP_ADDR, &gyroscopes, sizeof(gyroscopes));
			Send_to_WFQueue(&gyroscopes, sizeof(gyroscopes), TEMP_ADDR, COMMSsender);
			Read_Flash(TEMP_ADDR, &magnetometer, sizeof(magnetometer));
			Send_to_WFQueue(&magnetometer, sizeof(magnetometer), TEMP_ADDR, COMMSsender);
			Read_Flash(TEMP_ADDR, &photodiodes, sizeof(photodiodes));
			Send_to_WFQueue(&photodiodes, sizeof(photodiodes), TEMP_ADDR, COMMSsender);
			*/

			decoded[0] = MISSION_ID;	//Satellite ID
			decoded[1] = POCKETQUBE_ID;	//Poquetcube ID (there are at least 3)
			decoded[2] = SEND_TELEMETRY;	//ID of the packet
			memcpy(&transformed, read_telemetry, sizeof(transformed));
			for (uint8_t i=3; i<TELEMETRY_PACKET_SIZE+3; i++){
				decoded[i] = transformed[i-3];
			}
			TimerTime_t currentUnixTime = RtcGetTimerValue();
			uint32_t unixTime32 = (uint32_t)currentUnixTime;
			decoded[TELEMETRY_PACKET_SIZE+3] = (unixTime32 >> 24) & 0xFF;
			decoded[TELEMETRY_PACKET_SIZE+4] = (unixTime32 >> 16) & 0xFF;
			decoded[TELEMETRY_PACKET_SIZE+5] = (unixTime32 >> 8) & 0xFF;
			decoded[TELEMETRY_PACKET_SIZE+6] = unixTime32 & 0xFF;
			//print_word( TELEMETRY_PACKET_SIZE+7,decoded);


			uint8_t encoded[256];
			int encoded_len_bytes = encode (decoded, encoded, TELEMETRY_PACKET_SIZE+7);
			//print_word(encoded_len_bytes,encoded);
			vTaskDelay(pdMS_TO_TICKS(3000));


			Radio.Send(encoded,encoded_len_bytes);
			vTaskDelay(pdMS_TO_TICKS(3000));
			Radio.Send(encoded,encoded_len_bytes);
			State = RX;
		}
		break;
	}
	case STOP_SENDING_DATA:{
		tx_flag = false;	//Deactivates TX flag
		State = RX;
		send_data = false;
		break;
	}
	case ACK_DATA:{
		if (!contingency && info != 0){
			nack_flag = true;
			int j = 0;
			for(int i = 3; i<decoded_size-4; i++){ //-4 to not take into account the unix time (the packet timestamp)
				if(decoded[i]==0x0){
					nack[j] = i-3;
					j++;
				}
			}
			nack_size = j;
			if (nack_size !=0){
				tx_flag = true;	//Activates TX flag
				State = TX;
				send_data = true;
			}
			else{
				tx_flag = false;	//Activates RX flag
				State = RX;
				send_data = false;
			}
		}
		break;
	}

	case ADCS_CALIBRATION:{
		/* CALIBRATION PACKET RECEIVED */
		Send_to_WFQueue(&decoded[4], CALIBRATION_PACKET_SIZE, CALIBRATION_ADDR + CALIBRATION_PACKET_SIZE*calibration_counter, COMMSsender);
		xTaskNotify(OBC_Handle, CALIBRATION_NOTI, eSetBits); //Notification to OBC

		break;

	}
	//For high SF 2 packets will be needed and the code should be adjusted
	case CHANGE_TIMEOUT:{
		Send_to_WFQueue(&decoded[3], 2, COMMS_TIME_ADDR, COMMSsender);

		break;
	}
	case ACTIVATE_PAYLOAD:{
		Send_to_WFQueue(&decoded[3], 4, PL_TIME_ADDR, COMMSsender);
		Send_to_WFQueue(& decoded[7], 1, PHOTO_RESOL_ADDR, COMMSsender);
		Send_to_WFQueue(&decoded[8], 1, PHOTO_COMPRESSION_ADDR, COMMSsender);
		xTaskNotify(OBC_Handle, TAKEPHOTO_NOTI, eSetBits); //Notification to OBC

		Send_to_WFQueue(&decoded[9], 8, PL_RF_TIME_ADDR, COMMSsender);
		Send_to_WFQueue(&decoded[17], 1, F_MIN_ADDR, COMMSsender);
		Send_to_WFQueue(& decoded[18], 1, F_MAX_ADDR, COMMSsender);
		Send_to_WFQueue(& decoded[19], 1, DELTA_F_ADDR, COMMSsender);
		Send_to_WFQueue(& decoded[20], 1, INTEGRATION_TIME_ADDR, COMMSsender);

		break;
	}
	case SEND_CONFIG:
	case NACK_CONFIG:{
		uint64_t read_config[4];

		uint8_t transformed[CONFIG_PACKET_SIZE];	//Maybe is better to use 40 bytes, as multiple of 8
		if (!contingency){

			Read_Flash(PHOTO_ADDR, (uint8_t*)read_config, sizeof(read_config)); // change to TELEMETRY_ADDR
			Send_to_WFQueue((uint8_t*)read_config, sizeof(read_config), PHOTO_ADDR, COMMSsender);


			decoded[0] = MISSION_ID;	//Satellite ID
			decoded[1] = POCKETQUBE_ID;	//Poquetcube ID (there are at least 3)
			decoded[2] = SEND_CONFIG;	//ID of the packet
			TimerTime_t currentUnixTime = RtcGetTimerValue();

			memcpy(&transformed, read_config, sizeof(transformed));
			for (uint8_t i=3; i<CONFIG_PACKET_SIZE+3; i++){
				decoded[i] = transformed[i-3];
			}
			uint32_t unixTime32 = (uint32_t)currentUnixTime;
			decoded[CONFIG_PACKET_SIZE+3] = (unixTime32 >> 24) & 0xFF;
			decoded[CONFIG_PACKET_SIZE+4] = (unixTime32 >> 16) & 0xFF;
			decoded[CONFIG_PACKET_SIZE+5] = (unixTime32 >> 8) & 0xFF;
			decoded[CONFIG_PACKET_SIZE+6] = unixTime32 & 0xFF;


			uint8_t conv_encoded[256];
			int encoded_len_bytes = encode (decoded, conv_encoded, CONFIG_PACKET_SIZE+7);

			vTaskDelay(pdMS_TO_TICKS(3000));
			Radio.Send(conv_encoded,encoded_len_bytes);
			vTaskDelay(pdMS_TO_TICKS(3000));
			Radio.Send(conv_encoded,encoded_len_bytes);
			State = RX;
		}
		break;
	}
	case UPLINK_CONFIG:{
		/* The lengths of the data sent to the flash should be
		   saved in its own variable for everything to be parametized (not hardcoded).
		*/
		Send_to_WFQueue(&decoded[3], 4, SET_TIME_ADDR, COMMSsender);
		Send_to_WFQueue(&decoded[7], 2, SF_ADDR, COMMSsender);
		Send_to_WFQueue(&decoded[9], 2, CRC_ADDR, COMMSsender);
		Send_to_WFQueue(&decoded[11], 1, KP_ADDR, COMMSsender);
		Send_to_WFQueue(&decoded[12], 1, GYRO_RES_ADDR, COMMSsender);
		Send_to_WFQueue(&decoded[13], 1, NOMINAL_ADDR, COMMSsender);
		Send_to_WFQueue(&decoded[14], 1, LOW_ADDR, COMMSsender);
		Send_to_WFQueue(&decoded[15], 1, CRITICAL_ADDR, COMMSsender);
		break;
	}
	default:{
		State = TX;
		error_telecommand = true;
		break;}
	}

	if(!error_telecommand)
		{
			xEventGroupSetBits(xEventGroup, COMMS_TELECOMMAND_EVENT); // Notify OBC task that the telecommand has already been processed (it unblocks OBC task)	                                                          // If the SEND_XXXX telecommands end up not sending the telemetry in this function (doing it on TX State), this line would have to change.
		}
}

int interleave(unsigned char *codeword, int size,unsigned char* codeword_interleaved){

	int initial_length = size;
	for(int i = 1; (initial_length + i) % (BLOCK_ROW_INTER*BLOCK_COL_INTER) != 0; i++){
		codeword[initial_length + i] = 0;
		size++;
	}

	bool end = false;
	int q = 0;
	int r = 0;
	int col;
	int row;
	char block[BLOCK_ROW_INTER][BLOCK_COL_INTER];

	while(q < size){
		for(col = 0; col < BLOCK_COL_INTER && !end; col++){
			for(row = 0; row < BLOCK_ROW_INTER && !end; row++){
				if (q < size){
					block[row][col] = codeword[q];
					q++;
				}
				else{
					end = true;
				}
			}
		}
		for(int t = 0; t < BLOCK_COL_INTER; t++){
			for(int p = 0; p < BLOCK_ROW_INTER; p++){
					codeword_interleaved[r] = block[t][p];
					r++;
			}
		}
	}
	return size;
}

int deinterleave(unsigned char *codeword_interleaved , int size,unsigned char* codeword_deinterleaved ){

	interleave(codeword_interleaved , size,codeword_deinterleaved);
	bool end = false;
	while(!end){
	  if( memcmp(codeword_deinterleaved[size-1], (const void *)&byte_to_compare, 1) != 0){
		size--;
	  }
	  else{
		size --;
		end = true;
	  }
	}
	return size;
}

int encode (uint8_t* buffer, uint8_t* encoded, int packet_size)
{
	 /////////////////////////////////////////////////////////////////////////
	//            //ENCODED REED SOLOMON LIBCORRECT
	//            print_word(TELEMETRY_PACKET_SIZE+4, decoded);
	//
	//            static const uint16_t correct_rs_primitive_polynomial_ccsds = 0x187;  // x^8 + x^7 + x^2 + x + 1
	//            size_t block_length = 255;
	//            size_t min_distance = 32;
	//            size_t message_length = block_length-min_distance;
	//
	//            correct_reed_solomon *rs = correct_reed_solomon_create(correct_rs_primitive_polynomial_ccsds, 1, 1, min_distance);
	//
	//            uint8_t rs_encoded[message_length];
	//            ssize_t size_encode = correct_reed_solomon_encode(rs, decoded, message_length,rs_encoded);
	//            print_word(size_encode, rs_encoded);
	//
	//			// add two random errors to codeword
	//			unsigned char r = rand() % 256;
	//			int rloc = rand() % (TELEMETRY_PACKET_SIZE+4);
	//			rs_encoded[rloc] = r;
	//
	//			unsigned char r2 = rand() % 256;
	//			int rloc2 = rand() % (TELEMETRY_PACKET_SIZE+4);
	//			rs_encoded[rloc2] = r2;
	//			print_word(size_encode, rs_encoded);
	//
	//            uint8_t rs_decoded[message_length];
	//            ssize_t size_decode = correct_reed_solomon_decode(rs, rs_encoded, size_encode,rs_decoded);
	//            print_word(size_decode, rs_decoded);

	//ENCODE REED SOLOMON
	unsigned char codeword[256];

	/* Initialization the ECC library */
	initialize_ecc ();
	srand(time(NULL));   // Initialization, should only be called once.

	int ML = packet_size + NPAR;
	encode_data(buffer, packet_size, codeword);
	codeword[ML] = 0xFF;	//Final of the packet indicator
	ML++;

	//INTERLEAVE
	int size = interleave(codeword, ML, encoded);

	//ENCODE CONVOLUTIONAL
/*
	uint8_t msg[size];
	memcpy(msg, codeword_interleaved, size);

	correct_convolutional *conv;

	//create convolutional config
	conv = correct_convolutional_create(RATE_CON, ORDER_CON, correct_conv_r12_7_polynomial);

	//get size of encoded message in bytes
	size_t len_to_encode = correct_convolutional_encode_len(conv, size);
	int len_to_encode_bytes = ceil(len_to_encode/8);

	//encode message
	size_t encoded_len_bits = correct_convolutional_encode(conv,msg,size,conv_encoded);
	int encoded_len_bytes = ceil(encoded_len_bits/8);
*/
	//print_word(size, encoded);

	//add random errors
	uint8_t r3 = rand() % 256;
	int rloc3 = rand() % (size);
	encoded[rloc3] = r3;

	uint8_t  r4 = rand() % 256;
	int rloc4 = rand() % (size);
	encoded[rloc4] = r4;

	//print_word(size, encoded);
	return size;

}

void print_word(int p, unsigned char *data) {
  int i;
  for (i=0; i < p; i++) {
    printf ("%02X ", data[i]);
  }
  printf("\n");
}
