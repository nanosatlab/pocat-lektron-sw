#include "main.h"
#include "stm32l4xx_hal.h"
#include <stdio.h>
#include "obc.h"

/* ---- Macros and constants ---- */

/* ---- Module-level variables ---- */
TaskHandle_t obc_task_handle;

/* ---- Private function prototypes ---- */
void SystemClock_Config(void);
void SystemInit(void);


int main(void)
{

    HAL_Init();
    SystemClock_Config();
    MX_USART2_UART_Init();  // Initialized for printf usage

    printf("********************************\n\r");
    printf(" PoCat FLIGHT SOFTWARE \n\r");
    printf("*********************************\n\r");

    xTaskCreate(obc_task, "OBC", OBC_STACK_SIZE, NULL, OBC_PRIORITY, &xObcTaskHandle);

    vTaskStartScheduler();

    // Include error handling here, infinite loop...?
 
    return 0;

}

void SystemClock_Config(void) {

    RCC_OscInitTypeDef RCC_OscInitStruct;
    RCC_ClkInitTypeDef RCC_ClkInitStruct;
    RCC_PeriphCLKInitTypeDef PeriphClkInit;

    __HAL_RCC_LSEDRIVE_CONFIG(RCC_LSEDRIVE_LOW);

    RCC_OscInitStruct.OscillatorType =
    RCC_OSCILLATORTYPE_HSI | RCC_OSCILLATORTYPE_LSE;
    RCC_OscInitStruct.LSEState = RCC_LSE_ON;
    RCC_OscInitStruct.HSIState = RCC_HSI_ON;
    RCC_OscInitStruct.HSICalibrationValue = 16;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
    RCC_OscInitStruct.PLL.PLLM = 1;
    RCC_OscInitStruct.PLL.PLLN = 10;
    RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV7;
    RCC_OscInitStruct.PLL.PLLQ = RCC_PLLQ_DIV2;
    RCC_OscInitStruct.PLL.PLLR = RCC_PLLR_DIV2;
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) {
        // assert_failed(__FILE__, __LINE__);
        assert_param(FAIL);
    }

    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
    RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK) {
        // assert_failed(__FILE__, __LINE__);
        assert_param(FAIL);
    }

    PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_RTC | RCC_PERIPHCLK_USART2;
    PeriphClkInit.Usart2ClockSelection = RCC_USART2CLKSOURCE_PCLK1;
    PeriphClkInit.RTCClockSelection = RCC_RTCCLKSOURCE_LSE;
    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK) {
        // assert_failed(__FILE__, __LINE__);
        assert_param(FAIL);
    }

    if (HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1) != HAL_OK) {
        // assert_failed(__FILE__, __LINE__);
        assert_param(FAIL);
    }
    // HAL_SYSTICK_Config(HAL_RCC_GetHCLKFreq()/1000);
    // HAL_SYSTICK_CLKSourceConfig(SYSTICK_CLKSOURCE_HCLK);
    // HAL_NVIC_SetPriority(SysTick_IRQn, 0, 0);

    HAL_NVIC_SetPriority(SPI2_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(SPI2_IRQn);
}
