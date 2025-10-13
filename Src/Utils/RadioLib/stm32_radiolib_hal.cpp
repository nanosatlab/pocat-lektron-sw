#include "stm32_radiolib_hal.h"

// random values for now - to be changed to actual STM32 values
#define RADIOLIB_HAL_PIN_MODE_INPUT      0x01
#define RADIOLIB_HAL_PIN_MODE_OUTPUT     0x02
#define RADIOLIB_HAL_PIN_STATUS_LOW      0x00
#define RADIOLIB_HAL_PIN_STATUS_HIGH     0xFF
#define RADIOLIB_HAL_INTERRUPT_RISING    0x10
#define RADIOLIB_HAL_INTERRUPT_FALLING   0x20



stm32RadioLibHal::stm32RadioLibHal(SPI_HandleTypeDef* spi) 
    : RadioLibHal(
        RADIOLIB_HAL_PIN_MODE_INPUT,
        RADIOLIB_HAL_PIN_MODE_OUTPUT,
        RADIOLIB_HAL_PIN_STATUS_LOW,
        RADIOLIB_HAL_PIN_STATUS_HIGH,
        RADIOLIB_HAL_INTERRUPT_RISING,
        RADIOLIB_HAL_INTERRUPT_FALLING
      ),
      _spi(spi),
      _startMillis(0) {
}

void stm32RadioLibHal::pinMode(uint32_t pin, uint32_t mode) {
    // Empty
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