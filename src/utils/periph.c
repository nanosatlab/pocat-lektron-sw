/**
 * @file periph.c
 * @brief Peripheral handle definitions and initialization helpers.
 * @author Guillermo O'Tuama Pascual
 * @date 2026-01-20
 *
 * Owns the global STM32 HAL peripheral handles and centralizes both the
 * initial peripheral bring-up path and the clock-dependent reconfiguration
 * used after runtime frequency switches.
 */


// TODO
/* @details This is the main module. It performs system initialization and starts the FreeRTOS scheduler. It:
 * - Initializes the HAL library
 * - Configures the system clock
 * - Initializes peripherals:
 *   - GPIO
 *   - TIM5 (used as a microsecond timebase)
 *       - provides micros() in stm32_radiolib_hal.cpp
 *   - TIM2 (used for tone generation in stm32_radiolib_hal.cpp)
 *       - will probably not be needed for SX1262
 *   - SPI1 (communication with SX1262)
 *   - USART3 (debug output)
 *   - IWDG (independent watchdog)
 * - It then finally creates the OBC task and starts the FreeRTOS scheduler.
 */

#include "periph.h"

// TODO
/* Provided by main.c and stm32l4xx_hal_msp.c. */
void Error_Handler(void);
void HAL_TIM_MspPostInit(TIM_HandleTypeDef *htim);

TIM_HandleTypeDef htim2;
TIM_HandleTypeDef htim5;
SPI_HandleTypeDef hspi2;
UART_HandleTypeDef huart3;
IWDG_HandleTypeDef hiwdg;
RTC_HandleTypeDef hrtc;
ADC_HandleTypeDef hadc1;

/* ---- Private Helpers ---- */

static uint32_t tim5_prescaler_for_freq(ClockFreq_t freq);
static uint32_t spi2_prescaler_for_freq(ClockFreq_t freq);
static uint32_t adc1_prescaler_for_freq(ClockFreq_t freq);
static void periph_gpio_init(void);
static void periph_tim2_init(void);
static void periph_tim5_init(ClockFreq_t freq);
static void periph_spi2_init(ClockFreq_t freq);
static void periph_iwdg_init(void);
static void periph_usart3_init(void);
static void periph_rtc_init(void);
static void periph_adc1_init(ClockFreq_t freq);

/* ---- Public API ---- */
void periph_init_for_freq(ClockFreq_t freq)
{
    periph_gpio_init();
    periph_tim5_init(freq);
    periph_tim2_init();
    periph_spi2_init(freq);
    periph_iwdg_init();
    periph_usart3_init();
    periph_rtc_init();
    periph_adc1_init(freq);
}

void periph_reconfigure_for_freq(ClockFreq_t freq)
{
    __HAL_TIM_SET_PRESCALER(&htim5, tim5_prescaler_for_freq(freq));
    HAL_TIM_GenerateEvent(&htim5, TIM_EVENTSOURCE_UPDATE);

    if (HAL_UART_Init(&huart3) != HAL_OK) {
        Error_Handler();
    }

    HAL_SPI_DeInit(&hspi2);
    hspi2.Init.BaudRatePrescaler = spi2_prescaler_for_freq(freq);
    if (HAL_SPI_Init(&hspi2) != HAL_OK) {
        Error_Handler();
    }

    HAL_ADC_DeInit(&hadc1);
    hadc1.Init.ClockPrescaler = adc1_prescaler_for_freq(freq);
    if (HAL_ADC_Init(&hadc1) != HAL_OK) {
        Error_Handler();
    }

    if (HAL_ADCEx_Calibration_Start(&hadc1, ADC_SINGLE_ENDED) != HAL_OK) {
        Error_Handler();
    }
}

/* ---- Private Helpers ---- */

static uint32_t tim5_prescaler_for_freq(ClockFreq_t freq)
{
    switch (freq) {
        case CLK_FREQ_80MHZ: return 79u; /* 80 MHz / (79 + 1) = 1 MHz */
        case CLK_FREQ_8MHZ:  return 7u;  /* 8 MHz / (7 + 1) = 1 MHz */
        case CLK_FREQ_2MHZ:  return 1u;  /* 2 MHz / (1 + 1) = 1 MHz */
        default:             return 79u;
    }
}

static uint32_t spi2_prescaler_for_freq(ClockFreq_t freq)
{
    switch (freq) {
        case CLK_FREQ_80MHZ: return SPI_BAUDRATEPRESCALER_8; /* 10 MHz */
        case CLK_FREQ_8MHZ:  return SPI_BAUDRATEPRESCALER_2; /* 4 MHz  */
        case CLK_FREQ_2MHZ:  return SPI_BAUDRATEPRESCALER_2; /* 1 MHz  */
        default:             return SPI_BAUDRATEPRESCALER_8;
    }
}

static uint32_t adc1_prescaler_for_freq(ClockFreq_t freq)
{
    switch (freq) {
        case CLK_FREQ_80MHZ: return ADC_CLOCK_SYNC_PCLK_DIV4; /* 20 MHz */
        case CLK_FREQ_8MHZ:  return ADC_CLOCK_SYNC_PCLK_DIV1; /* 8 MHz  */
        case CLK_FREQ_2MHZ:  return ADC_CLOCK_SYNC_PCLK_DIV1; /* 2 MHz  */
        default:             return ADC_CLOCK_SYNC_PCLK_DIV4;
    }
}


/**
  * @brief GPIO Initialization Function
  * @todo Comment function implementation
  */
static void periph_gpio_init(void)
{
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();

     /* Radio GPIO pins (PC9/NRST, PA8/BUSY, PA10/DIO1, PB12/NSS) are
     configured by RadioLib through the HAL — do not touch them here. */
}

// QUITAAAAR AHHORA LO TENGO SOLO PARAAA PROBAR
/**
  * @brief TIM2 Initialization Function
  * @details Configured for PWM generation, used for tone generation in stm32_radiolib_hal.cpp.
  * @note It will probably not be needed for SX1262 operation but it was added for completeness.
  * If not used, it should be removed.
  * @todo Remove if not used.
  */
static void periph_tim2_init(void)
{
    TIM_MasterConfigTypeDef master_config = {0};
    TIM_OC_InitTypeDef output_compare_config = {0};

    htim2.Instance = TIM2;
    htim2.Init.Prescaler = 0;
    htim2.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim2.Init.Period = 0xFFFFFFFFu;
    htim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    if (HAL_TIM_PWM_Init(&htim2) != HAL_OK) {
        Error_Handler();
    }

    master_config.MasterOutputTrigger = TIM_TRGO_RESET;
    master_config.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
    if (HAL_TIMEx_MasterConfigSynchronization(&htim2, &master_config) != HAL_OK) {
        Error_Handler();
    }

    output_compare_config.OCMode = TIM_OCMODE_PWM1;
    output_compare_config.Pulse = 0;
    output_compare_config.OCPolarity = TIM_OCPOLARITY_HIGH;
    output_compare_config.OCFastMode = TIM_OCFAST_DISABLE;
    if (HAL_TIM_PWM_ConfigChannel(&htim2, &output_compare_config, TIM_CHANNEL_1) != HAL_OK) {
        Error_Handler();
    }

    HAL_TIM_MspPostInit(&htim2);
}

/**
  * @brief TIM5 Initialization Function
  * @details TIM5 is used as a microsecond timebase. 
  * - TIM5 is clocked from APB1. With the APB1 prescaler set to 1 in SystemClock_Config(),
  *   the timer input clock is 80 MHz.
  * - TIM5 prescaler is set to 80-1, so timer ticks every 1 microsecond (80 MHz / 80 = 1 MHz).
  */
static void periph_tim5_init(ClockFreq_t freq)
{
    TIM_ClockConfigTypeDef clock_source_config = {0};
    TIM_MasterConfigTypeDef master_config = {0};

    htim5.Instance = TIM5;
    htim5.Init.Prescaler = tim5_prescaler_for_freq(freq);
    htim5.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim5.Init.Period = 0xFFFFFFFFu;
    htim5.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim5.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
    if (HAL_TIM_Base_Init(&htim5) != HAL_OK) {
        Error_Handler();
    }

    clock_source_config.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
    if (HAL_TIM_ConfigClockSource(&htim5, &clock_source_config) != HAL_OK) {
        Error_Handler();
    }

    master_config.MasterOutputTrigger = TIM_TRGO_RESET;
    master_config.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
    if (HAL_TIMEx_MasterConfigSynchronization(&htim5, &master_config) != HAL_OK) {
        Error_Handler();
    }

    if (HAL_TIM_Base_Start(&htim5) != HAL_OK) {
        Error_Handler();
    }
}

/**
* @brief SPI2 Initialization Function
* @details Initializes SPI2 as an SPI master (full-duplex) for communication with the SX1262.
*  - 4-bit frames
*  - Most significant bit (MSB) first
*  - Clock polarity low, clock phase 1st edge (SPI mode 0)
*  - Hardware NSS output (NSS managed by SPI peripheral)
*  - NSS pulse mode enabled
*  - Baud rate prescaler set to 2 (SPI clock = fPCLK / 2, where fPCLK is the APB1 clock)
* @note Requires SPI2_NSS pin to be configured on the correct AF pin.
*/
static void periph_spi2_init(ClockFreq_t freq)
{
    hspi2.Instance = SPI2;
    hspi2.Init.Mode = SPI_MODE_MASTER;
    hspi2.Init.Direction = SPI_DIRECTION_2LINES;
    hspi2.Init.DataSize = SPI_DATASIZE_8BIT;
    hspi2.Init.CLKPolarity = SPI_POLARITY_LOW;
    hspi2.Init.CLKPhase = SPI_PHASE_1EDGE;
    hspi2.Init.NSS = SPI_NSS_SOFT;
    hspi2.Init.BaudRatePrescaler = spi2_prescaler_for_freq(freq);
    hspi2.Init.FirstBit = SPI_FIRSTBIT_MSB;
    hspi2.Init.TIMode = SPI_TIMODE_DISABLE;
    hspi2.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
    hspi2.Init.CRCPolynomial = 7;
    hspi2.Init.CRCLength = SPI_CRC_LENGTH_DATASIZE;
    hspi2.Init.NSSPMode = SPI_NSS_PULSE_DISABLE;
    if (HAL_SPI_Init(&hspi2) != HAL_OK) {
        Error_Handler();
    }
}


/**
  * @brief IWDG Initialization Function
  * @details The watchdog is currently set to the maximum prescaler and reload value for debugging purposes.
  * This results in a countdown period of approximately 30-35 seconds.
  */
static void periph_iwdg_init(void)
{
    hiwdg.Instance = IWDG;
    hiwdg.Init.Prescaler = IWDG_PRESCALER_256;
    hiwdg.Init.Window = 4095;
    hiwdg.Init.Reload = 4095;
    if (HAL_IWDG_Init(&hiwdg) != HAL_OK) {
        Error_Handler();
    }
}

/**
  * @brief USART3 Initialization Function
  * @details USART3 is configured for debug output using printf() at 115200 baud.
  */
static void periph_usart3_init(void)
{
    huart3.Instance = USART3;
    huart3.Init.BaudRate = 115200;
    huart3.Init.WordLength = UART_WORDLENGTH_8B;
    huart3.Init.StopBits = UART_STOPBITS_1;
    huart3.Init.Parity = UART_PARITY_NONE;
    huart3.Init.Mode = UART_MODE_TX_RX;
    huart3.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart3.Init.OverSampling = UART_OVERSAMPLING_16;
    huart3.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
    huart3.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
    if (HAL_UART_Init(&huart3) != HAL_OK) {
        Error_Handler();
    }
}

/**
  * @brief RTC Initialization Function
  * @param None
  * @retval None
  */
static void periph_rtc_init(void)
{
    RTC_TimeTypeDef time = {0};
    RTC_DateTypeDef date = {0};

    hrtc.Instance = RTC;
    hrtc.Init.HourFormat = RTC_HOURFORMAT_24;
    hrtc.Init.AsynchPrediv = 127;
    hrtc.Init.SynchPrediv = 255;
    hrtc.Init.OutPut = RTC_OUTPUT_DISABLE;
    hrtc.Init.OutPutRemap = RTC_OUTPUT_REMAP_NONE;
    hrtc.Init.OutPutPolarity = RTC_OUTPUT_POLARITY_HIGH;
    hrtc.Init.OutPutType = RTC_OUTPUT_TYPE_OPENDRAIN;
    if (HAL_RTC_Init(&hrtc) != HAL_OK) {
        Error_Handler();
    }

    /* Preserve RTC date/time across resets once the backup flag is set. */
    if (HAL_RTCEx_BKUPRead(&hrtc, RTC_BKP_DR0) != 0x32F2u) {
        time.Hours = 0x0;
        time.Minutes = 0x0;
        time.Seconds = 0x0;
        time.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
        time.StoreOperation = RTC_STOREOPERATION_RESET;
        if (HAL_RTC_SetTime(&hrtc, &time, RTC_FORMAT_BCD) != HAL_OK) {
            Error_Handler();
        }

        date.WeekDay = RTC_WEEKDAY_MONDAY;
        date.Month = RTC_MONTH_JANUARY;
        date.Date = 0x1;
        date.Year = 0x0;
        if (HAL_RTC_SetDate(&hrtc, &date, RTC_FORMAT_BCD) != HAL_OK) {
            Error_Handler();
        }

        HAL_RTCEx_BKUPWrite(&hrtc, RTC_BKP_DR0, 0x32F2u);
    }
}

/**
  * @brief ADC1 Initialization Function
  * @details Configures ADC1 for reading the internal temperature sensor.
  * - 12-bit resolution
  * - Single conversion mode
  * - Software trigger
  * - Internal temperature sensor channel (ADC_CHANNEL_TEMPSENSOR)
  */
static void periph_adc1_init(ClockFreq_t freq)
{
    hadc1.Instance = ADC1;
    hadc1.Init.ClockPrescaler = adc1_prescaler_for_freq(freq);
    hadc1.Init.Resolution = ADC_RESOLUTION_12B;
    hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
    hadc1.Init.ScanConvMode = ADC_SCAN_DISABLE;
    hadc1.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
    hadc1.Init.LowPowerAutoWait = DISABLE;
    hadc1.Init.ContinuousConvMode = DISABLE;
    hadc1.Init.NbrOfConversion = 1;
    hadc1.Init.DiscontinuousConvMode = DISABLE;
    hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
    hadc1.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
    hadc1.Init.DMAContinuousRequests = DISABLE;
    hadc1.Init.Overrun = ADC_OVR_DATA_OVERWRITTEN;
    hadc1.Init.OversamplingMode = DISABLE;
    if (HAL_ADC_Init(&hadc1) != HAL_OK) {
        Error_Handler();
    }

    if (HAL_ADCEx_Calibration_Start(&hadc1, ADC_SINGLE_ENDED) != HAL_OK) {
        Error_Handler();
    }
}
