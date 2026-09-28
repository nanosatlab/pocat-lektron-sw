/**
 * @file radiolib_wrapper.cpp
 * @brief C-compatible wrapper around RadioLib.
 * @details
 * Adapts the C API declared in radiolib_wrapper.h to the C++ RadioLib API
 * for the SX1262 using the STM32-specific RadioLib HAL implementation.
 */

#ifndef RADIO_MOCK
#include "radiolib_wrapper.h"
#include "modules/SX126x/SX126x_registers.h"

#include <RadioLib.h>
#include "stm32_radiolib_hal.h"
#include <stdio.h>
#include "FreeRTOS.h"
#include "semphr.h"

/** @brief STM32 RadioLib HAL adapter used by the RadioLib module instance. */
static stm32RadioLibHal hal(&hspi2);  

// Pin encoding: (portIndex << 16) | GPIO_PIN_x
// Port index: A=0, B=1, C=2, D=3, ...
#define RADIO_PIN_NSS    ((1 << 16) | GPIO_PIN_12)  // PB12 - SX1262 Chip Select
#define RADIO_PIN_DIO1   ((0 << 16) | GPIO_PIN_10)  // PA10 - SX1262 Interrupt (DIO1)
#define RADIO_PIN_RESET  ((2 << 16) | GPIO_PIN_9)   // PC9  - SX1262 Reset
#define RADIO_PIN_BUSY   ((0 << 16) | GPIO_PIN_8)   // PA8  - SX1262 Busy Indicator

/**
 * @brief RadioLib module descriptor for the SX1262 board wiring.
 *
 * Module constructor order: Module(hal, cs, irq, rst, gpio)
 * - cs   = NSS         (PB12)
 * - irq  = DIO1        (PA10)
 * - rst  = SX1262_NRST (PC9)
 * - gpio = BUSY        (PA8)
 */
static Module mod(&hal, RADIO_PIN_NSS, RADIO_PIN_DIO1, RADIO_PIN_RESET, RADIO_PIN_BUSY);

/** @brief RadioLib SX1262 instance used by the C wrapper functions. */
static SX1262 radio(&mod);

/**
 * @brief Convert wrapper bandwidth code to RadioLib bandwidth in kHz.
 * @param bw_code Bandwidth code (0=125 kHz, 1=250 kHz, 2=500 kHz).
 * @return Bandwidth value in kHz. Defaults to 125 kHz for unknown codes.
 */
static float bwCodeToKHz(uint8_t bw_code) {
  switch(bw_code) {
    case 0: return 125.0f;
    case 1: return 250.0f;
    case 2: return 500.0f;
    default: return 125.0f;
  }
}

/** @brief Semaphore signaled from DIO1 during blocking duty-cycle receive. */
static SemaphoreHandle_t s_dutyCycleSem = NULL;

/** @brief Task notified from the DIO1 ISR for asynchronous radio operations. */
static TaskHandle_t s_irqTask = NULL;

#define TRANSCEIVER_RADIO_IRQ_BIT   (1UL << 0)

/**
 * @brief DIO1 ISR callback used by blocking duty-cycle receive.
 *
 * Gives s_dutyCycleSem from interrupt context so the waiting task can continue.
 */
static void dutyCycleIsrCallback(void) {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xSemaphoreGiveFromISR(s_dutyCycleSem, &xHigherPriorityTaskWoken);
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

/**
 * @brief DIO1 ISR callback used by asynchronous radio operations.
 *
 * Notifies the task registered with RadioLib_SetIrqTask() that a radio IRQ
 * event occurred.
 */
static void radioIrqCallback(void) {
    if (s_irqTask == NULL) return;
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xTaskNotifyFromISR(s_irqTask, TRANSCEIVER_RADIO_IRQ_BIT, eSetBits, &xHigherPriorityTaskWoken);
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

extern "C" { // to stop name mangling

    int16_t RadioLib_Init(void) {
        printf("RadioLib_Init: calling radio.begin()...\r\n");

        // SX1262MB2xAS uses a crystal oscillator (XTAL), not a TCXO.
        // Must set this BEFORE begin() so RadioLib skips DIO3 TCXO setup.
        radio.XTAL = true;
        const int16_t state = radio.begin(
            434.0,  
            125.0,  
            9,      
            7,       
            RADIOLIB_SX126X_SYNC_WORD_PRIVATE,
            10,     
            8,      
            0.0,     // tcxoVoltage = 0 (no TCXO on this board)
            false   
        );
        if(state != RADIOLIB_ERR_NONE) {
            printf("Error initializing radio: %d\r\n", state);
        }
        return state;
    }

    void RadioLib_SetChannel(uint32_t freq_hz) {
       float freq_mhz = freq_hz / 1000000.0f;
       const int state = radio.setFrequency(freq_mhz); //esperem MHz
       if(state != RADIOLIB_ERR_NONE) {
           // Handle error (could add error callback or logging)
           //printf("Error en la configuracio de la frequencia: %d\n", state);
       }
    }

    static void logIfErr(const char *who, int state) {
        if (state != RADIOLIB_ERR_NONE) {
            printf("RadioLib_%s FAILED state=%d\r\n", who, state);
        }
    }

    void RadioLib_SetTxConfig(uint8_t sf, uint8_t cr, int8_t power,
                            uint8_t bw, int iqInverted,
                            int crcOn, uint16_t preambleLen)
    {
        // Set bandwidth FIRST so setSpreadingFactor's internal LDRO
        // auto-calc (symbolLength = 2^sf / bandwidthKhz) uses the bandwidth
        // we're actually about to use, not whatever was cached before.
        logIfErr("SetTxConfig.setBandwidth", radio.setBandwidth(bwCodeToKHz(bw)));
        logIfErr("SetTxConfig.setOutputPower", radio.setOutputPower(power));
        logIfErr("SetTxConfig.setSpreadingFactor", radio.setSpreadingFactor(sf));
        logIfErr("SetTxConfig.setCodingRate", radio.setCodingRate(cr + 4));
        logIfErr("SetTxConfig.setCRC", radio.setCRC(crcOn));
        logIfErr("SetTxConfig.setPreambleLength", radio.setPreambleLength(preambleLen));

        // Note: IQ inversion is not a separate method in RadioLib SX1262
        // If needed, handle in modulation settings or ignore.
    }

    void RadioLib_SetRxConfig(uint8_t sf, uint8_t cr, uint8_t bw,
                            int iqInverted, int crcOn,
                            uint16_t preambleLen)
    {
        logIfErr("SetRxConfig.setBandwidth", radio.setBandwidth(bwCodeToKHz(bw)));
        logIfErr("SetRxConfig.setSpreadingFactor", radio.setSpreadingFactor(sf));
        logIfErr("SetRxConfig.setCodingRate", radio.setCodingRate(cr + 4));
        logIfErr("SetRxConfig.setCRC", radio.setCRC(crcOn));
        logIfErr("SetRxConfig.setPreambleLength", radio.setPreambleLength(preambleLen));

        // Note: IQ inversion doesn’t have a separate setter in RadioLib SX1262
    }


    int16_t RadioLib_Receive(uint32_t timeoutMs,
                        uint8_t *outBuf, uint16_t bufSize,
                        uint16_t *outLen, int16_t *outRssi, int8_t *outSnr)
    {
        int16_t st = radio.receive(outBuf, bufSize, timeoutMs);

        if (st == RADIOLIB_ERR_NONE) {
            if (outLen)  *outLen  = radio.getPacketLength();
            if (outRssi) *outRssi = radio.getRSSI();
            if (outSnr)  *outSnr  = radio.getSNR();
        }
        return st;
    }

    int16_t RadioLib_Transmit(uint8_t *buf, uint16_t len) {
        return radio.transmit(buf, len);
    }

    int16_t RadioLib_Sleep(void) {
        return radio.sleep();
    }

    int16_t RadioLib_Standby(void) {
        return radio.standby();
    }

    int16_t RadioLib_ScanChannel(void) {
        return radio.scanChannel();
    }

    // Uses startChannelScan() (non-blocking) + DIO1 polling instead of the
    // blocking scanChannel() which only does a single scan.
    int16_t RadioLib_CadReceive(uint32_t cadTimeoutMs, uint32_t rxTimeoutMs,
                           uint8_t *outBuf, uint16_t bufSize,
                           uint16_t *outLen, int16_t *outRssi, int8_t *outSnr)
    {
        ChannelScanConfig_t cfg = {
            .cad = {
                .symNum = RADIOLIB_SX126X_CAD_ON_8_SYMB,
                .detPeak = 24, // default is 21 (RADIOLIB_SX126X_CAD_PARAM_DEFAULT), but it detects too much noise at 21.
                .detMin = RADIOLIB_SX126X_CAD_PARAM_DEFAULT,
                .exitMode = RADIOLIB_SX126X_CAD_GOTO_RX,
                .timeout = (RadioLibTime_t)rxTimeoutMs * 1000UL,
                .irqFlags = (1UL << RADIOLIB_IRQ_CAD_DETECTED) |
                            (1UL << RADIOLIB_IRQ_CAD_DONE) |
                            (1UL << RADIOLIB_IRQ_RX_DONE) |
                            (1UL << RADIOLIB_IRQ_TIMEOUT) |
                            (1UL << RADIOLIB_IRQ_CRC_ERR) |
                            (1UL << RADIOLIB_IRQ_HEADER_VALID) |
                            (1UL << RADIOLIB_IRQ_HEADER_ERR),
                .irqMask  = (1UL << RADIOLIB_IRQ_CAD_DETECTED) |
                            (1UL << RADIOLIB_IRQ_CAD_DONE) |
                            (1UL << RADIOLIB_IRQ_RX_DONE) |
                            (1UL << RADIOLIB_IRQ_TIMEOUT),
            },
        };

        RadioLibTime_t cadDeadline = hal.millis() + cadTimeoutMs;

        for (;;) {
            if (hal.millis() >= cadDeadline) {
                return RADIOLIB_CHANNEL_FREE;
            }

            int16_t state = radio.startChannelScan(cfg);
            if (state != RADIOLIB_ERR_NONE) {
                return state;
            }
            
            // The following logic mimics the radiolibs implementation of scanchannel() and receive()
            while (!hal.digitalRead(mod.getIrq())) {
                hal.yield();
                if (hal.millis() >= cadDeadline) {
                    (void)radio.standby();
                    return RADIOLIB_CHANNEL_FREE;
                }
            }

            uint32_t irq = radio.getIrqFlags();

            if (!(irq & RADIOLIB_SX126X_IRQ_CAD_DETECTED)) {
                /* Channel free — immediately loop back for the next scan.
                   startChannelScan() clears IRQs internally on re-entry. */
                continue;
            }

            /* ===== Phase 2: LoRa detected — radio is now in RX ===== */
            printf("COMMS: CAD detected LoRa activity\r\n");

            /* Clear the CAD IRQ bits so DIO1 can re-fire on RX_DONE. */
            radio.clearIrqFlags(
                RADIOLIB_SX126X_IRQ_CAD_DONE |
                RADIOLIB_SX126X_IRQ_CAD_DETECTED);

            /* Wait for DIO1 → RX_DONE or TIMEOUT */
            bool softTimeout = false;
            RadioLibTime_t rxDeadline = hal.millis() + rxTimeoutMs + 1000;
            while (!hal.digitalRead(mod.getIrq())) {
                hal.yield();
                if (hal.millis() >= rxDeadline) {
                    softTimeout = true;
                    break;
                }
            }

            state = radio.standby();
            if ((state != RADIOLIB_ERR_NONE) && (state != RADIOLIB_ERR_SPI_CMD_TIMEOUT)) {
                printf("COMMS: Error transitioning to standby after CAD detection: %d\r\n", state);
                return state;
            }

            irq = radio.getIrqFlags();

            if (softTimeout || (irq & RADIOLIB_SX126X_IRQ_TIMEOUT)) {
                (void)radio.clearIrqFlags(RADIOLIB_SX126X_IRQ_ALL);
                printf("COMMS: CAD->RX timeout, retrying\r\n");
                continue;
            }

            printf("COMMS: CAD->RX done, reading packet\r\n");
            size_t pktLen = radio.getPacketLength();

            int16_t st = radio.readData(outBuf, bufSize);
            if (st == RADIOLIB_ERR_NONE) {
                if (outLen)  *outLen  = (uint16_t)(pktLen > bufSize ? bufSize : pktLen);
                if (outRssi) *outRssi = (int16_t)radio.getRSSI();
                if (outSnr)  *outSnr  = (int8_t)radio.getSNR();
            }
            return st;
        }
    }

    int16_t RadioLib_DutyCycleReceive(uint32_t listenMs, uint16_t preambleLen,
                                      uint8_t *outBuf, uint16_t bufSize,
                                      uint16_t *outLen, int16_t *outRssi, int8_t *outSnr)
    {
        // Create semaphore once (persists across calls)
        if (s_dutyCycleSem == NULL) {
            s_dutyCycleSem = xSemaphoreCreateBinary();
            if (s_dutyCycleSem == NULL) return -1;
        }

        // Drain any stale signal from a previous interrupted cycle
        xSemaphoreTake(s_dutyCycleSem, 0);

        // Attach DIO1 rising-edge interrupt → ISR gives semaphore
        hal.attachInterrupt(mod.getIrq(), dutyCycleIsrCallback, hal.GpioInterruptRising);

        // Start hardware duty cycle RX (radio cycles autonomously)
        int16_t state = radio.startReceiveDutyCycleAuto(
            preambleLen, 0,
            RADIOLIB_IRQ_RX_DEFAULT_FLAGS,
            RADIOLIB_IRQ_RX_DEFAULT_MASK);
        if (state != RADIOLIB_ERR_NONE) {
            hal.detachInterrupt(mod.getIrq());
            return state;
        }

        // Block until DIO1 fires (RX_DONE) or our listen window expires
        BaseType_t got = xSemaphoreTake(s_dutyCycleSem, pdMS_TO_TICKS(listenMs));

        // Stop radio and detach interrupt
        radio.standby();
        hal.detachInterrupt(mod.getIrq());

        if (got != pdTRUE) {
            radio.clearIrqFlags(RADIOLIB_SX126X_IRQ_ALL);
            return RADIOLIB_ERR_RX_TIMEOUT;
        }

        // DIO1 fired — check what happened
        uint32_t irq = radio.getIrqFlags();

        if (!(irq & RADIOLIB_SX126X_IRQ_RX_DONE)) {
            radio.clearIrqFlags(RADIOLIB_SX126X_IRQ_ALL);
            return RADIOLIB_ERR_RX_TIMEOUT;
        }

        // Read the packet
        size_t pktLen = radio.getPacketLength();
        int16_t st = radio.readData(outBuf, bufSize);
        if (st == RADIOLIB_ERR_NONE) {
            if (outLen)  *outLen  = (uint16_t)(pktLen > bufSize ? bufSize : pktLen);
            if (outRssi) *outRssi = (int16_t)radio.getRSSI();
            if (outSnr)  *outSnr  = (int8_t)radio.getSNR();
        }
        return st;
    }

    void RadioLib_IrqProcess(void) {
        // De moment no fa falta implementar res aqui ja que radiolib gestiona els interrupts internament.
        // No obstant, si ens posem en un mode no bloquejant, potser caldra implementar alguna cosa aqui.
    }

    /* ===== Async interrupt-driven API ===== */

    void RadioLib_SetIrqTask(void *handle) {
        s_irqTask = (TaskHandle_t)handle;
        hal.attachInterrupt(mod.getIrq(), radioIrqCallback, hal.GpioInterruptRising);
    }

    int16_t RadioLib_StartReceive(uint16_t preambleLen) {
        int16_t state = radio.startReceiveDutyCycleAuto(
            preambleLen, 0,
            RADIOLIB_IRQ_RX_DEFAULT_FLAGS,
            RADIOLIB_IRQ_RX_DEFAULT_MASK);
        if (state != RADIOLIB_ERR_NONE) {
            printf("RadioLib_StartReceive: startReceiveDutyCycleAuto FAILED state=%d preamble=%u\r\n",
                   (int)state, (unsigned)preambleLen);
        }
        return state;
    }

    int16_t RadioLib_StartTransmit(uint8_t *buf, uint16_t len) {
        int16_t state = radio.startTransmit(buf, len);
        return state;
    }

    uint32_t RadioLib_GetIrqFlags(void) {
        return radio.getIrqFlags();
    }

    void RadioLib_ClearIrqFlags(uint32_t mask) {
        radio.clearIrqFlags(mask);
    }

    int16_t RadioLib_ReadRxData(uint8_t *outBuf, uint16_t bufSize,
                                uint16_t *outLen, int16_t *outRssi, int8_t *outSnr) {
        int16_t st = radio.readData(outBuf, bufSize);
        if (st == RADIOLIB_ERR_NONE) {
            if (outLen)  *outLen  = radio.getPacketLength();
            if (outRssi) *outRssi = radio.getRSSI();
            if (outSnr)  *outSnr  = radio.getSNR();
        }
        return st;
    }

}
#endif /* RADIO_MOCK */
