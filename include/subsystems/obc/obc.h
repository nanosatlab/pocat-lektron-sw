/**
 * @file obc.h
 * @author guillermo.o.tuama@estudiantat.upc.edu
 * @details 
 * OBC Task serves as the central scheduler, coordinating the operation of all other tasks. 
 * It is responsible for managing transitions between different operational modes, task management, 
 * power control, and essential satellite checkups.
 * 
 */

#ifndef INC_OBC_H_
#define INC_OBC_H_

#include "FreeRTOS.h"
#include "task.h"
#include "state_machine.h"

// TODO: revisar stack sizes y prioridades!

// Task stack sizes
#define OBC_STACK_SIZE       1024
#define PAYLOAD_STACK_SIZE   512
#define EPS_STACK_SIZE       512
#define COMMS_STACK_SIZE     1024
#define ADCS_STACK_SIZE      1024
#define OBDH_STACK_SIZE      1024
#define TRANSCEIVER_STACK_SIZE 1024
#define BEACON_STACK_SIZE    512

// Task priorities
#define OBC_PRIORITY        6
#define OBDH_PRIORITY       5
#define COMMS_PRIORITY      4
#define TRANSCEIVER_PRIORITY 4
#define BEACON_PRIORITY     4
#define EPS_PRIORITY        3
#define ADCS_PRIORITY       2
#define PAYLOAD_PRIORITY    1

/**
 * @brief OBC FreeRTOS task entry point: runs the OBC state machine.
 * @param pv_parameters Task parameter provided by xTaskCreate(); currently unused.
 */
void obc_task(void *pv_parameters);

#endif /* INC_OBC_H_ */