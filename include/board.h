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
void board_fatal(uint32_t code);
