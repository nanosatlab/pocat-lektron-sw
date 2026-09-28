/**
 * @file stm32_radiolib_hal.h
 * @author Guillermo O'Tuama Pascual   
 * @brief STM32 implementation of the RadioLib HAL interface
 * @date 2026-01-22
 */


#ifndef STM32_HAL_RADIOLIB_H
#define STM32_HAL_RADIOLIB_H

#include "main.h"
#include "Hal.h"
#include "stm32l4xx_hal.h"


/**
 * @class stm32RadioLibHal
 * @brief STM32 implementation of the RadioLibHal.
 * @details This class provides the STM32-specific implementation of the RadioLibHal interface,
 * enabling RadioLib to operate on our STM32L476RG using the HAL library.
 * @note GPIO pins are encoded as a 32-bit value for compatibility with RadioLib:
 * - upper 16 bits: GPIO port index (GPIOA=0, GPIOB=1, GPIOC=2, ...)
 * - lower 16 bits: GPIO pin mask (GPIO_PIN_x)
 */
class stm32RadioLibHal : public RadioLibHal {
    public:
        /**
        * @brief Constructor
        * @param spi Pointer to an initialized STM32 HAL SPI handle, used for communication with SX1262.
        */
        stm32RadioLibHal(SPI_HandleTypeDef* spi);

        /**
        * @brief GPIO pin mode (input/output/...) configuration method.
        * @param pin Encoded pin to be changed.
        * @param mode Mode to be set (STM32 HAL GPIO mode, GPIO_MODE_*).
        */
        void pinMode(uint32_t pin, uint32_t mode) override;

        /**
        * @brief Digital write method.
        * @param pin Encoded pin to be changed
        * @param value Value to set (GpioLevelHigh or GpioLevelLow).
        */
        void digitalWrite(uint32_t pin, uint32_t value) override;

        /**
        * @brief Digital read method.
        * @param pin Encoded pin to be read
        * @return Value read on the pin (GpioLevelHigh or GpioLevelLow).
        */
        uint32_t digitalRead(uint32_t pin) override;

         /**
        * @brief Method to attach function to an external interrupt.
        * @param interruptNum Interrupt number to attach to (platform-specific).
        * @param interruptCb Interrupt service routine to execute.
        * @param mode Rising/falling mode (platform-specific).
        */
        void attachInterrupt(uint32_t interruptNum, void (*interruptCb)(void), uint32_t mode) override;
        
        /**
        * @brief Method to detach function from an external interrupt.
        * @param interruptNum Interrupt number to detach from.
        */
        void detachInterrupt(uint32_t interruptNum) override;
        
        /**
        * @brief Blocking wait function.
        * @param ms Number of milliseconds to wait
        */
        void delay(RadioLibTime_t ms) override;

        /** 
        * @brief Blocking microsecond wait function.
        * @param us Number of microseconds to wait.
        */
        void delayMicroseconds(RadioLibTime_t us) override;

        /** 
        @brief Get number of milliseconds since start.
        @returns Number of milliseconds since start.
        */
        RadioLibTime_t millis() override;

        /** 
        @brief Get number of microseconds since start.
        @returns Number of microseconds since start.
        */
        RadioLibTime_t micros() override;

        /**
        * @brief Measure the length of incoming digital pulse in microseconds.
        * @param pin Encoded pin to measure on  
        * @param state Pin level to monitor (GpioLevelHigh or GpioLevelLow).
        * @param timeout Timeout in microseconds.
        * @returns Pulse length in microseconds, or 0 if the pulse did not start before timeout.
        */
        long pulseIn(uint32_t pin, uint32_t state, RadioLibTime_t timeout) override;

        /**
        * @brief SPI initialization method.
        */
        void spiBegin() override;

        /**
        * @brief Method to start SPI transaction.
        */
        void spiBeginTransaction() override;

        /**
        * @brief Method to transfer buffer over SPI.
        * @param out Buffer to send.
        * @param len Number of data to send or receive.
        * @param in Buffer to save received data into.
        */
        void spiTransfer(uint8_t* out, size_t len, uint8_t* in) override;
        
        /**
        * @brief Method to end SPI transaction.
        */
        void spiEndTransaction() override;

        /**
        * @brief SPI termination method.
        */
        void spiEnd() override;


        /**
        * @brief Module initialization method.
        * @details This will be called by all radio modules at the beginning of startup.
        * Can be used to e.g., initialize SPI interface.
        */
        void init() override;

        /** 
        @brief Module termination method.
        This will be called by all radio modules when the destructor is called.
        Can be used to e.g., stop SPI interface.
        */
        void term() override;

        /** 
        * @brief Method to produce a square-wave with 50% duty cycle ("tone") of a given frequency at some pin.
        * @note This function will probably not be needed for our use case (communicating with SX1262). Remove 
        * tone and noTone functions if not necesary.
        * @param pin Pin to be used as the output.
        * @param frequency Frequency of the square wave.
        * @param duration Duration of the tone in ms. When set to 0, the tone will be infinite.
        */
        virtual void tone(uint32_t pin, unsigned int frequency, RadioLibTime_t duration = 0);

        /** 
        * @brief Method to stop producing a tone.
        * @param pin Pin which is currently producing the tone.
        */
        virtual void noTone(uint32_t pin);
        
        /** 
        * @brief Yield method, called from long loops in multi-threaded environment (to prevent blocking other threads).
        * @details The implementation of this function assumes yield is called from a task, not from an interrupt. 
        * It does not make sense to call yield from an interrupt.
        */
        virtual void yield();
        
        /** 
        * @brief Function to convert from pin number to interrupt number.
        * @param pin Pin to convert from.
        * @returns The interrupt number of a given pin.
        */
        virtual uint32_t pinToInterrupt(uint32_t pin);

                
        /** @brief Handle the EXTI callback. */
        static void handleExtiCallback(uint16_t pin);
                
    private:
        /** @brief Pointer to the SPI handle. */
        SPI_HandleTypeDef* _spi;

        /** @brief Start time in milliseconds. */
        uint32_t _startMillis;

        /** @brief Array of function pointers to the EXTI callbacks. */
        static void (*_extiCallbacks[16])(void);

        /** @brief Get the port from a pin. */
        GPIO_TypeDef* getPort(uint32_t pin);

        /** @brief Get the pin mask from a pin. */
        uint16_t getPinMask(uint32_t pin);

        /** @brief Get the EXTI line from a pin mask. */
        static int getExtiLineFromPinMask(uint16_t pinMask);

        /** @brief Enable the clock for a port. */
        void enablePortClock(GPIO_TypeDef* port);   

};

#endif