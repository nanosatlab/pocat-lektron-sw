/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * <h2><center>&copy; Copyright (c) 2022 STMicroelectronics.
  * All rights reserved.</center></h2>
  *
  * This software component is licensed by ST under BSD 3-Clause license,
  * the "License"; You may not use this file except in compliance with the
  * License. You may obtain a copy of the License at:
  *                        opensource.org/licenses/BSD-3-Clause
  *
  ******************************************************************************
  */

#include "main.h"
#include "cmsis_os.h"
#include "FreeRTOS.h"
#include "task.h"
#include "camerav2.h"
#include "comms.h"
#include "core_cm4.h" // Ya que el L476 es Cortex-M4


ADC_HandleTypeDef hadc1;

DAC_HandleTypeDef hdac1;

I2C_HandleTypeDef hi2c1;

RTC_HandleTypeDef hrtc;

SPI_HandleTypeDef hspi2;

TIM_HandleTypeDef htim5;
TIM_HandleTypeDef htim7;
TIM_HandleTypeDef htim16;
TIM_HandleTypeDef htim17;

UART_HandleTypeDef huart4;
DMA_HandleTypeDef hdma_uart4_rx;

osThreadId defaultTaskHandle;

uint8_t timer_counter = 0;		//TEST
uint8_t beacon_counter = 0;		//COMMS BEACON
uint8_t timeout_counter = 0; 	//COMMS TIMEOUT
uint8_t start_timer = false;	//TO SKIP STARTING TIMER IRQ
uint8_t stop_timer = false;		//TO SKIP STOPPING TIMER IRQ

TaskHandle_t FLASH_Handle;
TaskHandle_t OBC_Handle;
TaskHandle_t COMMS_Handle;
TaskHandle_t EPS_Handle;
TaskHandle_t PAYLOAD_Handle;
TaskHandle_t ADCS_Handle;
TaskHandle_t sTIM_Handle;
TaskHandle_t RFI_Handle;

QueueHandle_t FLASH_Queue;

EventGroupHandle_t xEventGroup;

TimerHandle_t xTimerObc;
TimerHandle_t xTimerComms;
TimerHandle_t xTimerAdcs;
TimerHandle_t xTimerEps;
TimerHandle_t xTimerPayload;
TimerHandle_t xTimerPhoto;
TimerHandle_t xTimerRF;
TimerHandle_t xTimerBeacon;

void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);
static void MX_I2C1_Init(void);
static void MX_RTC_Init(void);
static void MX_SPI2_Init(void);
static void MX_UART4_Init(void);
static void MX_TIM16_Init(void);
static void MX_ADC1_Init(void);
static void MX_DAC1_Init(void);
static void MX_TIM17_Init(void);
static void MX_TIM7_Init(void);
static void MX_TIM5_Init(void);
void StartDefaultTask(void const * argument);

void SystemClockConfig( void );
static void OBC_Task(void *params);
static void COMMS_Task(void *params);
static void ADCS_Task(void *params);
static void PAYLOAD_Task(void *params);
static void FLASH_Task(void *params);
static void EPS_Task(void *params);
static void sTIM_Task(void *params);
static void RFI_Task(void *params);

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */

HAL_Init();
  /* Configure the system clock */
  SystemClock_Config();

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_I2C1_Init();
  MX_RTC_Init();
  MX_SPI2_Init();
  MX_UART4_Init();
  MX_TIM16_Init();
  MX_ADC1_Init();
  MX_DAC1_Init();
  MX_TIM17_Init();
  MX_TIM7_Init();
  MX_TIM5_Init();


  /****TASK CREATION****/
  xTaskCreate( FLASH_Task,     "FLASH",   FLASH_STACK_SIZE, NULL,   FLASH_PRIORITY, &FLASH_Handle);
  //xTaskCreate( EPS_Task,         "EPS",     EPS_STACK_SIZE, NULL,   EPS_PRIORITY, &EPS_Handle  );
  //xTaskCreate( ADCS_Task,       "ADCS",    ADCS_STACK_SIZE, NULL,    ADCS_PRIORITY, &ADCS_Handle   );
  //xTaskCreate( OBC_Task,         "OBC",     OBC_STACK_SIZE, NULL,     OBC_PRIORITY, &OBC_Handle);
  xTaskCreate( COMMS_Task,     "COMMS",   COMMS_STACK_SIZE, NULL,   COMMS_PRIORITY, &COMMS_Handle  );
  //xTaskCreate( sTIM_Task,     "TIM",   sTIM_STACK_SIZE, NULL,   sTIM_PRIORITY, &sTIM_Handle  );

  xTaskCreate( PAYLOAD_Task, "PAYLOAD", PAYLOAD_STACK_SIZE, NULL, PAYLOAD_PRIORITY, &PAYLOAD_Handle);
  //xTaskCreate( RFI_Task,     "RFI",   PAYLOAD_STACK_SIZE, NULL,   PAYLOAD_PRIORITY, &RFI_Handle  );

  FLASH_Queue = xQueueCreate(10,sizeof(QueueData_t)); // QUEUE : FIFO buffer for controlling the writing on the memory flash

  xEventGroup = xEventGroupCreate();                  // EVENT GROUP : communication object used for blocking events

  /****SOFTWARE TIMERS CREATION****/

  //xTimerObc = xTimerCreate("TIMER OBC", pdMS_TO_TICKS(OBC_ACTIVE_PERIOD), false, NULL, ObcTimerCallback);
  xTimerComms = xTimerCreate("TIMER COMMS", pdMS_TO_TICKS(COMMS_ACTIVE_PERIOD), false, NULL, CommsTimerCallback);
  //xTimerAdcs = xTimerCreate("TIMER ADCS", pdMS_TO_TICKS(ADCS_ACTIVE_PERIOD), false, NULL, AdcsTimerCallback);
  //xTimerEps = xTimerCreate("TIMER EPS", pdMS_TO_TICKS(EPS_ACTIVE_PERIOD), false, NULL, EpsTimerCallback);

  xTimerPayload = xTimerCreate("TIMER PAYLOAD", pdMS_TO_TICKS(PAYLOAD_ACTIVE_PERIOD), false, NULL, PayloadTimerCallback);
  //xTimerRF = xTimerCreate("TIMER RF", pdMS_TO_TICKS(1000), false, NULL, RFTimerCallback);
  xTimerBeacon = xTimerCreate("TIMER BEACON", pdMS_TO_TICKS(INIT_BEACON_PERIOD), true, NULL, BeaconTimerCallback);


  vTaskStartScheduler();

  /* Create the thread(s) */
  /* definition and creation of defaultTask */
  osThreadDef(defaultTask, StartDefaultTask, osPriorityNormal, 0, 128);
  defaultTaskHandle = osThreadCreate(osThread(defaultTask), NULL);

  /* Start scheduler */
  osKernelStart();

  while (1){};
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  if (HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1) != HAL_OK)
  {
    Error_Handler();
  }
  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI|RCC_OSCILLATORTYPE_LSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.LSIState = RCC_LSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = 1;
  RCC_OscInitStruct.PLL.PLLN = 10;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV7;
  RCC_OscInitStruct.PLL.PLLQ = RCC_PLLQ_DIV2;
  RCC_OscInitStruct.PLL.PLLR = RCC_PLLR_DIV2;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }
  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief ADC1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_ADC1_Init(void)
{

  ADC_MultiModeTypeDef multimode = {0};
  ADC_ChannelConfTypeDef sConfig = {0};

  /** Common config
  */
  hadc1.Instance = ADC1;
  hadc1.Init.ClockPrescaler = ADC_CLOCK_ASYNC_DIV1;
  hadc1.Init.Resolution = ADC_RESOLUTION_12B;
  hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
  hadc1.Init.ScanConvMode = ADC_SCAN_DISABLE;
  hadc1.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
  hadc1.Init.LowPowerAutoWait = DISABLE;
  hadc1.Init.ContinuousConvMode = ENABLE;
  hadc1.Init.NbrOfConversion = 1;
  hadc1.Init.DiscontinuousConvMode = DISABLE;
  hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
  hadc1.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
  hadc1.Init.DMAContinuousRequests = DISABLE;
  hadc1.Init.Overrun = ADC_OVR_DATA_PRESERVED;
  hadc1.Init.OversamplingMode = DISABLE;
  if (HAL_ADC_Init(&hadc1) != HAL_OK)
  {
    Error_Handler();
  }
  /** Configure the ADC multi-mode
  */
  multimode.Mode = ADC_MODE_INDEPENDENT;
  if (HAL_ADCEx_MultiModeConfigChannel(&hadc1, &multimode) != HAL_OK)
  {
    Error_Handler();
  }
  /** Configure Regular Channel
  */
  sConfig.Channel = ADC_CHANNEL_5;
  sConfig.Rank = ADC_REGULAR_RANK_1;
  sConfig.SamplingTime = ADC_SAMPLETIME_2CYCLES_5;
  sConfig.SingleDiff = ADC_SINGLE_ENDED;
  sConfig.OffsetNumber = ADC_OFFSET_NONE;
  sConfig.Offset = 0;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }

}

/**
  * @brief DAC1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_DAC1_Init(void)
{


  DAC_ChannelConfTypeDef sConfig = {0};

  /** DAC Initialization
  */
  hdac1.Instance = DAC1;
  if (HAL_DAC_Init(&hdac1) != HAL_OK)
  {
    Error_Handler();
  }
  /** DAC channel OUT1 config
  */
  sConfig.DAC_SampleAndHold = DAC_SAMPLEANDHOLD_DISABLE;
  sConfig.DAC_Trigger = DAC_TRIGGER_NONE;
  sConfig.DAC_OutputBuffer = DAC_OUTPUTBUFFER_ENABLE;
  sConfig.DAC_ConnectOnChipPeripheral = DAC_CHIPCONNECT_DISABLE;
  sConfig.DAC_UserTrimming = DAC_TRIMMING_FACTORY;
  if (HAL_DAC_ConfigChannel(&hdac1, &sConfig, DAC_CHANNEL_1) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief I2C1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_I2C1_Init(void)
{

  hi2c1.Instance = I2C1;
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
  /** Configure Analogue filter
  */
  if (HAL_I2CEx_ConfigAnalogFilter(&hi2c1, I2C_ANALOGFILTER_ENABLE) != HAL_OK)
  {
    Error_Handler();
  }
  /** Configure Digital filter
  */
  if (HAL_I2CEx_ConfigDigitalFilter(&hi2c1, 0) != HAL_OK)
  {
    Error_Handler();
  }

}

/**
  * @brief RTC Initialization Function
  * @param None
  * @retval None
  */
static void MX_RTC_Init(void)
{

  RTC_TimeTypeDef sTime = {0};
  RTC_DateTypeDef sDate = {0};

  /** Initialize RTC Only
  */
  hrtc.Instance = RTC;
  hrtc.Init.HourFormat = RTC_HOURFORMAT_24;
  hrtc.Init.AsynchPrediv = 127;
  hrtc.Init.SynchPrediv = 255;
  hrtc.Init.OutPut = RTC_OUTPUT_DISABLE;
  hrtc.Init.OutPutRemap = RTC_OUTPUT_REMAP_NONE;
  hrtc.Init.OutPutPolarity = RTC_OUTPUT_POLARITY_HIGH;
  hrtc.Init.OutPutType = RTC_OUTPUT_TYPE_OPENDRAIN;
  if (HAL_RTC_Init(&hrtc) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initialize RTC and set the Time and Date
  */
  sTime.Hours = 0x0;
  sTime.Minutes = 0x0;
  sTime.Seconds = 0x0;
  sTime.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
  sTime.StoreOperation = RTC_STOREOPERATION_RESET;
  if (HAL_RTC_SetTime(&hrtc, &sTime, RTC_FORMAT_BCD) != HAL_OK)
  {
    Error_Handler();
  }
  sDate.WeekDay = RTC_WEEKDAY_MONDAY;
  sDate.Month = RTC_MONTH_JANUARY;
  sDate.Date = 0x1;
  sDate.Year = 0x0;

  if (HAL_RTC_SetDate(&hrtc, &sDate, RTC_FORMAT_BCD) != HAL_OK)
  {
    Error_Handler();
  }

}

/**
  * @brief SPI2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_SPI2_Init(void)
{

  /* SPI2 parameter configuration*/
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
    Error_Handler();
  }

}

/**
  * @brief TIM5 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM5_Init(void)
{

  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};

  htim5.Instance = TIM5;
  htim5.Init.Prescaler = 0;
  htim5.Init.CounterMode = TIM_COUNTERMODE_DOWN;
  htim5.Init.Period = 8000-1;
  htim5.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim5.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
  if (HAL_TIM_Base_Init(&htim5) != HAL_OK)
  {
    Error_Handler();
  }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim5, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim5, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }

}

/**
  * @brief TIM7 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM7_Init(void)
{


  TIM_MasterConfigTypeDef sMasterConfig = {0};

  htim7.Instance = TIM7;
  htim7.Init.Prescaler = 40000-1;
  htim7.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim7.Init.Period = 2000-1;
  htim7.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim7) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim7, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }

}

/**
  * @brief TIM16 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM16_Init(void)
{

  htim16.Instance = TIM16;
  htim16.Init.Prescaler = 8000 - 1;
  htim16.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim16.Init.Period = 10000 - 1;
  htim16.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim16.Init.RepetitionCounter = 0;
  htim16.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim16) != HAL_OK)
  {
    Error_Handler();
  }

}

/**
  * @brief TIM17 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM17_Init(void)
{

  htim17.Instance = TIM17;
  htim17.Init.Prescaler = 48000-1;
  htim17.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim17.Init.Period = 50000-1;
  htim17.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim17.Init.RepetitionCounter = 0;
  htim17.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim17) != HAL_OK)
  {
    Error_Handler();
  }

}

/**
  * @brief UART4 Initialization Function
  * @param None
  * @retval None
  */
static void MX_UART4_Init(void)
{

  huart4.Instance = UART4;
  huart4.Init.BaudRate = 115200;
  huart4.Init.WordLength = UART_WORDLENGTH_8B;
  huart4.Init.StopBits = UART_STOPBITS_1;
  huart4.Init.Parity = UART_PARITY_NONE;
  huart4.Init.Mode = UART_MODE_TX_RX;
  huart4.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart4.Init.OverSampling = UART_OVERSAMPLING_16;
  huart4.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart4.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&huart4) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * Enable DMA controller clock
  */
static void MX_DMA_Init(void)
{

  /* DMA controller clock enable */
  __HAL_RCC_DMA2_CLK_ENABLE();

  /* DMA interrupt init */
  /* DMA2_Channel5_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA2_Channel5_IRQn, 5, 0);
  HAL_NVIC_EnableIRQ(DMA2_Channel5_IRQn);

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_0|GPIO_PIN_2|GPIO_PIN_3|GPIO_PIN_9, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_3|GPIO_PIN_9|GPIO_PIN_11|GPIO_PIN_12
                          |GPIO_PIN_15, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0|GPIO_PIN_10|GPIO_PIN_11, GPIO_PIN_RESET);

  /*Configure GPIO pin : B1_Pin */
  GPIO_InitStruct.Pin = B1_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(B1_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : PC0 PC2 PC3 PC9 */
  GPIO_InitStruct.Pin = GPIO_PIN_0|GPIO_PIN_2|GPIO_PIN_3|GPIO_PIN_9;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pins : PA3 PA9 PA11 PA12
                           PA15 */
  GPIO_InitStruct.Pin = GPIO_PIN_3|GPIO_PIN_9|GPIO_PIN_11|GPIO_PIN_12
                          |GPIO_PIN_15;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pins : PA7 PA8 */
  GPIO_InitStruct.Pin = GPIO_PIN_7|GPIO_PIN_8;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pin : PC4 */
  GPIO_InitStruct.Pin = GPIO_PIN_4;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pins : PB0 PB10 PB11 */
  GPIO_InitStruct.Pin = GPIO_PIN_0|GPIO_PIN_10|GPIO_PIN_11;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure GPIO pin : PA10 */
  GPIO_InitStruct.Pin = GPIO_PIN_10;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pin : PB5 */
  GPIO_InitStruct.Pin = GPIO_PIN_5;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /* EXTI interrupt init*/
  HAL_NVIC_SetPriority(EXTI15_10_IRQn, 5, 0);
  HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);

}


static void OBC_Task(void *params) //To be reworked
{
	xTimerStart(xTimerObc,0);

	uint8_t currentState;

	OBC_Init();

	 for(;;)
	 {
		 Read_Flash(CURRENT_STATE_ADDR,&currentState,1);

		 switch(currentState)
		 {
	  	  	  case _NOMINAL:
	  	  		  OBC_Nominal();
	  	  		  break;

	  	  	  case _CONTINGENCY:
	  	  		  OBC_Contingency();
	  	  		  currentState = _NOMINAL;
	  	  		  break;

	  	  	  case _SUNSAFE:
	  	  		  OBC_Sunsafe();
	  	  		  break;

	  	  	  case _SURVIVAL:
	  	  		  OBC_Survival();
	  	  		  break;

	  	  	  default:
	  	  		  OBC_Survival();
	  	  		  break;
	  	  }
  }
}

static void COMMS_Task(void *params)
{
	//xTimerStart(xTimerBeacon,0);
	for(;;)
	{
		COMMS_StateMachine();
	}
}

static void ADCS_Task(void *params) //To be merged with internal
{
	xTimerStart(xTimerAdcs,0);

	uint32_t RX_ADCS_NOTIS;

	for(;;)
	{

		if (xTaskNotifyWait(0, 0xFFFFFFFF, &RX_ADCS_NOTIS, portMAX_DELAY) == pdPASS)
			{
				xTimerStart(xTimerAdcs,0);

				if((RX_ADCS_NOTIS & ADCS_TELEMETRY_NOTI) == ADCS_TELEMETRY_NOTI)
				{
					// Poll the Lateral Temperature Sensors
					// Store Lateral Temperature Sensors data on memory flash at TEMPLAT_ADDR

					// Poll the Gyroscope
					// Store Gyroscope data on memory flash at GYRO_ADDR

					// Poll the Photodiodes
					// Store Photodiodes data on memory flash at PHOTODIODES_ADDR

					// Poll the Magnetometers
					// Store Magnetometers data on memory flash at MAGNETOMETERS_ADDR

				}

				if((RX_ADCS_NOTIS & DETUMBLING_NOTI) == DETUMBLING_NOTI)
				{
					// Check Gyroscope--> Rotating?
							// If Rotating --> Bdot algotithm for Detumbling
				}

				if((RX_ADCS_NOTIS & CHECKROTATE_NOTI) == CHECKROTATE_NOTI) // Change CHECK_ROTATE_NOTI
				{
					// Compute temperature gradient between PQ faces
							// If Excessive Temperature Gradient --> ROTATE
				}

				if((RX_ADCS_NOTIS & POINTING_NOTI) == POINTING_NOTI)
				{
					// Nadir Pointing Algorithm
					xEventGroupSetBits(xEventGroup, ADCS_POINTINGDONE_EVENT);
				}

				if((RX_ADCS_NOTIS & STOP_POINTING_NOTI) == STOP_POINTING_NOTI)
				{
					// Stop Nadir Pointing Algorithm
				}

				if((RX_ADCS_NOTIS & SUNSAFE_NOTI) == SUNSAFE_NOTI)
				{
					// Rotate directly
				}
			}
	}
}

static void PAYLOAD_Task(void *params)
{

	uint32_t RX_PAYLOAD_NOTIS;

	uint8_t resolution = 0x00;      //0x11 o 0x00
	uint8_t compressibility = 0xFF; // 0x00 --- 0xFF
	uint8_t info[50];

	for(;;)
	{

		if (xTaskNotifyWait(0, 0xFFFFFFFF, &RX_PAYLOAD_NOTIS, portMAX_DELAY) == pdPASS)
			{
				if((RX_PAYLOAD_NOTIS & ACTIVATE_PAYLOAD)==ACTIVATE_PAYLOAD)
				{
					initCam(huart4, resolution, compressibility, info);
					getPhoto(huart4, info);
					xTaskNotifyStateClear(PAYLOAD_Handle);
				}
			}
	}
}

static void RFI_Task(void *params) //To be merged with internal
{
	uint32_t RX_PAYLOAD_NOTIS;
	for (;;)
	{
		if (xTaskNotifyWait(0, 0xFFFFFFFF, &RX_PAYLOAD_NOTIS, portMAX_DELAY) == pdPASS)
			{
				if((RX_PAYLOAD_NOTIS & ACTIVATE_PAYLOAD)==ACTIVATE_PAYLOAD)
				{
					xTimerStart(xTimerRF,0);

					xEventGroupWaitBits(xEventGroup, PAYLOAD_TIMERF_EVENT, 0, true, portMAX_DELAY);
					Init_RFIPayload();
					Start_RFIPayload();
					Check_RFIPayload();
					Measure_loop();
					Stop_RFIPayload();

					xTimerStop(xTimerRF,0);
				}
			}
		vTaskDelay(pdMS_TO_TICKS(100));
	}
}

static void FLASH_Task(void *params) // GateKeeper Task of Writing on Flash Memory
{
	QueueData_t RxQueueData;
	BaseType_t xQueueStatus;
	UBaseType_t numMessageWaiting;
	int i;

	for(;;)
	{
		/*
		 * The peek function is an attempt to read from the queue. If there is
		 * some data item on the queue it will read the data but it does NOT
		 * extract the data from the queue. So the data will still be on the queue
		 * despite the fact that we have read from it. On the other hand,
		 * if the queue is empty it will block the task until it is not empty.
		 */

		xQueueStatus = xQueuePeek(FLASH_Queue,&RxQueueData,portMAX_DELAY);


		numMessageWaiting = uxQueueMessagesWaiting(FLASH_Queue); // Get the number of messages waiting on the queue.

		for(i=0;i<numMessageWaiting;i++) // Iterate according to the number of messages on the queue.
		{
			xQueueStatus = xQueueReceive(FLASH_Queue,&RxQueueData,100); // Read and extract the data item on the FRONT of the queue (FIFO).

			if(xQueueStatus==pdPASS)
			{
				if(RxQueueData.arrayLength<2000)  // If the data if less than 2000 bytes write it directly.
				{
					Write_Flash(RxQueueData.addr,RxQueueData.pointer,RxQueueData.arrayLength);
				}

				else
					/*
					 * If data exceeds 2000 bytes the data will be splitted into chunks
					 * not exceeding 2000 bytes. And those pieces will be written on the
					 * flash separately.
					 */
				{
					uint8_t n = ceil((float)RxQueueData.arrayLength / 2000); // Number of chunks
					uint8_t i = 0;   // Chunk number to iterate.
					uint16_t length; // Length of the chunk.
										// Spoiler : last piece = rest of bytes.
										//           rest of the pieces = 2000  bytes.
					uint32_t addr;   // First address of each piece.
					uint8_t * ptr;   // Pointer pointing to the first byte of the piece.
					while(i<n)
					{
						if(i==n-1)
						{

							/*
							 * If we are dealing with the last chunk of data,
							 * its length must be the overall length of what
							 * has to be written on the flash minus the number of
							 * chunks of 2000 bytes that had before.
							 */

							length = (uint16_t)(RxQueueData.arrayLength - 2000*i);
						}

						else
						{
							// The rest of the parts are 2000 bytes long.

							length = (uint16_t)2000;
						}

						/*
						 * One memory address can store only one byte.
						 * Therefore the address of the following data piece
						 * must be the address of the previous piece plus 2000.
						 */

						addr = (uint32_t)(RxQueueData.addr + i*2000);

						/*
						 * There are a lot of stupidly confusing  explanations regarding
						 * pointers. But it is basically the address on the memory (SRAM
						 * in our case) where the data at which is pointing to is stored.
						 * So the same logic for the flash memory address applies here.
						 */

						ptr = (uint8_t *)(RxQueueData.pointer + i*2000);

						Write_Flash(addr,ptr,length); // Write the piece of data.

						i++;
					}

				}
			}
		}

		xQueueReset(FLASH_Queue); // Reset a queue back to its original empty state (it should be already empty)
	}
}

static void EPS_Task(void *params) //To be merged with internal
{
	xTimerStart(xTimerEps,0);

	uint32_t RX_EPS_NOTIS;

	for(;;)
	{
		if (xTaskNotifyWait(0, 0xFFFFFFFF, &RX_EPS_NOTIS, portMAX_DELAY)==pdPASS)
		{
			if((RX_EPS_NOTIS & EPS_BATTERY_NOTI) == EPS_BATTERY_NOTI)
			{
				uint8_t state, prev_state, batt, nom, low, crit;

				// Read Battery Capacity

				batt = 95; // arbitrary value by now

				Read_Flash(CURRENT_STATE_ADDR, &state, 1);
				Read_Flash(PREVIOUS_STATE_ADDR, &prev_state, 1);
				//Read_Flash(NOMINAL_ADDR, &nom, 1);
				//Read_Flash(LOW_ADDR, &low, 1);
				//Read_Flash(CRITICAL_ADDR, &crit, 1);

				Send_to_WFQueue(&state, 1, PREVIOUS_STATE_ADDR, EPSsender); // store the current state as previous

				switch(state)
				{
					case _INIT :
					{
						if(batt>=nom){state = _NOMINAL;}
						else{state = _CONTINGENCY;}
					}

					case _NOMINAL :
						{
							if(batt>=nom){state = _NOMINAL;}
							else{state = _CONTINGENCY;}
						}

					case _CONTINGENCY :
						{
							if(batt>=nom){state = _NOMINAL;}
							else if(batt<low){state = _SUNSAFE;}
							else{state = _CONTINGENCY;}
						}
					case _SUNSAFE :
						{
							if(batt>=low){state = _CONTINGENCY;}
							else if(batt<crit){state = _SURVIVAL;}
							else{state = _SUNSAFE;}
						}
					case _SURVIVAL :
						{
							if(batt>=crit){state = _SUNSAFE;} // Through telecommands (SURVIVAL-->SUNSAFE)
																		// Hear after sleep
							else{state = _SURVIVAL;}
						}
					default:{state = prev_state;}
				}

				Send_to_WFQueue(&state, 1, CURRENT_STATE_ADDR, EPSsender);
			}

			if((RX_EPS_NOTIS & EPS_HEATER_NOTI) == EPS_HEATER_NOTI)
			{
				// If we are charging the battery
					// Read the battery temperature sensor
						// If (temp<threshold && Heater OFF) --> Heater ON
						// Else IF (temp>threshold && Heater ON) --> Heater OFF
			}

			if((RX_EPS_NOTIS & EPS_TELEMETRY_NOTI) == EPS_TELEMETRY_NOTI)
			{
				// Read battery temperature sensor
				// Store battery temperature sensor on memory flash at BATT_TEMP_ADDR

				// Read battery capacity
				// Store battery capacity on memory flash at BATT_CAP_ADDR

			}
		}
	}
}

static void sTIM_Task(void *params)
{
	uint32_t sTIM_RX_NOTIS; // Notifications received by sTIM task

	/* Each index of the following arrays correspond to each task controlled by the software timer tasks.
	 * Like this, index 0 refers to OBC, 1 to COMMS, 2 to ADCS, 3 to PAYLOAD and 4 to EPS.*/

	TaskHandle_t        vTask[5]={OBC_Handle,COMMS_Handle,ADCS_Handle,PAYLOAD_Handle, EPS_Handle}; // Array of task handlers
	TimerHandle_t      vTimer[5]={xTimerObc,xTimerComms,xTimerAdcs,xTimerPayload,xTimerEps}; // Array of software timers
	uint32_t    vActivePeriod[5]={OBC_ACTIVE_PERIOD, COMMS_ACTIVE_PERIOD, ADCS_ACTIVE_PERIOD, PAYLOAD_ACTIVE_PERIOD, EPS_ACTIVE_PERIOD}; // Array of task active periods
	uint32_t       vSusPeriod[5]={OBC_SUSPENDED_PERIOD, COMMS_SUSPENDED_PERIOD, ADCS_SUSPENDED_PERIOD, PAYLOAD_SUSPENDED_PERIOD, EPS_SUSPENDED_PERIOD}; // Array of task inactive periods
	eTaskState     vTaskState[5]={0,0,0,0,0}; // Array of task states
	UBaseType_t vTaskPriority[5]={0,0,0,0,0}; // Array of task priorities
	uint8_t            vCount[5]={0,0,0,0,0}; /* Array that represents the period of the timer
													* Timer[i] triggered & vCount[i]=0 --> Active period has finished
													* Timer[i] triggered & vCount[i]=1 --> Inactive period has finished*/
	uint8_t sender; // Index determining the task related to the timer that has triggered
	uint8_t i; // For loop purposes
	uint8_t maxReadyIndex; // Index of the maximum priority among the ready state tasks
	uint8_t maxReadyPriority = 0; // Maximum priority among the ready state tasks

	for(;;)
	{
		if(xTaskNotifyWait(0, 0xFFFF, &sTIM_RX_NOTIS, portMAX_DELAY)==pdPASS) // According to the received timer ID work with the sender index.
			{
				if((sTIM_RX_NOTIS & sTIM_OBC_NOTI) == sTIM_OBC_NOTI){sender = OBC;}
				if((sTIM_RX_NOTIS & sTIM_COMMS_NOTI) == sTIM_COMMS_NOTI){sender = COMMS;}
				if((sTIM_RX_NOTIS & sTIM_ADCS_NOTI) == sTIM_ADCS_NOTI){sender = ADCS;}
				if((sTIM_RX_NOTIS & sTIM_PAYLOAD_NOTI) == sTIM_PAYLOAD_NOTI){sender = PAYLOAD;}
				if((sTIM_RX_NOTIS & sTIM_EPS_NOTI) == sTIM_EPS_NOTI){sender = EPS;}
			}

		if(vCount[sender]==0) // Active period finished
		{
			vCount[sender]++; // Next timer trigger will be considered as the end of inactive period
			xTimerChangePeriod(vTimer[sender],vSusPeriod[sender],0); // Change the period of the timer from active to inactive
			vTaskSuspend(vTask[sender]); // Suspend the task
			xTimerStart(vTimer[sender],0);
		}

		else // Suspended period finished
		{
			vCount[sender]--; // Next timer trigger will be considered as the end of active period
			xTimerChangePeriod(vTimer[sender],vActivePeriod[sender],0);// Change the period of the timer from inactive to active
			vTaskResume(vTask[sender]); // Resume the task
		}

		for(i=0;i<(sizeof(vCount)/sizeof(uint8_t));i++) /* Loop for determining:
																- The current state of the tasks.
																- The current priority of the tasks.
																- The maximum priority of the tasks. */
		{
			vTaskState[i]=eTaskGetState(vTask[i]);
			vTaskPriority[i]=uxTaskPriorityGet(vTask[i]);

			if((vTaskState[i]==eReady)&&(vTaskPriority[i]>maxReadyPriority))
			{
				maxReadyPriority = vTaskPriority[i];
			}
		}

		for(i=0;i<(sizeof(vCount)/sizeof(uint8_t));i++) /* Loop that stops every timer that is
														*  not the one corresponding to the maximum priority
														*  task in ready state. */
		{
			if((vTaskPriority[i]!=maxReadyPriority) && (vTaskState[i]!=eSuspended) && (vCount[i]==0))
			{
				xTimerChangePeriod(vTimer[i],vActivePeriod[i],0);
				xTimerStop(vTimer[i],0);
			}

			else
			{
				maxReadyIndex = i;
			}
		}

		xTimerStart(vTimer[maxReadyIndex],0); // Start the timer for the maximum priority among tasks in ready state.
	}
}


void Init_Peripherals()
{
	MX_GPIO_Init();
	MX_DMA_Init();
	MX_I2C1_Init();
	MX_RTC_Init();
	MX_SPI2_Init();
	MX_UART4_Init();
	MX_TIM16_Init();
	MX_ADC1_Init();
	MX_DAC1_Init();
	MX_TIM17_Init();
	MX_TIM7_Init();
	MX_TIM5_Init();
}

void SystemClockConfig( void )
{

  RCC_OscInitTypeDef RCC_OscInitStruct;
  RCC_ClkInitTypeDef RCC_ClkInitStruct;
  RCC_PeriphCLKInitTypeDef PeriphClkInit;


  __HAL_RCC_LSEDRIVE_CONFIG(RCC_LSEDRIVE_LOW);


  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI|RCC_OSCILLATORTYPE_LSE;
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
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    //assert_failed(__FILE__, __LINE__);
	  assert_param( FAIL );

  }

  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK)
  {
    //assert_failed(__FILE__, __LINE__);
	  assert_param( FAIL );
  }

  PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_RTC|RCC_PERIPHCLK_USART2;
  PeriphClkInit.Usart2ClockSelection = RCC_USART2CLKSOURCE_PCLK1;
  PeriphClkInit.RTCClockSelection = RCC_RTCCLKSOURCE_LSE;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
  {
    //assert_failed(__FILE__, __LINE__);
	  assert_param( FAIL );
  }


  if (HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1) != HAL_OK)
  {
    //assert_failed(__FILE__, __LINE__);
	  assert_param( FAIL );
  }
    //HAL_SYSTICK_Config(HAL_RCC_GetHCLKFreq()/1000);
    //HAL_SYSTICK_CLKSourceConfig(SYSTICK_CLKSOURCE_HCLK);
    //HAL_NVIC_SetPriority(SysTick_IRQn, 0, 0);

  HAL_NVIC_SetPriority(SPI2_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(SPI2_IRQn);
}


void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim){

	if(htim == &htim16)
	{
		HAL_TIM_Base_Stop_IT(&htim16);
		HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, GPIO_PIN_RESET);
	}

	else if (htim == &htim17){
		if (beacon_counter >= 1){	// We TX beacon once every minute
			//tx_beacon();
			beacon_counter = 0;
			HAL_TIM_Base_Stop_IT(&htim17);
		} else{
			beacon_counter = beacon_counter + 1;
		}
	}
}


void Stop_timer_16(void){
	stop_timer = true;
	HAL_TIM_Base_Stop_IT(&htim16);
	//manualDelayMS(1);
}

void Start_timer_16(void){
	start_timer = true;
	HAL_TIM_Base_Start_IT(&htim16);
	//manualDelayMS(1);
}

/**
  * @brief  Function implementing the defaultTask thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartDefaultTask */
void StartDefaultTask(void const * argument)
{
  /* Infinite loop */
  for(;;)
  {
    osDelay(1);
  }

}


/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
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
int fputc(int ch, FILE *f)
{
    ITM_SendChar(ch);
    return ch;
}

#ifdef  USE_FULL_ASSERT
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

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
