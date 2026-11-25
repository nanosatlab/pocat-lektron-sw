/**
 * @file stub_task_entries.c
 * @brief Empty task entry points so task_management.c links in host tests.
 * @date 2026-10-23
 *
 * @details
 * `task_table[]` in task_management.c points at the 7 task functions
 * (`payload_task`, `eps_task`, ...). The fake RTOS records tasks but never
 * runs their body, so these empty versions are enough to link.
 *
 * They are declared **weak**: when a suite also compiles the real task, as
 * test_obdh does by `#include`-ing obdh.c, the linker uses the real
 * function, and the stub is ignored without a "multiple definition" error.
 */
#include "payload.h"
#include "eps.h"
#include "comms.h"
#include "adcs.h"
#include "obdh.h"
#include "transceiver.h"
#include "beacon.h"

#define POCAT_WEAK __attribute__((weak))

POCAT_WEAK void payload_task(void *pv_parameters)     { (void)pv_parameters; }
POCAT_WEAK void eps_task(void *pv_parameters)         { (void)pv_parameters; }
POCAT_WEAK void comms_task(void *pv_parameters)       { (void)pv_parameters; }
POCAT_WEAK void adcs_task(void *pv_parameters)        { (void)pv_parameters; }
POCAT_WEAK void obdh_task(void *pv_parameters)        { (void)pv_parameters; }
POCAT_WEAK void transceiver_task(void *pv_parameters) { (void)pv_parameters; }
POCAT_WEAK void beacon_task(void *pv_parameters)      { (void)pv_parameters; }
