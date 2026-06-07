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
static void MX_GPIO_Init(void);
static void MX_TIM5_Init(void);
static void MX_TIM2_Init(void);
static void MX_SPI2_Init(void);
static void MX_IWDG_Init(void);
static void MX_USART2_UART_Init(void);
static void MX_RTC_Init(void);
static void MX_ADC1_Init(void);
static void MX_I2C1_Init(void);

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
  SystemClock_Config();  
  MX_GPIO_Init(); 
  MX_TIM5_Init();
  MX_TIM2_Init();
  MX_SPI2_Init();
  MX_IWDG_Init();
  MX_USART2_UART_Init();
  MX_RTC_Init();
  MX_ADC1_Init();
  MX_I2C1_Init();

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


/* USER CODE BEGIN MX_I2C_Init */

/**
  * @brief I2C1 Initialization Function
  * @details Configures I2C1 at 100kHz for the DS2782 Battery Sensor.
  */
static void MX_I2C1_Init(void)
{
  hi2c1.Instance = I2C1;
  
  // Timing value for 100kHz based on the 80MHz System Clock
  hi2c1.Init.Timing = 0x10909CEC; 
  
  hi2c1.Init.OwnAddress1 = 0;
  hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c1.Init.OwnAddress2 = 0;
  hi2c1.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
  hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE; 
  
  if (HAL_I2C_Init(&hi2c1) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE END MX_I2C_Init */

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
