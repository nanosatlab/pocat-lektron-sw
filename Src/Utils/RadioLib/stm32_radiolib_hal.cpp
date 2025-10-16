#include "stm32_radiolib_hal.h"

// ----------------Helper private functions ---------------------

// Extract the GPIO port from a combined pin ID (upper 16 bits = port)

GPIO_TypeDef* stm32RadioLibHal::getPort(uint32_t pin) {
    uint32_t portIndex = (pin >> 16) & 0xFF;
    switch (portIndex) {
        case 0: return GPIOA;
        case 1: return GPIOB;
        case 2: return GPIOC;
        case 3: return GPIOD;
        case 4: return GPIOE;
        case 5: return GPIOF;
        default: return GPIOA; // fallback
    }
}

// Extract the pin mask (lower 16 bits)
uint16_t stm32RadioLibHal::getPinMask(uint32_t pin) {
    return (uint16_t)(pin & 0xFFFF);
}

void stm32RadioLibHal::enablePortClock(GPIO_TypeDef* port) {
    if (port == GPIOA)      __HAL_RCC_GPIOA_CLK_ENABLE();
    else if (port == GPIOB) __HAL_RCC_GPIOB_CLK_ENABLE();
    else if (port == GPIOC) __HAL_RCC_GPIOC_CLK_ENABLE();
    else if (port == GPIOD) __HAL_RCC_GPIOD_CLK_ENABLE();
    else if (port == GPIOE) __HAL_RCC_GPIOE_CLK_ENABLE();
    else if (port == GPIOF) __HAL_RCC_GPIOF_CLK_ENABLE();
}


// ---------------- stm32RadioLibHal implementation ---------------------

stm32RadioLibHal::stm32RadioLibHal(SPI_HandleTypeDef* spi) 
    : RadioLibHal(
        GPIO_MODE_INPUT,
        GPIO_MODE_OUTPUT_PP, // push-pull output
        GPIO_PIN_RESET,
        GPIO_PIN_SET,
        GPIO_MODE_IT_RISING,
        GPIO_MODE_IT_FALLING
      ),
      _spi(spi),
      _startMillis(0) {
}

// para utilitzar esta fucion vamos a tener que codificar el pin y el port en el primer parametero "pin"
void stm32RadioLibHal::pinMode(uint32_t pin, uint32_t mode) {

    GPIO_InitTypeDef GPIO_InitStruct = {0};

    // recogemos puerto y pin:
    GPIO_TypeDef* port = getPort(pin);
    uint16_t pinMask = getPinMask(pin);

    /* GPIO Ports Clock Enable */
    enablePortClock(port);

    /*Configure GPIO pin Output Level */
    // Do I have to use HAL_GPIO_WritePin???

    /*Configure GPIO pin :  */
    GPIO_InitStruct.Pin = pinMask;
    GPIO_InitStruct.Pull = GPIO_NOPULL; 
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    GPIO_InitStruct.Mode = mode;
    HAL_GPIO_Init(port, &GPIO_InitStruct);

    // What about this here?? Look into it:
    // HAL_NVIC_SetPriority(EXTI15_10_IRQn, 5, 0);
    // HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);  HAL_NVIC_SetPriority(EXTI15_10_IRQn, 5, 0);
    // HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);
    
}

void stm32RadioLibHal::digitalWrite(uint32_t pin, uint32_t value) {
    // Empty
}

uint32_t stm32RadioLibHal::digitalRead(uint32_t pin) {
    return 0;
}

void stm32RadioLibHal::attachInterrupt(uint32_t interruptNum, void (*interruptCb)(void), uint32_t mode) {
    // Empty
}

void stm32RadioLibHal::detachInterrupt(uint32_t interruptNum) {
    // Empty
}

void stm32RadioLibHal::delay(RadioLibTime_t ms) {
    // Empty
}

void stm32RadioLibHal::delayMicroseconds(RadioLibTime_t us) {
    // Empty
}

RadioLibTime_t stm32RadioLibHal::millis() {
    return 0;
}

RadioLibTime_t stm32RadioLibHal::micros() {
    return 0;
}

long stm32RadioLibHal::pulseIn(uint32_t pin, uint32_t state, RadioLibTime_t timeout) {
    return 0;
}

void stm32RadioLibHal::spiBegin() {
    // Empty
}

void stm32RadioLibHal::spiBeginTransaction() {
    // Empty
}

void stm32RadioLibHal::spiTransfer(uint8_t* out, size_t len, uint8_t* in) {
    // Empty
}

void stm32RadioLibHal::spiEndTransaction() {
    // Empty
}

void stm32RadioLibHal::spiEnd() {
    // Empty
}

void stm32RadioLibHal::init() {
    // Empty
}
void stm32RadioLibHal::term() {
    // Empty
}

void stm32RadioLibHal::tone(uint32_t pin, unsigned int frequency, RadioLibTime_t duration) {
    // Empty
}

void stm32RadioLibHal::noTone(uint32_t pin) {
    // Empty
}

void stm32RadioLibHal::yield() {
    // Empty
}

uint32_t stm32RadioLibHal::pinToInterrupt(uint32_t pin) {
    return 0;
}   