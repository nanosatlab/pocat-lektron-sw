/**
 * @file   stm32_radiolib_hal.cpp
 * @brief STM32 implementation of the RadioLib HAL interface
 * @author Guillermo O'Tuama Pascual
 * @date 2026-01-22
 * 
 */

#include "stm32_radiolib_hal.h"
#include <string.h>

/** @brief Forwarding of the HAL_GPIO_EXTI_Callback to our implementation. */
extern "C" void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin) {
    // Forward to our static method
    stm32RadioLibHal::handleExtiCallback(GPIO_Pin);
}

void (*stm32RadioLibHal::_extiCallbacks[16])(void) = { nullptr };

/* 
 * Base GPIO STM32 configuration passed to RadioLibHal:
 *  - GPIO_MODE_INPUT        : mode used for input pins 
 *  - GPIO_MODE_OUTPUT_PP    : push-pull output mode for control pins
 *  - GPIO_PIN_RESET / SET   : logical low/high levels for GpioLevelLow/High
 *  - GPIO_MODE_IT_RISING    : EXTI configuration for interrupts on rising edges
 *  - GPIO_MODE_IT_FALLING   : EXTI configuration for interrupts on falling edges
 */
stm32RadioLibHal::stm32RadioLibHal(SPI_HandleTypeDef* spi) 
    : RadioLibHal(
        GPIO_MODE_INPUT,
        GPIO_MODE_OUTPUT_PP,
        GPIO_PIN_RESET,
        GPIO_PIN_SET,
        GPIO_MODE_IT_RISING,
        GPIO_MODE_IT_FALLING
      ),
      _spi(spi),
      _startMillis(0) {
}

/* 
 * Generic GPIO configuration used by RadioLib:
 *  - Pull = GPIO_NOPULL: pin configured with no internal pull-up/down.
 *  - Speed = GPIO_SPEED_FREQ_LOW: pin configured with low speed.
 * @todo Beware GPIO_SPEED_FREQ_LOW might not be enough for some pins. Check this.
 */
void stm32RadioLibHal::pinMode(uint32_t pin, uint32_t mode) { 

    if(pin == RADIOLIB_NC) {
        return;
    }

    GPIO_TypeDef* port = getPort(pin);
    uint16_t pinMask = getPinMask(pin);

    enablePortClock(port);

    GPIO_InitTypeDef GPIO_InitStruct{};
    GPIO_InitStruct.Pin   = pinMask;
    GPIO_InitStruct.Pull  = GPIO_NOPULL; 
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    GPIO_InitStruct.Mode  = mode;
    HAL_GPIO_Init(port, &GPIO_InitStruct);

}

void stm32RadioLibHal::digitalWrite(uint32_t pin, uint32_t value) {

    if(pin == RADIOLIB_NC) {
        return;
    }

    GPIO_TypeDef* port = getPort(pin);
    uint16_t pinMask = getPinMask(pin);
    HAL_GPIO_WritePin(
        port,
        pinMask,
        (value == GpioLevelHigh) ? GPIO_PIN_SET : GPIO_PIN_RESET
    );
  
}

uint32_t stm32RadioLibHal::digitalRead(uint32_t pin) {

    if (pin == RADIOLIB_NC) {
        return 0;
    }

    GPIO_TypeDef* port = getPort(pin);
    uint16_t pinMask = getPinMask(pin);
    return (HAL_GPIO_ReadPin(port, pinMask) == GPIO_PIN_SET) ? GpioLevelHigh : GpioLevelLow;
}

/** 
 @todo Complete with STM32CubeMX code for attachInterrupt
 @todo Check priority value
  */
void stm32RadioLibHal::attachInterrupt(uint32_t interruptNum, void (*interruptCb)(void), uint32_t mode) {
    
    if (interruptNum == RADIOLIB_NC || interruptCb == nullptr) {
        return;
    }

    GPIO_TypeDef* port = getPort(interruptNum);
    uint16_t pinMask = getPinMask(interruptNum);


    __HAL_RCC_SYSCFG_CLK_ENABLE(); 

    GPIO_InitTypeDef GPIO_InitStruct{};
    GPIO_InitStruct.Pin   = pinMask;
    GPIO_InitStruct.Pull  = GPIO_NOPULL;          
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    GPIO_InitStruct.Mode  = mode;                
    HAL_GPIO_Init(port, &GPIO_InitStruct);

    int line = getExtiLineFromPinMask(pinMask); 
    if (line < 0 || line > 15) {
        return;   
    }
   
    _extiCallbacks[line] = interruptCb;

    
    IRQn_Type irqn;
    if (line <= 4) {
        static const IRQn_Type table[5] = {
            EXTI0_IRQn, EXTI1_IRQn, EXTI2_IRQn, EXTI3_IRQn, EXTI4_IRQn
        };
        irqn = table[line];
    } else if (line <= 9) {
        irqn = EXTI9_5_IRQn;
    } else {
        irqn = EXTI15_10_IRQn;
    }
    
    HAL_NVIC_SetPriority(irqn, 5, 0);
    HAL_NVIC_EnableIRQ(irqn);
}

/**
 @todo Complete with STM32CubeMX code for detachInterrupt
*/
void stm32RadioLibHal::detachInterrupt(uint32_t interruptNum) {
    if (interruptNum == RADIOLIB_NC) {
        return;
    }

    uint16_t pinMask   = getPinMask(interruptNum);
    int line = getExtiLineFromPinMask(pinMask);
    if (line < 0 || line > 15) {
        return;
    }

    _extiCallbacks[line] = nullptr;

    __HAL_GPIO_EXTI_CLEAR_IT(pinMask); // Clear EXTI flag ??

    IRQn_Type irqn;

    if (line <= 4) {
        static const IRQn_Type table[5] = {
            EXTI0_IRQn, EXTI1_IRQn, EXTI2_IRQn, EXTI3_IRQn, EXTI4_IRQn
        };
        irqn = table[line];
        HAL_NVIC_DisableIRQ(irqn);
    }
    else {
        int groupStart, groupEnd;
        if (line <= 9) {
            irqn = EXTI9_5_IRQn;
            groupStart = 5;
            groupEnd   = 9;
        }
        else {
            irqn = EXTI15_10_IRQn;
            groupStart = 10;
            groupEnd   = 15;
        }
        bool anyUsed = false;
        for (int i = groupStart; i <= groupEnd; ++i) {
   
            if (_extiCallbacks[i] != nullptr) {
                anyUsed = true;
                break;
            }
        }
        if (!anyUsed) {
            HAL_NVIC_DisableIRQ(irqn);
        }
    }
}

void stm32RadioLibHal::delay(RadioLibTime_t ms) {
#if !defined(RADIOLIB_CLOCK_DRIFT_MS)
    HAL_Delay(ms);
#else
    HAL_Delay(ms * 1000 / (1000 + RADIOLIB_CLOCK_DRIFT_MS));
#endif
}

/* The delay is implemented as a busy wait. */
/** @todo Check if a busy wait implementation works for the case.*/
void stm32RadioLibHal::delayMicroseconds(RadioLibTime_t us) {
#if !defined(RADIOLIB_CLOCK_DRIFT_MS)
    RadioLibTime_t start = micros();
    while ((micros() - start) < us); 
#else
    RadioLibTime_t corrected = us * 1000 / (1000 + RADIOLIB_CLOCK_DRIFT_MS);
    RadioLibTime_t start = micros();
    while ((micros() - start) < corrected); 
#endif
}

RadioLibTime_t stm32RadioLibHal::millis() {
#if !defined(RADIOLIB_CLOCK_DRIFT_MS)
    return HAL_GetTick();
#else
    return HAL_GetTick() * 1000 / (1000 + RADIOLIB_CLOCK_DRIFT_MS);
#endif
}

/** @todo check thif this works when operational mode changes are implemented, 
 * because TIM5 is clocked from APB1 bus.
 */
RadioLibTime_t stm32RadioLibHal::micros() {
#if !defined(RADIOLIB_CLOCK_DRIFT_MS)
    return __HAL_TIM_GET_COUNTER(&htim5);
#else
    return __HAL_TIM_GET_COUNTER(&htim5) * 1000 / (1000 + RADIOLIB_CLOCK_DRIFT_MS);
#endif
}

long stm32RadioLibHal::pulseIn(uint32_t pin, uint32_t state, RadioLibTime_t timeout)
{
    
    if(pin == RADIOLIB_NC) {
        return 0;
    }

    GPIO_TypeDef* port = getPort(pin);
    uint16_t pinMask = getPinMask(pin);

    uint32_t startMicros = micros();
    uint32_t timeoutMicros = timeout;

    uint8_t targetState = (state ? GPIO_PIN_SET : GPIO_PIN_RESET);

    // wait for the pin to go out of target state
    while (HAL_GPIO_ReadPin(port, pinMask) == targetState) {
        if ((uint32_t)(micros() - startMicros) > timeoutMicros) return 0;
        yield();
    }

    // wait for the pin to go back to target state
    while (HAL_GPIO_ReadPin(port, pinMask) != targetState) {
       if ((uint32_t)(micros() - startMicros) > timeoutMicros) return 0;
        yield();
    }

    // measure the time the pin remains in target state
    uint32_t pulseStart = micros();
    while (HAL_GPIO_ReadPin(port, pinMask) == targetState) {
        if ((uint32_t)(micros() - startMicros) > timeoutMicros) return 0;
        yield();
    }
    return micros() - pulseStart;

}

/* No need to implement this in stm32. Init SPI at boot! */
void stm32RadioLibHal::spiBegin() {}

/* No need to for beginning transaction in stm32. Just use the spi handle!*/
void stm32RadioLibHal::spiBeginTransaction() {}

/**
 * @brief SPI full-duplex transfer using direct 8-bit register access.
 *
 * HAL_SPI_TransmitReceive uses 16-bit data-packing for transfers > 1 byte,
 * switching the FRXTH threshold mid-transfer for odd lengths.  This causes
 * an intermittent FIFO race condition that corrupts received data — no
 * combination of pre/post flushing reliably prevents it.
 *
 * This implementation keeps FRXTH = 1 permanently and accesses DR as
 * uint8_t, clocking one byte at a time.  Deterministic and reliable.
 *
 * @todo Protect SPI with a mutex/semaphore if multiple tasks access it.
 */
void stm32RadioLibHal::spiTransfer(uint8_t* out, size_t len, uint8_t* in)
{
    configASSERT(!xPortIsInsideInterrupt());
    if (len == 0) return;

    SPI_TypeDef* spi = _spi->Instance;

    // Force 8-bit RX FIFO threshold so RXNE fires after every single byte
    spi->CR2 |= SPI_RXFIFO_THRESHOLD;

    // Enable SPI if not already on
    if (!(spi->CR1 & SPI_CR1_SPE)) {
        spi->CR1 |= SPI_CR1_SPE;
    }

    for (size_t i = 0; i < len; i++) {
        while (!(spi->SR & SPI_SR_TXE)) {}
        *(__IO uint8_t *)&spi->DR = out ? out[i] : 0x00;

        while (!(spi->SR & SPI_SR_RXNE)) {}
        uint8_t rx = *(__IO uint8_t *)&spi->DR;
        if (in) in[i] = rx;
    }

    while (spi->SR & SPI_SR_BSY) {}
}

/*  No need to for ending spi transaction in stm32. */
void stm32RadioLibHal::spiEndTransaction() {}

/* No need to for ending spi in stm32.*/
void stm32RadioLibHal::spiEnd() {}

/* No need to initialize anything in stm32. */
void stm32RadioLibHal::init() {}

/* No need to terminate anything in stm32. */
void stm32RadioLibHal::term() {}

/** @todo Check if this function is necesary for our use case (communicating with SX1262)*/
void stm32RadioLibHal::tone(uint32_t pin, unsigned int frequency, RadioLibTime_t duration) {

    if (pin == RADIOLIB_NC || frequency == 0) { //To-do: might have to protect frequency values a bit more. > than a certain value?
        return;
    }

    GPIO_TypeDef* port = getPort(pin);
    uint16_t pinMask = getPinMask(pin);

    // solo se puede usar PA0, PA5 o PA15 para salida de timer2 channel1
    if (!(port == GPIOA &&
         (pinMask == GPIO_PIN_0 ||
          pinMask == GPIO_PIN_5 ||
          pinMask == GPIO_PIN_15))) {
        return;  
    }

    enablePortClock(port);

    GPIO_InitTypeDef GPIO_InitStruct{};
    GPIO_InitStruct.Pin = pinMask;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    GPIO_InitStruct.Alternate = GPIO_AF1_TIM2;
    HAL_GPIO_Init(port, &GPIO_InitStruct);

    // Compute timer clock correctly
    uint32_t timerClock = HAL_RCC_GetPCLK1Freq();

    // If APB1 prescaler > 1, timer clock is multiplied by 2
    if ((RCC->CFGR & RCC_CFGR_PPRE1) != RCC_CFGR_PPRE1_DIV1) {
        timerClock *= 2;
    }
    //uint32_t timerClock = 80000000; // To-do: 80 MHz, sys clock is set to 80 MHz at the moment (nominal state). This will have to be changed when operational mode changes are implemented
    uint32_t arr = (timerClock / frequency) - 1;

    __HAL_TIM_SET_PRESCALER(&htim2, 0);
    __HAL_TIM_SET_AUTORELOAD(&htim2, arr);

    // arduino's tone uses 50% duty cycle
    __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, arr / 2);

    // this reloads parameter values immediately
    HAL_TIM_GenerateEvent(&htim2, TIM_EVENTSOURCE_UPDATE);

    // Start PWM output
    HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_1);

    // Optional blocking with duration
    if (duration > 0) {
        delay(duration);
        noTone(pin);
    }
}

/** @todo Check if this function is necesary for our use case (communicating with SX1262)*/
void stm32RadioLibHal::noTone(uint32_t pin) {
    if (pin == RADIOLIB_NC) {
        return;
    }
    HAL_TIM_PWM_Stop(&htim2, TIM_CHANNEL_1);

    GPIO_TypeDef* port = getPort(pin);
    uint16_t pinMask = getPinMask(pin);

    // reseteamos pin como un ouput normal (para que device no lea floating values) 
    GPIO_InitTypeDef GPIO_InitStruct{};
    GPIO_InitStruct.Pin = pinMask;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(port, &GPIO_InitStruct);

    HAL_GPIO_WritePin(port, pinMask, GPIO_PIN_RESET);
}

/* This implementation assumes yield is called from a task, not from an interrupt. 
 * It does not make sense to call yield from an interrupt.
*/
void stm32RadioLibHal::yield() {
    // Allow task switching if scheduler is active
    if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) {
        if (!xPortIsInsideInterrupt()) {
            taskYIELD();
        }
    }
}

/* We will treat interrupt pins as normal pins, by encoding the port and pin as a single number. */
uint32_t stm32RadioLibHal::pinToInterrupt(uint32_t pin) { return pin; }   

// ============================================================================
// Private Helper Functions 
// ============================================================================

/* We encode the pins as a uint32_t where the upper 16 bits are the port and 
the lower 16 bits are the pin (port+pin).*/
GPIO_TypeDef* stm32RadioLibHal::getPort(uint32_t pin)
{
    uint32_t portIndex = (pin >> 16) & 0xFF;

    switch (portIndex) {
        case 0: return GPIOA;
        case 1: return GPIOB;
        case 2: return GPIOC;
        case 3: return GPIOD;
        case 4: return GPIOE;
        case 5: return GPIOF;
        case 6: return GPIOG;
        default:
            configASSERT(false);
            return nullptr;
    }
}

uint16_t stm32RadioLibHal::getPinMask(uint32_t pin) {
    return (uint16_t)(pin & 0xFFFF);
}

void stm32RadioLibHal::enablePortClock(GPIO_TypeDef* port)
{
    if (port == GPIOA)      __HAL_RCC_GPIOA_CLK_ENABLE();
    else if (port == GPIOB) __HAL_RCC_GPIOB_CLK_ENABLE();
    else if (port == GPIOC) __HAL_RCC_GPIOC_CLK_ENABLE();
    else if (port == GPIOD) __HAL_RCC_GPIOD_CLK_ENABLE();
    else if (port == GPIOE) __HAL_RCC_GPIOE_CLK_ENABLE();
    else if (port == GPIOF) __HAL_RCC_GPIOF_CLK_ENABLE();
    else if (port == GPIOG) __HAL_RCC_GPIOG_CLK_ENABLE();
    else {
        configASSERT(false); 
        return;
    }
}

int stm32RadioLibHal::getExtiLineFromPinMask(uint16_t pinMask) {
    // Check if exactly one bit is set
    if (pinMask == 0 || (pinMask & (pinMask - 1)) != 0) {
        configASSERT(false); 
        return -1;
    }
    
    return __builtin_ctz(pinMask);
}

/** @todo Refactor to use FreeRTOS task notification instead of direct callback execution.
 *  Currently executing callbacks directly from ISR is dangerous and can cause issues:
 *  - Callbacks may call FreeRTOS APIs that are not ISR-safe
 *  - Long callback execution blocks other interrupts
 *  - No proper synchronization with task context
 *  
 *  Should be replaced with:
 *  BaseType_t xHigherPriorityTaskWoken = pdFALSE;
 *  xTaskNotifyFromISR(COMMS_TASK, (1 << line), eSetBits, &xHigherPriorityTaskWoken);
 *  portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
 *  
 *  Then handle the callback execution in the COMMS task context.
 */
void stm32RadioLibHal::handleExtiCallback(uint16_t gpioPin) {
    int line = getExtiLineFromPinMask(gpioPin);
    if (line < 0 || line > 15) {
        return;
    }

    // Dejo esto de prueba, pero esta parte es peligrosa. Deberiamos de notificar a COMMS task 
    // en vez de ejecutar el callback directamente
    void (*cb)(void) = _extiCallbacks[line];
    if (cb != nullptr) {
        cb();
    }
}
