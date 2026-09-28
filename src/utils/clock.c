/**
 * @file clock.c
 * @brief Dynamic system clock frequency switching for power management.
 * @details
 * Implements startup clock configuration and runtime switching between the
 * supported system clock modes: 80 MHz using HSI+PLL, 8 MHz using MSI range 7,
 * and 2 MHz using MSI range 5.
 */

#include "clock.h"
#include "stm32l4xx_hal.h"
#include <stdio.h>

/** @brief Current system clock frequency tracked by the clock module. */
static ClockFreq_t current_freq = CLK_FREQ_80MHZ;

/** @brief Whether the clock module has completed initial clock configuration. */
static bool clock_initialized = false;

static bool systemclock_config_for_freq(ClockFreq_t target);
static bool switch_to_hsi(void);
static bool switch_to_msi(uint32_t msi_range, uint32_t flash_latency);
static bool systemclock_config_hsi(void);
static bool systemclock_config_msi(uint32_t msi_range, uint32_t flash_latency);

bool systemclock_init_for_freq(ClockFreq_t freq)
{
    bool ok;

    if (clock_initialized && freq == current_freq) {
        return true;
    }

    ok = systemclock_config_for_freq(freq);

    if (!ok) {
        printf("System clock config failed\r\n");
        return false;
    }

    current_freq = freq;
    clock_initialized = true;
    return true;
}

bool clock_switch_to_freq(ClockFreq_t freq)
{
    bool ok;

    if (!clock_initialized) {
        return false;
    }

    if (freq == current_freq) {
        return true;
    }

    if (freq == CLK_FREQ_80MHZ) {
        ok = switch_to_hsi();
    } else {
        uint32_t msi_range = (freq == CLK_FREQ_8MHZ) ? RCC_MSIRANGE_7 : RCC_MSIRANGE_5;
        uint32_t latency = (freq == CLK_FREQ_8MHZ) ? FLASH_LATENCY_1 : FLASH_LATENCY_0;
        ok = switch_to_msi(msi_range, latency);
    }

    if (!ok) {
        printf("Clock switch failed\r\n");
        return false;
    }

    current_freq = freq;
    return true;
}

ClockFreq_t clock_get_current(void)
{
    return current_freq;
}

/**
 * @brief Configure the system clock for the requested frequency.
 *
 * Selects the HSI+PLL configuration for 80 MHz, or the appropriate MSI range
 * and flash latency for lower-frequency operation.
 *
 * @param target Target system clock frequency.
 * @return true on success, false if a HAL clock configuration call failed.
 */
static bool systemclock_config_for_freq(ClockFreq_t target)
{
    if (target == CLK_FREQ_80MHZ) {
        return systemclock_config_hsi();
    }

    uint32_t msi_range = (target == CLK_FREQ_8MHZ) ? RCC_MSIRANGE_7 : RCC_MSIRANGE_5;
    uint32_t latency = (target == CLK_FREQ_8MHZ) ? FLASH_LATENCY_1 : FLASH_LATENCY_0;
    return systemclock_config_msi(msi_range, latency);
}

/**
 * @brief Switch the running system clock to the 80 MHz HSI+PLL configuration.
 *
 * Exits low-power run mode if needed, raises the regulator voltage scale,
 * enables HSI and the PLL, switches SYSCLK to the PLL output, and then disables
 * MSI to reduce power consumption.
 *
 * @return true on success, false if any required HAL operation failed.
 */
static bool switch_to_hsi(void)
{
    // Step 1: Exit Low-Power Run mode if active (required before raising voltage/frequency)
    if (__HAL_PWR_GET_FLAG(PWR_FLAG_REGLPF)) {
        if (HAL_PWREx_DisableLowPowerRunMode() != HAL_OK) {
            return false;
        }
    }

    // Step 2: Voltage scaling to Range 1 BEFORE increasing frequency
    if (HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1) != HAL_OK) {
        return false;
    }

    // Step 3: Enable HSI and PLL
    RCC_OscInitTypeDef osc = {0};
    osc.OscillatorType = RCC_OSCILLATORTYPE_HSI;
    osc.HSIState = RCC_HSI_ON;
    osc.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    osc.PLL.PLLState = RCC_PLL_ON;
    osc.PLL.PLLSource = RCC_PLLSOURCE_HSI;
    osc.PLL.PLLM = 1;
    osc.PLL.PLLN = 10;
    osc.PLL.PLLP = RCC_PLLP_DIV7;
    osc.PLL.PLLQ = RCC_PLLQ_DIV2;
    osc.PLL.PLLR = RCC_PLLR_DIV2;
    if (HAL_RCC_OscConfig(&osc) != HAL_OK) {
        return false;
    }

    // Step 4: Switch SYSCLK to PLL
    RCC_ClkInitTypeDef clk = {0};
    clk.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                  | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV1;
    clk.APB2CLKDivider = RCC_HCLK_DIV1;
    if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_4) != HAL_OK) {
        return false;
    }

    // Step 5: Optionally disable MSI to save power
    RCC_OscInitTypeDef msi_off = {0};
    msi_off.OscillatorType = RCC_OSCILLATORTYPE_MSI;
    msi_off.MSIState = RCC_MSI_OFF;
    msi_off.PLL.PLLState = RCC_PLL_NONE;
    HAL_RCC_OscConfig(&msi_off);

    return true;
}

/**
 * @brief Switch the running system clock to an MSI-based configuration.
 *
 * Enables MSI at the requested range, switches SYSCLK to MSI, disables the
 * HSI/PLL clock path, lowers the regulator voltage scale, and enters
 * low-power run mode when the 2 MHz MSI range is selected.
 *
 * @param msi_range STM32 HAL MSI range value for the target clock.
 * @param flash_latency Flash latency required for the target clock.
 * @return true on success, false if any required HAL operation failed.
 */
static bool switch_to_msi(uint32_t msi_range, uint32_t flash_latency)
{
    // Step 1: Exit Low-Power Run mode if active (e.g. switching from 2 MHz to 8 MHz)
    if (__HAL_PWR_GET_FLAG(PWR_FLAG_REGLPF)) {
        if (HAL_PWREx_DisableLowPowerRunMode() != HAL_OK) {
            return false;
        }
    }

    // Step 2: Enable MSI at target range
    RCC_OscInitTypeDef osc = {0};
    osc.OscillatorType = RCC_OSCILLATORTYPE_MSI;
    osc.MSIState = RCC_MSI_ON;
    osc.MSIClockRange = msi_range;
    osc.MSICalibrationValue = RCC_MSICALIBRATION_DEFAULT;
    osc.PLL.PLLState = RCC_PLL_NONE;
    if (HAL_RCC_OscConfig(&osc) != HAL_OK) {
        return false;
    }

    // Step 3: Switch SYSCLK to MSI
    RCC_ClkInitTypeDef clk = {0};
    clk.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                  | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource = RCC_SYSCLKSOURCE_MSI;
    clk.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV1;
    clk.APB2CLKDivider = RCC_HCLK_DIV1;
    if (HAL_RCC_ClockConfig(&clk, flash_latency) != HAL_OK) {
        return false;
    }

    /** @todo Confirm whether PLL must be disabled before HSI instead of
     *        disabling both in the same HAL_RCC_OscConfig() call. */
    // Disable PLL and HSI at the same time with HAL_RCC_OscConfig. PLL might have to be disabled before HSI but we'll leave it like this for now
    RCC_OscInitTypeDef pll_off = {0};
    pll_off.OscillatorType = RCC_OSCILLATORTYPE_HSI;
    pll_off.HSIState = RCC_HSI_OFF; // HSI clock deactivation
    pll_off.PLL.PLLState = RCC_PLL_OFF; // PLL deactivation
    HAL_RCC_OscConfig(&pll_off);

    // Step 5: Voltage scaling to Range 2 AFTER decreasing frequency
    if (HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE2) != HAL_OK) {
        return false;
    }

    /* Step 6: Enter Low-Power run mode at 2 MHz for maximum power savings.
     * At 8 MHz we stay in normal Run mode (Low-Power run mode is only valid up to 2 MHz). */
    if (msi_range == RCC_MSIRANGE_5) {
        HAL_PWREx_EnableLowPowerRunMode();
    }

    return true;
}

/**
 * @brief Configure the startup system clock to the 80 MHz HSI+PLL path.
 *
 * Sets regulator voltage scale 1, enables HSI and LSI, configures the PLL from
 * HSI, and selects the PLL output as SYSCLK.
 * Only should be called once at startup.
 *
 * @return true on success, false if any required HAL operation failed.
 */
static bool systemclock_config_hsi(void)
{
    RCC_OscInitTypeDef osc = {0};
    RCC_ClkInitTypeDef clk = {0};

    // Configure the main internal regulator output voltage
    if (HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1) != HAL_OK) {
        return false;
    }

    osc.OscillatorType = RCC_OSCILLATORTYPE_HSI | RCC_OSCILLATORTYPE_LSI;
    osc.HSIState = RCC_HSI_ON;
    osc.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    osc.LSIState = RCC_LSI_ON;
    osc.PLL.PLLState = RCC_PLL_ON;
    osc.PLL.PLLSource = RCC_PLLSOURCE_HSI;
    osc.PLL.PLLM = 1;
    osc.PLL.PLLN = 10;
    osc.PLL.PLLP = RCC_PLLP_DIV7;
    osc.PLL.PLLQ = RCC_PLLQ_DIV2;
    osc.PLL.PLLR = RCC_PLLR_DIV2;
    if (HAL_RCC_OscConfig(&osc) != HAL_OK) {
        return false;
    }

    // Initializes the CPU, AHB and APB buses clocks 
    clk.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                  | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV1;
    clk.APB2CLKDivider = RCC_HCLK_DIV1;
    if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_4) != HAL_OK) {
        return false;
    }

    return true;
}

/**
 * @brief Configure the startup system clock to an MSI-based path.
 *
 * Sets regulator voltage scale 2, enables LSI and MSI at the requested range,
 * selects MSI as SYSCLK, and enters low-power run mode when the 2 MHz MSI range
 * is selected. Only should be called once at startup.
 *
 * @param msi_range STM32 HAL MSI range value for the target clock.
 * @param flash_latency Flash latency required for the target clock.
 * @return true on success, false if any required HAL operation failed.
 */
static bool systemclock_config_msi(uint32_t msi_range, uint32_t flash_latency)
{
    RCC_OscInitTypeDef osc = {0};
    RCC_ClkInitTypeDef clk = {0};

    // Configure the main internal regulator output voltage
    if (HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE2) != HAL_OK) {
        return false;
    }

    /** Initializes the RCC Oscillators according to the specified parameters
    * in the RCC_OscInitTypeDef structure.
    */
    osc.OscillatorType = RCC_OSCILLATORTYPE_LSI | RCC_OSCILLATORTYPE_MSI;
    osc.LSIState = RCC_LSI_ON;
    osc.MSIState = RCC_MSI_ON;
    osc.MSICalibrationValue = 0;
    osc.MSIClockRange = msi_range;
    osc.PLL.PLLState = RCC_PLL_NONE;
    if (HAL_RCC_OscConfig(&osc) != HAL_OK) {
        return false;
    }

    // Initializes the CPU, AHB and APB buses clocks
    clk.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                  | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource = RCC_SYSCLKSOURCE_MSI;
    clk.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV1;
    clk.APB2CLKDivider = RCC_HCLK_DIV1;
    if (HAL_RCC_ClockConfig(&clk, flash_latency) != HAL_OK) {
        return false;
    }

    // enter low-power run mode
    if (msi_range == RCC_MSIRANGE_5) {
        HAL_PWREx_EnableLowPowerRunMode();
    }

    return true;
}
