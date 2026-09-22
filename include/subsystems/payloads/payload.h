/**
 * @file payload.h
 * @brief Payload task header file.
 * 
 */

#ifndef INC_PAYLOAD_H_
#define INC_PAYLOAD_H_

/**
 * @brief Payload FreeRTOS task entry point.
 * @param pv_parameters Task parameter provided by xTaskCreate(); currently unused.
 */
void payload_task(void *pv_parameters);

#endif /* INC_PAYLOAD_H_ */
