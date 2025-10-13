#ifndef STM32_HAL_RADIOLIB_H
#define STM32_HAL_RADIOLIB_H

#include "Hal.h" // RadioLibHal implementation
#include "stm32l4xx_hal.h"

class stm32RadioLibHal : public RadioLibHal {
public:
    stm32RadioLibHal(SPI_HandleTypeDef* spi);
    
    // Pure virtual methods - MUST implement
    // implementations of pure virtual RadioLibHal methods
    void pinMode(uint32_t pin, uint32_t mode) override;
    void digitalWrite(uint32_t pin, uint32_t value) override;
    uint32_t digitalRead(uint32_t pin) override;
    void attachInterrupt(uint32_t interruptNum, void (*interruptCb)(void), uint32_t mode) override;
    void detachInterrupt(uint32_t interruptNum) override;
    void delay(RadioLibTime_t ms) override;
    void delayMicroseconds(RadioLibTime_t us) override;
    RadioLibTime_t millis() override;
    RadioLibTime_t micros() override;
    long pulseIn(uint32_t pin, uint32_t state, RadioLibTime_t timeout) override;
    void spiBegin() override;
    void spiBeginTransaction() override;
    void spiTransfer(uint8_t* out, size_t len, uint8_t* in) override;
    void spiEndTransaction() override;
    void spiEnd() override;

    // implementations of virtual RadioLibHal methods
    void init() override;
    void term() override;
    void tone(uint32_t pin, unsigned int frequency, RadioLibTime_t duration = 0) override;
    void noTone(uint32_t pin) override;
    void yield() override;
    uint32_t pinToInterrupt(uint32_t pin) override;
    
private:
    SPI_HandleTypeDef* _spi;
    uint32_t _startMillis;
};

#endif