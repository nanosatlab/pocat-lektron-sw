/**
 * @file adcs.h
 * @brief Processes sensor data, executes algorithms to determine the satellite's 
    position, and issues commands to control and stabilize its orientation.
 * @date 2026-01-20
 * 
 */

#ifndef INC_ADCS_H_
#define INC_ADCS_H_

/**
 * @brief ADCS FreeRTOS task entry point.
 * @param pv_parameters Task parameter provided by xTaskCreate(); currently unused.
 */
void adcs_task(void *pv_parameters);

#endif /* INC_ADCS_H_ */
