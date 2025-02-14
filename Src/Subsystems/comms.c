/*
 * comms2.c
 *
 *  Created on: Jul 10, 2024
 *      Author: Óscar Pavón
 *  This is a full rework of previous COMMS
 *  Previous work by Artur Cot & Julia Tribó
 */

#include "Subsystems/comms.h"

typedef enum // Possible States of the State Machine
{ STARTUP,
  STDBY,
  RX,
  TX,
  SLEEP,
} COMMS_States;

COMMS_States COMMS_State = STARTUP;

static RadioEvents_t RadioEvents;

/************  PACKETS  ************/

uint8_t cust_RxData[48]; // Tx and Rx decoded
uint8_t Decoded_Data[48];

uint8_t TxPacket[48] = {MISSION_ID, POCKETQUBE_ID};
uint8_t payloadData[48];
uint8_t Encoded_Packet[48];

uint8_t packet_number = 1;
uint8_t packet_start = 1;
uint8_t plsize = 39;
uint8_t packet_window = 5;
uint8_t TLCReceived = 0;

uint8_t packet_to_send[48] = {MISSION_ID, POCKETQUBE_ID, BEACON}; // Test packet

/*************  FLAGS  *************/

int TLCReceived_Flag = 0;
int CADRX_Flag = 0;
int BedTime_Flag = 0;
int Wait_ACK_Flag = 0;
int TXACK_Flag = 0;
int GoTX_Flag = 0;
int TxData_Flag = 0;
int Beacon_Flag = 0;
int TXStopped_Flag = 0;

/***********  COUNTERS  ***********/

uint16_t COMMSRxErrors = 0;
uint16_t COMMSRxTimeouts = 0;
uint16_t COMMSNotUs = 0;
uint8_t TLE_counter = 1;
uint8_t ADCS_counter = 1;
uint16_t window_counter = 1;
uint8_t telemetry_counter[TLCOUNTER_MAX] = {0};
// TimerHandle_t xTimerBeacon;
uint32_t current_telemetry_adress = TELEMETRY_LEGACY_ADDR;

/*************  SIGNAL  *************/

int8_t RssiValue = 0;
int8_t SnrValue = 0;
int16_t RssiMoy = 0;
int8_t SnrMoy = 0;

/*************  CONFIG  *************/
int CADMODE_Flag = 0;

uint16_t packetwindow = 1;
uint32_t rxTime = 1000;
uint32_t sleepTime = 1000;
uint32_t RF_F = 868000000;
uint8_t SF = 11;
uint8_t CR = 1;

void COMMS_StateMachine(void) {
  COMMS_State = STARTUP;
  /* Target board initialization*/

  BoardInitMcu();

  /* Radio initialization */
  RadioEvents.TxDone = OnTxDone; // standby
  RadioEvents.RxDone = OnRxDone; // standby
  RadioEvents.TxTimeout = OnTxTimeout;
  RadioEvents.RxTimeout = OnRxTimeout;
  RadioEvents.RxError = OnRxError;
  RadioEvents.CadDone = OnCadDone;
  Radio.Init(&RadioEvents);  // Initializes the Radio
  SX1262Config(11, 1, RF_F); // Configures the transceiver
  COMMS_State = SLEEP;       // Radio is already in STDBY
  SX126xConfigureCad(CAD_SYMBOL_NUM, CAD_DET_PEAK, CAD_DET_MIN, 0);

  for (;;) {
    // COMMS_RX_OBCFlags(); // Function that checks the notifications sent by
    // OBC to COMMS.

    Radio.IrqProcess(); // Checks the interruptions
    switch (COMMS_State) {
    case RX:

      if (Wait_ACK_Flag) {
        Radio.Rx(4000);
        Wait_ACK_Flag = 0;
      }
      if (TLCReceived_Flag) {
        // deinterleave(&cust_RxData,sizeof(cust_RxData),Decoded_Data);
        process_telecommand(cust_RxData);
        TLCReceived_Flag = 0;
      } else {
        COMMS_State = STDBY;
        BedTime_Flag = 0;
      }
      break;
    case TX:
      if (Beacon_Flag) {
        Beacon_Flag = 0;
        TxPrepare(BEACON_OP);
        Radio.Send(packet_to_send, 48);
        vTaskDelay(4000);
        COMMS_State = SLEEP;
      }

      if (TXACK_Flag) {
        TXACK_Flag = 0;
        TxPrepare(ACK_OP);
        Radio.Send(packet_to_send, 3);
        vTaskDelay(pdMS_TO_TICKS(Radio.TimeOnAir(MODEM_LORA, 6)));
        COMMS_State = SLEEP;
      }

      if (TxData_Flag) {
        TxData_Flag = 0;
        packet_start = cust_RxData[3]; // TDB
        packet_number = packet_start;
        for (window_counter = 1; window_counter <= packet_window;
             window_counter++) {
          packet_number++;
          TxPrepare(DATA_OP);
          Radio.Send(TxPacket, plsize + 9);
          vTaskDelay(pdMS_TO_TICKS(2000));
        }
        COMMS_State = RX;
        Wait_ACK_Flag = 1;
      }

      break;
    case STDBY:
      switch (TLCReceived) {
      case (UPLINK_CONFIG):
        Radio.Standby();
        if (cust_RxData[4] == RF_ID1 && (RF_F / 86800000) != 1) // 128
        {
          RF_F = 86800000;
          Radio.SetChannel(RF_F);
        } else if (cust_RxData[4] == RF_ID2 && (RF_F / 91500000) != 1) // 255
        {
          RF_F = 91500000;
          Radio.SetChannel(RF_F);
        }

        if ((10 <= cust_RxData[5]) | (cust_RxData[5] <= 14)) {
          SF = cust_RxData[5]; // TBD
        }

        if ((1 <= cust_RxData[6]) | (cust_RxData[6] <= 4)) {
          CR = cust_RxData[6]; // TBD
        }

        // if ((10000 > cust_RxData[4]) | (cust_RxData[4] > 1000)) {
        //   rxTime = cust_RxData[4];
        // } // Might not be data 4, to revisit
        // If agreed, this needs revision cust_RxData[4] will never be 1000, as it is uint_8 (max 256)
        Radio.SetTxConfig(MODEM_LORA, TX_OUTPUT_POWER, 0, LORA_BANDWIDTH, SF,
                          CR, LORA_PREAMBLE_LENGTH, LORA_FIX_LENGTH_PAYLOAD_ON,
                          true, 0, 0, LORA_IQ_INVERSION_ON, 3000);

        Radio.SetRxConfig(MODEM_LORA, LORA_BANDWIDTH, SF, CR, 0,
                          LORA_PREAMBLE_LENGTH, LORA_SYMBOL_TIMEOUT,
                          LORA_FIX_LENGTH_PAYLOAD_ON, 0, true, 0, 0,
                          LORA_IQ_INVERSION_ON, true);

        COMMS_State = TX;
        Beacon_Flag = 1;
        break;
      case (RESET2):
        COMMS_State = STARTUP;
        break;
      default:
        COMMS_State = SLEEP;
        break;
      }
      break;
    case SLEEP:
      if (BedTime_Flag) {
        Radio.Sleep();
        vTaskDelay(pdMS_TO_TICKS(rxTime));
        BedTime_Flag = 0;
      } else {
        if (CADMODE_Flag && CADRX_Flag) {
          Radio.Rx(4000);
          CADRX_Flag = 0;
        } else if (CADMODE_Flag) {
          Radio.StartCad();
          vTaskDelay(pdMS_TO_TICKS(rxTime));
        } else {
          Radio.Rx(rxTime * 2);
          vTaskDelay(pdMS_TO_TICKS(rxTime * 2));
        }
        BedTime_Flag = 1;
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
      Radio.Init(&RadioEvents);  // Initializes the Radio
      SX1262Config(11, 1, RF_F); // Configures the transceiver
      COMMS_State = SLEEP;       // Radio is already in STDBY
      SX126xConfigureCad(CAD_SYMBOL_NUM, CAD_DET_PEAK, CAD_DET_MIN, 0);
      /* fall through */
    default:
      COMMS_State = SLEEP;
      break;
    }
  }
}

void OnCadDone(bool channelActivityDetected) {
  if (channelActivityDetected == true) {
    COMMS_State = SLEEP;
    CADRX_Flag = 1;
    BedTime_Flag = 0;
  } else {
    Radio.Standby();
    COMMS_State = SLEEP;
  }
}

void OnTxDone() {}

void OnRxDone(uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr) {

  memset(cust_RxData, 0, size);
  memcpy(cust_RxData, payload, size);

  RssiValue = rssi;
  SnrValue = snr;

  // RssiMoy = (((RssiMoy*RxCorrectCnt)+RssiValue)/(RxCorrectCnt+1));
  // SnrMoy = (((SnrMoy*RxCorrectCnt)+SnrValue)/(RxCorrectCnt+1));

  // xEventGroupSetBits(xEventGroup, COMMS_RXIRQFlag_EVENT);

  if (cust_RxData[0] == 0xC8 && cust_RxData[1] == 0x9D) {
    TLCReceived_Flag = 1;
    COMMS_State = RX;
  } else {
    memset(cust_RxData, 0, sizeof(cust_RxData));
    COMMS_State = STDBY;
    Radio.Standby();
    COMMSNotUs++;
  }
}

void OnRxError() {
  Radio.Standby();
  COMMS_State = RX;
  COMMSRxErrors++;
}

void OnRxTimeout() {
  COMMS_State = SLEEP;
  COMMSRxTimeouts++;
  ADCS_counter = 1;
  TLE_counter = 1;
}

void OnTxTimeout() {
  Radio.Standby();
  COMMS_State = STDBY;
}

void SX126xConfigureCad(RadioLoRaCadSymbols_t cadSymbolNum, uint8_t cadDetPeak,
                        uint8_t cadDetMin, uint32_t cadTimeout) {
  SX126xSetDioIrqParams(IRQ_CAD_DONE | IRQ_CAD_ACTIVITY_DETECTED,
                        IRQ_CAD_DONE | IRQ_CAD_ACTIVITY_DETECTED,
                        IRQ_RADIO_NONE, IRQ_RADIO_NONE);
  SX126xSetCadParams(cadSymbolNum, cadDetPeak, cadDetMin, LORA_CAD_RX,
                     cadTimeout);
  // THE TOTAL CAD TIMEOUT CAN BE EQUAL TO RX TIMEOUT (IT SHALL NOT BE HIGHER
  // THAN 4 SECONDS)
}

void SX1262Config(uint8_t SF, uint8_t CR, uint32_t RF_F) {

  /* Reads the SF, CR and time between packets variables from memory */

  /* Configuration of the LoRa frequency and TX and RX parameters */
  Radio.SetChannel(RF_F);

  Radio.SetTxConfig(MODEM_LORA, TX_OUTPUT_POWER, 0, LORA_BANDWIDTH, SF, CR,
                    LORA_PREAMBLE_LENGTH, LORA_FIX_LENGTH_PAYLOAD_ON, true, 0,
                    0, LORA_IQ_INVERSION_ON, 3000);

  Radio.SetRxConfig(MODEM_LORA, LORA_BANDWIDTH, SF, CR, 0, LORA_PREAMBLE_LENGTH,
                    LORA_SYMBOL_TIMEOUT, LORA_FIX_LENGTH_PAYLOAD_ON, 0, true, 0,
                    0, LORA_IQ_INVERSION_ON, true);
}

void process_telecommand(uint8_t Data[]) {
  TLCReceived = Data[2];
  switch (TLCReceived) {
  case RESET2:    // Satellite reset RX 
    HAL_NVIC_SystemReset();
    GoTX_Flag = 1;
    Beacon_Flag = 1;
    break;
  case EXIT_STATE:  // Force to change State by TC
    if (Data[3] == 0xF0) {
      xTaskNotify(OBC_Handle, EXIT_CONTINGENCY_NOTI,
                  eSetBits); // Notification to OBC
    } else if (Data[3] == 0x0F) {
      xTaskNotify(OBC_Handle, EXIT_SUNSAFE_NOTI,
                  eSetBits); // Notification to OBC
    } else if (Data[4] == 0xF0) {
      xTaskNotify(OBC_Handle, EXIT_SURVIVAL_NOTI,
                  eSetBits); // Notification to OBC
    }

    GoTX_Flag = 1;
    Beacon_Flag = 1;
    break;
  case TLE: // TLE Received, we need to update
    if ((TLE_counter == 1 && Data[2] == 86) ||
        (TLE_counter == 2 && Data[2] == 164)) {
      Send_to_WFQueue(&Data[3], TLE_PACKET_SIZE,
                      TLE_ADDR1 + (TLE_counter - 1) * TLE_PACKET_SIZE,
                      COMMSsender);
      TLE_counter++;
      Wait_ACK_Flag = 1;
    } else if (TLE_counter == 3 && Data[2] == 255) {
      Send_to_WFQueue(&Data[3], 1, TLE_ADDR1 + 2 * TLE_PACKET_SIZE,
                      COMMSsender);
      Send_to_WFQueue(&Data[4], TLE_PACKET_SIZE - 1, TLE_ADDR2, COMMSsender);
      TLE_counter = 1;
      GoTX_Flag = 1;
      TXACK_Flag = 1;
    }
    break;
  case SEND_DATA: // We are asked to send data
    plsize = 39;
    packetwindow = 5;
    GoTX_Flag = 1;
    TxData_Flag = 1;
    break;
  case SEND_TELEMETRY:  // We are asked to send telemetry
    Beacon_Flag = 1;
    GoTX_Flag = 1;
    break;
  case ACTIVATE_PAYLOAD:  // TC to activate the PL
    Send_to_WFQueue(&Data[3], 4, PL_TIME_ADDR, COMMSsender);
    Send_to_WFQueue(&Data[7], 1, PHOTO_RESOL_ADDR, COMMSsender);
    Send_to_WFQueue(&Data[8], 1, PHOTO_COMPRESSION_ADDR, COMMSsender);
    xTaskNotify(OBC_Handle, TAKEPHOTO_NOTI, eSetBits); // Notification to OBC

    Send_to_WFQueue(&Data[9], 8, PL_RF_TIME_ADDR, COMMSsender);
    Send_to_WFQueue(&Data[17], 1, F_MIN_ADDR, COMMSsender);
    Send_to_WFQueue(&Data[18], 1, F_MAX_ADDR, COMMSsender);
    Send_to_WFQueue(&Data[19], 1, DELTA_F_ADDR, COMMSsender);
    Send_to_WFQueue(&Data[20], 1, INTEGRATION_TIME_ADDR, COMMSsender);
    GoTX_Flag = 1;
    TXACK_Flag = 1;
    break;
  case UPLINK_CONFIG: // Update configuration
    COMMS_State = STDBY;
    break;
  case STOP_TRANSMISSION:
    xTimerStop(xTimerBeacon, 0);
    TXStopped_Flag = 1;
    /* fall through */
  default:
    COMMS_State = SLEEP;
    break;
  }
  if (TXStopped_Flag) {
    GoTX_Flag = 0;
  }
  // Now send notification to OBC
  if (GoTX_Flag) {
    GoTX_Flag = 0;
    COMMS_State = TX;
    SX126xSetFs(); // Probably useless (15us)
  }
}

void TxPrepare(uint8_t operation) {
  TimerTime_t currentUnixTime = RtcGetTimerValue();
  uint32_t unixTime32 = (uint32_t)currentUnixTime;
  switch (operation) {
    case BEACON_OP:
      TxPacket[2] = BEACON; // should be packet ID
      break;
    case ACK_OP:
      // TxPacket[2] = ACK;
      // TxPacket[3] = TLCResult;
      TxPacket[4] = (unixTime32 >> 24) & 0xFF;
      TxPacket[5] = (unixTime32 >> 16) & 0xFF;
      TxPacket[6] = (unixTime32 >> 8) & 0xFF;
      TxPacket[7] = unixTime32 & 0xFF;
      TxPacket[8] = 0xFF;
      break;
    case DATA_OP:
      TxPacket[2] = SEND_DATA;
      TxPacket[3] = packet_number;

      TxPacket[plsize + 4] = (unixTime32 >> 24) & 0xFF;
      TxPacket[plsize + 5] = (unixTime32 >> 16) & 0xFF;
      TxPacket[plsize + 6] = (unixTime32 >> 8) & 0xFF;
      TxPacket[plsize + 7] = unixTime32 & 0xFF;
      TxPacket[plsize + 8] = 0xFF;

      Read_Flash(PHOTO_ADDR + packet_number * plsize, (uint8_t *)payloadData, plsize);
      memmove(TxPacket + 4, payloadData, plsize);
      // interleave(TxPacket,plsize+9,Encoded_Packet);
      Wait_ACK_Flag = 1;
      break;
    default:
      Radio.Standby();
      COMMS_State = STDBY;
      memset(TxPacket, 0, sizeof(TxPacket));
      memset(payloadData, 0, sizeof(payloadData));
      break;
  }
}

int interleave(unsigned char *codeword, int size,
               unsigned char *codeword_interleaved) {

  bool end = false;
  int q = 0;
  int r = 0;
  int initial_length = size;
  char block[BLOCK_ROW_INTER][BLOCK_COL_INTER];

  for (int i = 1;
       (initial_length + i) % (BLOCK_ROW_INTER * BLOCK_COL_INTER) != 0; i++) {
    codeword[initial_length + i] = 0;
    size++;
  }
  
  while (q < size) {
    for (int col = 0; col < BLOCK_COL_INTER && !end; col++) {
      for (int row = 0; row < BLOCK_ROW_INTER && !end; row++) {
        if (q < size) {
          block[row][col] = codeword[q];
          q++;
        } else {
          end = true;
        }
      }
    }
    for (int t = 0; t < BLOCK_COL_INTER; t++) {
      for (int p = 0; p < BLOCK_ROW_INTER; p++) {
        codeword_interleaved[r] = block[t][p];
        r++;
      }
    }
  }
  return size;
}

int deinterleave(unsigned char *codeword_interleaved, int size,
                 unsigned char *codeword_deinterleaved) {

  interleave(codeword_interleaved, size, codeword_deinterleaved);
  bool end = false;
  while (!end) {
    if (memcmp(&codeword_deinterleaved[size - 1], &(uint8_t){0xFF}, 1) != 0) {
      size--;
    } else {
      size--;
      end = true;
    }
  }
  return size;
}

void beacon_time() {
  Beacon_Flag = 1;
  COMMS_State = TX;
}

void store_telemetry() {
  // Design 1: Reserve a space for the counter and flash two times
  uint8_t telemetry_data[BEACON_PL_LEN] = {0};
  if (telemetry_counter[0] == 0) {
    Read_Flash(TELEMETRY_LEGACY_ADDR, (uint8_t *)telemetry_counter, TLCOUNTER_MAX);
    for (int i = 0; i < TLCOUNTER_MAX; i++) {
      if (telemetry_counter[i] == 0) {
        telemetry_counter[0] = i;
        break;
      }
    }
  }
  // Telemetry data should be polled from sensors and stored in the
  // telemetry_data[BEACON_PL_LEN] array

  if (telemetry_counter[0] == TLCOUNTER_MAX) {
    erase_page(TELEMETRY_LEGACY_ADDR);
    telemetry_counter[0] = 0;
  }
  telemetry_counter[0]++;
  current_telemetry_adress =
      (TELEMETRY_LEGACY_ADDR + telemetry_counter[0] * BEACON_PL_LEN +
       TLCOUNTER_MAX);
  Send_to_WFQueue((uint8_t *)telemetry_data, sizeof(telemetry_data),
                  current_telemetry_adress, COMMSsender);
  Send_to_WFQueue(&telemetry_counter[0], sizeof(telemetry_counter[0]),
                  TELEMETRY_LEGACY_ADDR + telemetry_counter[0],
                  COMMSsender); // Design 1

}
