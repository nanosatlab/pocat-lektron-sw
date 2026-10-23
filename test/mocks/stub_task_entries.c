/* Empty task entry points: the fake RTOS never runs a task body, but task_management.c's
 * task table needs these symbols to link. */
#include "payload.h"
#include "eps.h"
#include "comms.h"
#include "adcs.h"
#include "obdh.h"
#include "transceiver.h"
#include "beacon.h"

void payload_task(void *pv_parameters)     { (void)pv_parameters; }
void eps_task(void *pv_parameters)         { (void)pv_parameters; }
void comms_task(void *pv_parameters)       { (void)pv_parameters; }
void adcs_task(void *pv_parameters)        { (void)pv_parameters; }
void obdh_task(void *pv_parameters)        { (void)pv_parameters; }
void transceiver_task(void *pv_parameters) { (void)pv_parameters; }
void beacon_task(void *pv_parameters)      { (void)pv_parameters; }
