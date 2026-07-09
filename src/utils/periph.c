/**
 * @file periph.c
 * @brief Peripheral handle definitions and initialization helpers.
 * @details
 * Defines the global STM32 HAL peripheral handles declared in periph.h and
 * centralizes board peripheral initialization. The module initializes GPIO,
 * TIM5, TIM2, SPI2, IWDG, UART4, RTC, and ADC1, and provides reconfiguration
 * for peripherals whose timing depends on the selected system clock.
 * @author Guillermo O'Tuama Pascual
 * @date 2026-01-20
 */

#include "periph.h"

// TODO
/* Provided by main.c and stm32l4xx_hal_msp.c. */
void Error_Handler(void);
void HAL_TIM_MspPostInit(TIM_HandleTypeDef *htim);

TIM_HandleTypeDef htim2;
TIM_HandleTypeDef htim5;
SPI_HandleTypeDef hspi2;
UART_HandleTypeDef huart4;
IWDG_HandleTypeDef hiwdg;
RTC_HandleTypeDef hrtc;
ADC_HandleTypeDef hadc1;
I2C_HandleTypeDef hi2c1;

static uint32_t tim5_prescaler_for_freq(ClockFreq_t freq);
static uint32_t spi2_prescaler_for_freq(ClockFreq_t freq);
static uint32_t adc1_prescaler_for_freq(ClockFreq_t freq);
static void periph_gpio_init(void);
static void periph_tim2_init(void);
static void periph_tim5_init(ClockFreq_t freq);
static void periph_spi2_init(ClockFreq_t freq);
static void periph_iwdg_init(void);
static void periph_uart4_init(void);
static void periph_rtc_init(void);
static void periph_adc1_init(ClockFreq_t freq);
static void periph_i2c1_init(void);

void periph_init_for_freq(ClockFreq_t freq)
{
    periph_gpio_init();
    periph_tim5_init(freq);
    periph_tim2_init();
    periph_spi2_init(freq);
    periph_iwdg_init();
    periph_uart4_init();
    periph_rtc_init();
    periph_adc1_init(freq);
    periph_i2c1_init();
}

void periph_reconfigure_for_freq(ClockFreq_t freq)
{
    __HAL_TIM_SET_PRESCALER(&htim5, tim5_prescaler_for_freq(freq));
    HAL_TIM_GenerateEvent(&htim5, TIM_EVENTSOURCE_UPDATE);

    if (HAL_UART_Init(&huart4) != HAL_OK) {
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

HAL_StatusTypeDef adc_read_channel(uint32_t channel, uint32_t *result)
{
    ADC_ChannelConfTypeDef sConfig = {0};

    sConfig.Channel = channel;
    sConfig.Rank = ADC_REGULAR_RANK_1;
    sConfig.SamplingTime = ADC_SAMPLETIME_640CYCLES_5;
    sConfig.SingleDiff = ADC_SINGLE_ENDED;
    sConfig.OffsetNumber = ADC_OFFSET_NONE;
    sConfig.Offset = 0;
    if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK) {
        return HAL_ERROR;
    }

    HAL_ADC_Start(&hadc1);
    if (HAL_ADC_PollForConversion(&hadc1, 100) != HAL_OK) {
        return HAL_TIMEOUT;
    }
    *result = HAL_ADC_GetValue(&hadc1);
    HAL_ADC_Stop(&hadc1);

    return HAL_OK;
}

/**
 * @brief Select the TIM5 prescaler for the configured system clock.
 *
 * TIM5 is used as a microsecond timebase, so the prescaler is chosen to keep
 * the timer counter running at 1 MHz for each supported system clock.
 *
 * @param freq System clock selection.
 * @return TIM5 prescaler value.
 */
static uint32_t tim5_prescaler_for_freq(ClockFreq_t freq)
{
    switch (freq) {
        case CLK_FREQ_80MHZ: return 79u; /* 80 MHz / (79 + 1) = 1 MHz */
        case CLK_FREQ_8MHZ:  return 7u;  /* 8 MHz / (7 + 1) = 1 MHz */
        case CLK_FREQ_2MHZ:  return 1u;  /* 2 MHz / (1 + 1) = 1 MHz */
        default:             return 79u;
    }
}

/**
 * @brief Select the SPI2 baud-rate prescaler for the configured system clock.
 *
 * @param freq System clock selection.
 * @return SPI2 baud-rate prescaler value.
 */
static uint32_t spi2_prescaler_for_freq(ClockFreq_t freq)
{
    switch (freq) {
        case CLK_FREQ_80MHZ: return SPI_BAUDRATEPRESCALER_8; /* 10 MHz */
        case CLK_FREQ_8MHZ:  return SPI_BAUDRATEPRESCALER_2; /* 4 MHz  */
        case CLK_FREQ_2MHZ:  return SPI_BAUDRATEPRESCALER_2; /* 1 MHz  */
        default:             return SPI_BAUDRATEPRESCALER_8;
    }
}

/**
 * @brief Select the ADC1 clock prescaler for the configured system clock.
 *
 * @param freq System clock selection.
 * @return ADC1 clock prescaler value.
 */
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
  * @details Enables the GPIO port clocks required by board peripherals.
  * Radio control and interrupt pins are configured later by the RadioLib HAL.
  */
static void periph_gpio_init(void)
{
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();

     /* Radio GPIO pins (PC9/NRST, PA8/BUSY, PA10/DIO1, PB12/NSS) are
     configured by RadioLib through the HAL — do not touch them here. */

    /* EPS pins (pin map in periph.h). The LTC4040 status outputs are
     * open-drain active-low, so they need pull-ups to read a valid high when
     * de-asserted; without this the pins stay in the STM32L4 analog reset
     * state and the is_charging / has_fault / is_eclipse bits are
     * meaningless. */
    GPIO_InitTypeDef eps_gpio = {0};
    eps_gpio.Mode  = GPIO_MODE_INPUT;
    eps_gpio.Pull  = GPIO_PULLUP;
    eps_gpio.Speed = GPIO_SPEED_FREQ_LOW;
    eps_gpio.Pin = EPS_PIN_CHRG;  HAL_GPIO_Init(EPS_PIN_CHRG_PORT,  &eps_gpio);
    eps_gpio.Pin = EPS_PIN_PFO;   HAL_GPIO_Init(EPS_PIN_PFO_PORT,   &eps_gpio);
    eps_gpio.Pin = EPS_PIN_FAULT; HAL_GPIO_Init(EPS_PIN_FAULT_PORT, &eps_gpio);

    /* CHRGOFF and heater: push-pull outputs, driven low before init so the
     * boot state is safe (charger enabled, heater off). setup_eps() applies
     * the flash-restored configuration once the EPS task starts. */
    eps_gpio.Mode = GPIO_MODE_OUTPUT_PP;
    eps_gpio.Pull = GPIO_NOPULL;
    HAL_GPIO_WritePin(EPS_PIN_CHRGOFF_PORT, EPS_PIN_CHRGOFF, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(EPS_PIN_HEATER_PORT,  EPS_PIN_HEATER,  GPIO_PIN_RESET);
    eps_gpio.Pin = EPS_PIN_CHRGOFF; HAL_GPIO_Init(EPS_PIN_CHRGOFF_PORT, &eps_gpio);
    eps_gpio.Pin = EPS_PIN_HEATER;  HAL_GPIO_Init(EPS_PIN_HEATER_PORT,  &eps_gpio);

    /* CLPROG: explicit analog input for the ADC channel (previously relied
     * on the STM32L4 reset state being analog). */
    eps_gpio.Mode = GPIO_MODE_ANALOG;
    eps_gpio.Pull = GPIO_NOPULL;
    eps_gpio.Pin  = EPS_PIN_CLPROG; HAL_GPIO_Init(EPS_PIN_CLPROG_PORT, &eps_gpio);
}

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
  * @details TIM5 is used as a microsecond timebase. The prescaler is selected
  * from the current system clock configuration so the timer counter runs at
  * 1 MHz for each supported clock frequency.
  * @param freq Current system clock selection.
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
*  - 8-bit frames
*  - Most significant bit (MSB) first
*  - Clock polarity low, clock phase 1st edge (SPI mode 0)
*  - Software NSS management
*  - NSS pulse mode disabled
*  - Baud rate prescaler selected from the current system clock
* @param freq Current system clock selection.
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
  * @brief UART4 Initialization Function
  * @details UART4 is configured for debug output using printf() at 115200 baud.
  */
static void periph_uart4_init(void)
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
    if (HAL_UART_Init(&huart4) != HAL_OK) {
        Error_Handler();
    }
}

/**
  * @brief RTC Initialization Function
  * @details Initializes the RTC in 24-hour mode and preserves the stored
  * date/time across resets when the backup register flag is already set.
  * If the flag is missing, default time/date fields are written and the backup
  * flag is set.
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
  * @details Configures ADC1 for single software-triggered conversions used by
  * the internal temperature-sensor readout. The ADC clock prescaler is selected
  * from the current system clock configuration, and the channel is configured
  * by the code that performs each conversion.
  * - 12-bit resolution
  * - Single conversion mode
  * - Software trigger
  * - Calibration started in single-ended mode
  * @param freq Current system clock selection.
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

/**
  * @brief I2C1 Initialization Function
  * @details Configures I2C1 at 100 kHz for the DS2782 battery fuel gauge.
  * The timing constant is precomputed for an 80 MHz PCLK1; the project does
  * not currently switch system clock at runtime, so a single value suffices.
  * If dynamic clocking is added, periph_reconfigure_for_freq() must reinit
  * I2C1 with a timing value selected from the new PCLK1.
  */
static void periph_i2c1_init(void)
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
    if (HAL_I2C_Init(&hi2c1) != HAL_OK) {
        Error_Handler();
    }

    if (HAL_I2CEx_ConfigAnalogFilter(&hi2c1, I2C_ANALOGFILTER_ENABLE) != HAL_OK) {
        Error_Handler();
    }

    if (HAL_I2CEx_ConfigDigitalFilter(&hi2c1, 0) != HAL_OK) {
        Error_Handler();
    }
}
