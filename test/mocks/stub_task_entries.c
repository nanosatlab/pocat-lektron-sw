/* Empty task entry points: the fake RTOS never runs a task body, but task_management.c's
 * task table needs these symbols to link. Also main.c's getter of the OBC task handle,
 * which tm_current_index() uses. */
#include "stub_task_entries.h"
#include "main.h"
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

static TaskHandle_t obc_handle;
void stub_set_obc_handle(TaskHandle_t handle) { obc_handle = handle; }
TaskHandle_t main_get_obc_handle(void) { return obc_handle; }
