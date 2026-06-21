/**
 * @file main.c
 * @brief System boot and RTOS initialization.
 * @author Guillermo O'Tuama Pascual
 * @date 2026-01-20
 * @details This is the main module. It performs system initialization and starts the FreeRTOS scheduler. It:
 * - Initializes the HAL library
 * - Restores the boot state from flash
 * - Configures the system clock for that state
 * - Initializes board peripherals
 * - Creates the OBC task and starts the FreeRTOS scheduler
 */

#include "main.h"
#include "stm32l4xx_hal.h"
#include <stdio.h>
#include "obc.h"
#include "clock.h"
#include "periph.h"
#include "state_machine.h"
#include "log.h"
#include "time.h"
#include "flash.h"

/** @brief Handle for the OBC FreeRTOS task*/
static TaskHandle_t obc_task_handle;

TaskHandle_t main_get_obc_handle(void) { return obc_task_handle; }

/* Private function prototypes */
void SystemClock_Config(void);

/**
 * @brief Main function
 *
 * @details 
 * - Initializes the hardware abstraction layer
 * - Configures the system clock
 * - Initializes peripherals
 * - Creates the OBC task
 * - Starts the FreeRTOS scheduler.
 *
 * @return This function should never return.
 */
int main(void)
{

  HAL_Init();

  ObcState_t bootState;
  Read_Flash(CURRENT_STATE_ADDR, (uint8_t*)&bootState, sizeof(ObcState_t));

  ClockFreq_t freq = freq_for_state(bootState);
  if (!systemclock_init_for_freq(freq)) {
      Error_Handler();
  }
  periph_init_for_freq(freq);

  log_init();
  time_init();
  printf("\r\n=======================\r\n pocat flight software\r\n=======================\r\n\r\n");
  
  BaseType_t result = xTaskCreate(obc_task, "OBC", OBC_STACK_SIZE, (void*)(uint32_t)bootState, OBC_PRIORITY, &obc_task_handle);

  if (result != pdPASS) {
      printf("Failed to create OBC task!\r\n");
  } else {
      printf("OBC task created successfully\r\n");
  }

  vTaskStartScheduler();

  return 0;
  
}


/**
  * @brief  This function is executed in case of error occurrence.
  * @todo Implement error handling mechanism.
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
