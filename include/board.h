#pragma once

#include "stm32f4xx_hal.h"
#include <stdbool.h>
#include <stdint.h>

extern ADC_HandleTypeDef hadc1;
extern DMA_HandleTypeDef hdma_adc1;
extern I2C_HandleTypeDef hi2c1;
extern SPI_HandleTypeDef hspi2;
extern DMA_HandleTypeDef hdma_spi2_rx;
extern DMA_HandleTypeDef hdma_spi2_tx;
extern TIM_HandleTypeDef htim2;
extern UART_HandleTypeDef huart2;
extern DMA_HandleTypeDef hdma_usart2_rx;
extern DMA_HandleTypeDef hdma_usart2_tx;

bool board_init(void);
bool board_clock_valid(void);
uint32_t board_reset_cause(void);
uint32_t board_unique_id_word(uint8_t index);
void board_lepton_power(bool enabled);
void board_lepton_reset(bool released);
void board_lepton_cs(bool selected);
void board_lepton_spi_clock_hold(void);
void board_lepton_spi_clock_enable(void);
void board_rs485_de(bool enabled);

/* Independent watchdog.
 *
 * The experiment cannot be power cycled in orbit, so nothing may be able to
 * stop it permanently. The IWDG runs from the LSI, independently of the core
 * clock and of the interrupt mask, so it resets the part even from a fault
 * handler with interrupts disabled. Once started it cannot be stopped.
 *
 * The timeout is generous -- tens of seconds -- because the only thing that
 * legitimately stalls this firmware is a flash sector erase, which the
 * datasheet allows up to 3 seconds for. A false reset in flight would be far
 * worse than a slow recovery. */
void board_watchdog_init(void);
void board_watchdog_refresh(void);

/* Record an unrecoverable fault and wait for the watchdog to reset the part. */
void board_fatal(uint32_t code);

/* The fault code recorded before the last reset, or 0 if the part came up
 * cleanly. Read once at startup and reported in telemetry, so a watchdog reset
 * in orbit can be explained rather than merely noticed. */
uint32_t board_previous_fatal(void);
void board_persist_fatal(uint32_t code);
