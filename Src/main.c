/*

#include "main.h"
#include "stm32l4xx_hal.h"
#include <stdio.h>
#include "obc.h"
*/
/* ---- Macros and constants ---- */

/* ---- Module-level variables ---- */
//static TaskHandle_t obc_task_handle;

// peripheral variables
//SPI_HandleTypeDef hspi2; // important to define it because of extern declaration in header

/* ---- Private function prototypes ---- */
/*
static void SystemClock_Config(void);
static void MX_SPI2_Init(void);


int main(void)
{

    HAL_Init();
    SystemClock_Config();
    MX_USART2_UART_Init();  // Initialized for printf usage
    MX_SPI2_Init(); // SPI initialization

    int res = prova();
    if(res == 0) {
        prova_send("Hola nanosat!");
    }

    printf("*********************************\r\n");
    printf(" PoCat FLIGHT SOFTWARE\r\n");
    printf("*********************************\r\n");
printf("Creant tasca OBC... ");
    
    BaseType_t status = xTaskCreate(obc_task, "OBC", OBC_STACK_SIZE, NULL, OBC_PRIORITY, &obc_task_handle);

    if (status != pdPASS) {
        printf("[ERROR]\r\n");
        printf("ERROR CRITIC: No s'ha pogut crear la tasca OBC!\r\n");
        printf("CAUSA PROBABLE: Falta memoria Heap al FreeRTOS.\r\n");
        printf("SOLUCIO: Augmenta configTOTAL_HEAP_SIZE a FreeRTOSConfig.h\r\n");
        
        // Bucle infinit d'error (el LED parpellejant aniria bé aquí)
        while(1) {
            // Error fatal
        }
    } else {
        printf("[OK]\r\n");
    }

    // 5. Arrencar el Sistema Operatiu
    printf("Arrencant Scheduler...\r\n");
    vTaskStartScheduler();

    // 6. Gestió d'error si el Scheduler falla
    // El codi MAI hauria d'arribar aquí si tot va bé.
    printf("ERROR FATAL: vTaskStartScheduler ha retornat!\r\n");
    printf("Aixo passa si no hi ha prou Heap per crear la tasca Idle.\r\n");
 
    while (1)
    {
    }
 
    return 0;

}

static void SystemClock_Config(void) {

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

*/
/*
static void MX_SPI2_Init(void)
{

 
  hspi2.Instance = SPI2;
  hspi2.Init.Mode = SPI_MODE_MASTER;
  hspi2.Init.Direction = SPI_DIRECTION_2LINES;
  hspi2.Init.DataSize = SPI_DATASIZE_4BIT;
  hspi2.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi2.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi2.Init.NSS = SPI_NSS_HARD_OUTPUT;
  hspi2.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_2;
  hspi2.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi2.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi2.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi2.Init.CRCPolynomial = 7;
  hspi2.Init.CRCLength = SPI_CRC_LENGTH_DATASIZE;
  hspi2.Init.NSSPMode = SPI_NSS_PULSE_ENABLE;
  if (HAL_SPI_Init(&hspi2) != HAL_OK)
  {
    // Error_Handler(); have to implement
  }

}
*/
#include "main.h"
#include "stm32l4xx_hal.h"
#include <stdio.h>
#include "obc.h"
#include <string.h>

/* ---- Macros and constants ---- */

/* ---- Module-level variables ---- */
static TaskHandle_t obc_task_handle;
extern UART_HandleTypeDef huart2;
// peripheral variables
SPI_HandleTypeDef hspi2; 

/* ---- Private function prototypes ---- */
static void SystemClock_Config(void);
static void MX_SPI2_Init(void);

// Aquesta funció està definida a Src/Utils/log.c, així que només posem el prototip
// perquè el main sàpiga que existeix i la pugui cridar.
void MX_USART2_UART_Init(void); 

int main(void)
{
    HAL_Init();
    SystemClock_Config();
    
    // Inicialitzem la UART fent servir la funció de log.c
    // Això també configura el printf automàticament.
    MX_USART2_UART_Init();  
    
    MX_SPI2_Init();         // SPI initialization

    // --- Codi antic de test (comentat) ---
    // int res = prova();
    // if(res == 0) {
    //    prova_send("Hola nanosat!");
    // }
// TEST DIRECTE UART (Sense printf)
    /*char *test_msg = "TEST DIRECTE UART OK\r\n";//Prova 2
    HAL_UART_Transmit(&huart2, (uint8_t*)test_msg, strlen(test_msg), 1000);//Prova 2
    printf("\r\n");
    printf("*********************************\r\n");
    printf(" PoCat FLIGHT SOFTWARE\r\n");
    printf("*********************************\r\n");
    printf("Creant tasca OBC...\r\n ");
    */
    BaseType_t status = xTaskCreate(obc_task, "OBC", OBC_STACK_SIZE, NULL, OBC_PRIORITY, &obc_task_handle);

    if (status != pdPASS) {
        printf("[ERROR]\r\n");
        printf("ERROR CRITIC: No s'ha pogut crear la tasca OBC!\r\n");
        printf("CAUSA PROBABLE: Falta memoria Heap al FreeRTOS.\r\n");
        printf("SOLUCIO: Augmenta configTOTAL_HEAP_SIZE a FreeRTOSConfig.h\r\n");
        
        while(1) {
            // Error fatal - Bucle infinit
        }
    } else {
        printf("[OK]\r\n");
    }

    printf("Arrencant Scheduler...\r\n");
    vTaskStartScheduler();

    // Si arribem aquí, és que el Scheduler ha fallat (no hi ha RAM per la tasca Idle)
    printf("ERROR FATAL: vTaskStartScheduler ha retornat!\r\n");
 
    while (1)
    {
    }
 
    return 0;
}

static void SystemClock_Config(void) {

    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
    RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};

    __HAL_RCC_LSEDRIVE_CONFIG(RCC_LSEDRIVE_LOW);

    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI | RCC_OSCILLATORTYPE_LSE;
    RCC_OscInitStruct.LSEState = RCC_LSE_ON;
    RCC_OscInitStruct.HSIState = RCC_HSI_ON;
    RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
    RCC_OscInitStruct.PLL.PLLM = 1;
    RCC_OscInitStruct.PLL.PLLN = 10;
    RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV7;
    RCC_OscInitStruct.PLL.PLLQ = RCC_PLLQ_DIV2;
    RCC_OscInitStruct.PLL.PLLR = RCC_PLLR_DIV2;
    
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) {
        while(1);
    }

    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                                  RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK) {
        while(1);
    }

    PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_RTC | RCC_PERIPHCLK_USART2;
    PeriphClkInit.Usart2ClockSelection = RCC_USART2CLKSOURCE_PCLK1;
    PeriphClkInit.RTCClockSelection = RCC_RTCCLKSOURCE_LSE;
    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK) {
        while(1);
    }

    if (HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1) != HAL_OK) {
        while(1);
    }

    HAL_NVIC_SetPriority(SPI2_IRQn, 5, 0); 
    HAL_NVIC_EnableIRQ(SPI2_IRQn);
}


static void MX_SPI2_Init(void)
{
  hspi2.Instance = SPI2;
  hspi2.Init.Mode = SPI_MODE_MASTER;
  hspi2.Init.Direction = SPI_DIRECTION_2LINES;
  hspi2.Init.DataSize = SPI_DATASIZE_4BIT;
  hspi2.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi2.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi2.Init.NSS = SPI_NSS_HARD_OUTPUT;
  hspi2.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_2;
  hspi2.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi2.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi2.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi2.Init.CRCPolynomial = 7;
  hspi2.Init.CRCLength = SPI_CRC_LENGTH_DATASIZE;
  hspi2.Init.NSSPMode = SPI_NSS_PULSE_ENABLE;
  if (HAL_SPI_Init(&hspi2) != HAL_OK)
  {
    // Error_Handler(); 
  }
}