#include "board.h"

#include "app_config.h"
#include "health.h"

ADC_HandleTypeDef hadc1;
DMA_HandleTypeDef hdma_adc1;
I2C_HandleTypeDef hi2c1;
SPI_HandleTypeDef hspi2;
DMA_HandleTypeDef hdma_spi2_rx;
DMA_HandleTypeDef hdma_spi2_tx;
TIM_HandleTypeDef htim2;
UART_HandleTypeDef huart2;
DMA_HandleTypeDef hdma_usart2_rx;
DMA_HandleTypeDef hdma_usart2_tx;

static uint32_t saved_reset_cause;

static bool system_clock_config(void);
static bool gpio_init(void);
static bool dma_init(void);
static bool i2c_init(void);
static bool spi_init(void);
static bool adc_timer_init(void);
static bool uart_init(void);

bool board_init(void) {
  saved_reset_cause = RCC->CSR;
  __HAL_RCC_CLEAR_RESET_FLAGS();

  if (!gpio_init()) {
    g_health.fatal_code = 0xB101U;
    return false;
  }
  if (!system_clock_config()) {
    health_increment(&g_health.clock_failures);
    g_health.fatal_code = 0xB102U;
    return false;
  }
  if (!dma_init()) {
    g_health.fatal_code = 0xB103U;
    return false;
  }
  if (!i2c_init()) {
    g_health.fatal_code = 0xB104U;
    return false;
  }
  if (!spi_init()) {
    g_health.fatal_code = 0xB105U;
    return false;
  }
  if (!adc_timer_init()) {
    g_health.fatal_code = 0xB106U;
    return false;
  }
  if (!uart_init()) {
    g_health.fatal_code = 0xB107U;
    return false;
  }
  if (!board_clock_valid()) {
    health_increment(&g_health.clock_failures);
    g_health.fatal_code = 0xB108U;
    return false;
  }
  return true;
}

static bool system_clock_config(void) {
  RCC_OscInitTypeDef osc = {0};
  RCC_ClkInitTypeDef clocks = {0};
  RCC_PeriphCLKInitTypeDef peripheral = {0};

  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  osc.HSEState = RCC_HSE_BYPASS;
  osc.PLL.PLLState = RCC_PLL_ON;
  osc.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  osc.PLL.PLLM = 8U;
  osc.PLL.PLLN = 200U;
  osc.PLL.PLLP = RCC_PLLP_DIV2;
  osc.PLL.PLLQ = 4U;
  osc.PLL.PLLR = 2U;
  if (HAL_RCC_OscConfig(&osc) != HAL_OK) {
    return false;
  }

  clocks.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                     RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
  clocks.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  clocks.AHBCLKDivider = RCC_SYSCLK_DIV1;
  clocks.APB1CLKDivider = RCC_HCLK_DIV2;
  clocks.APB2CLKDivider = RCC_HCLK_DIV1;
  if (HAL_RCC_ClockConfig(&clocks, FLASH_LATENCY_3) != HAL_OK) {
    return false;
  }

  peripheral.PeriphClockSelection = RCC_PERIPHCLK_CLK48;
  peripheral.PLLI2S.PLLI2SM = 8U;
  peripheral.PLLI2S.PLLI2SN = 192U;
  peripheral.PLLI2S.PLLI2SQ = 4U;
  peripheral.PLLI2S.PLLI2SR = 2U;
  peripheral.Clk48ClockSelection = RCC_CLK48CLKSOURCE_PLLI2SQ;
  if (HAL_RCCEx_PeriphCLKConfig(&peripheral) != HAL_OK) {
    return false;
  }

  HAL_SYSTICK_Config(HAL_RCC_GetHCLKFreq() / 1000U);
  HAL_SYSTICK_CLKSourceConfig(SYSTICK_CLKSOURCE_HCLK);
  HAL_NVIC_SetPriority(SysTick_IRQn, 15U, 0U);
  return true;
}

bool board_clock_valid(void) {
  uint32_t plli2s;
  uint32_t m;
  uint32_t n;
  uint32_t q;
  uint32_t source_hz;
  uint64_t clk48_hz;

  if (HAL_RCC_GetSysClockFreq() != APP_SYSCLK_HZ) {
    return false;
  }

  /* HAL_RCCEx_GetPeriphCLKFreq() does not implement RCC_PERIPHCLK_CLK48
   * for STM32F412 and returns zero. Validate the selected PLLI2S-Q clock
   * directly so a healthy USB clock does not produce a false fatal error. */
  if ((RCC->CR & RCC_CR_PLLI2SRDY) == 0U ||
      __HAL_RCC_GET_CLK48_SOURCE() != RCC_CLK48CLKSOURCE_PLLI2SQ) {
    return false;
  }

  plli2s = RCC->PLLI2SCFGR;
  m = (plli2s & RCC_PLLI2SCFGR_PLLI2SM) >> RCC_PLLI2SCFGR_PLLI2SM_Pos;
  n = (plli2s & RCC_PLLI2SCFGR_PLLI2SN) >> RCC_PLLI2SCFGR_PLLI2SN_Pos;
  q = (plli2s & RCC_PLLI2SCFGR_PLLI2SQ) >> RCC_PLLI2SCFGR_PLLI2SQ_Pos;
  if (m == 0U || q == 0U) {
    return false;
  }

  source_hz = ((RCC->PLLCFGR & RCC_PLLCFGR_PLLSRC) != 0U) ? HSE_VALUE : HSI_VALUE;
  clk48_hz = ((uint64_t)source_hz * (uint64_t)n) / ((uint64_t)m * (uint64_t)q);
  return clk48_hz == APP_USBCLK_HZ;
}

static bool gpio_init(void) {
  GPIO_InitTypeDef gpio = {0};

  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_SYSCFG_CLK_ENABLE();

  /* Safe output levels must be established before changing pin modes. */
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_8 | GPIO_PIN_9, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_7 | GPIO_PIN_15, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_12, GPIO_PIN_SET);

  gpio.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_4 | GPIO_PIN_7;
  gpio.Mode = GPIO_MODE_ANALOG;
  gpio.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOA, &gpio);

  gpio.Pin = GPIO_PIN_8 | GPIO_PIN_9;
  gpio.Mode = GPIO_MODE_OUTPUT_PP;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &gpio);

  gpio.Pin = GPIO_PIN_7 | GPIO_PIN_12 | GPIO_PIN_15;
  gpio.Mode = GPIO_MODE_OUTPUT_PP;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOB, &gpio);

  gpio.Pin = GPIO_PIN_13;
  gpio.Mode = GPIO_MODE_IT_RISING;
  gpio.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOC, &gpio);
  HAL_NVIC_SetPriority(EXTI15_10_IRQn, 3U, 0U);
  HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);
  return true;
}

static bool dma_init(void) {
  __HAL_RCC_DMA1_CLK_ENABLE();
  __HAL_RCC_DMA2_CLK_ENABLE();

  HAL_NVIC_SetPriority(DMA1_Stream3_IRQn, 2U, 0U);
  HAL_NVIC_EnableIRQ(DMA1_Stream3_IRQn);
  HAL_NVIC_SetPriority(DMA1_Stream4_IRQn, 2U, 0U);
  HAL_NVIC_EnableIRQ(DMA1_Stream4_IRQn);
  HAL_NVIC_SetPriority(DMA1_Stream5_IRQn, 8U, 0U);
  HAL_NVIC_EnableIRQ(DMA1_Stream5_IRQn);
  HAL_NVIC_SetPriority(DMA1_Stream6_IRQn, 8U, 0U);
  HAL_NVIC_EnableIRQ(DMA1_Stream6_IRQn);
  HAL_NVIC_SetPriority(DMA2_Stream0_IRQn, 7U, 0U);
  HAL_NVIC_EnableIRQ(DMA2_Stream0_IRQn);
  return true;
}

static bool i2c_init(void) {
  hi2c1.Instance = I2C1;
  hi2c1.Init.ClockSpeed = APP_LEPTON_I2C_HZ;
  hi2c1.Init.DutyCycle = I2C_DUTYCYCLE_2;
  hi2c1.Init.OwnAddress1 = 0U;
  hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c1.Init.OwnAddress2 = 0U;
  hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  return HAL_I2C_Init(&hi2c1) == HAL_OK;
}

static bool spi_init(void) {
  hspi2.Instance = SPI2;
  hspi2.Init.Mode = SPI_MODE_MASTER;
  hspi2.Init.Direction = SPI_DIRECTION_2LINES;
  hspi2.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi2.Init.CLKPolarity = SPI_POLARITY_HIGH;
  hspi2.Init.CLKPhase = SPI_PHASE_2EDGE;
  hspi2.Init.NSS = SPI_NSS_SOFT;
  hspi2.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_4;
  hspi2.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi2.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi2.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi2.Init.CRCPolynomial = 7U;
  return HAL_SPI_Init(&hspi2) == HAL_OK;
}

static bool adc_timer_init(void) {
  ADC_ChannelConfTypeDef channel = {0};
  TIM_MasterConfigTypeDef master = {0};

  hadc1.Instance = ADC1;
  hadc1.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV4;
  hadc1.Init.Resolution = ADC_RESOLUTION_12B;
  hadc1.Init.ScanConvMode = ENABLE;
  hadc1.Init.ContinuousConvMode = DISABLE;
  hadc1.Init.DiscontinuousConvMode = DISABLE;
  hadc1.Init.NbrOfDiscConversion = 0U;
  hadc1.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_RISING;
  hadc1.Init.ExternalTrigConv = ADC_EXTERNALTRIGCONV_T2_TRGO;
  hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
  hadc1.Init.NbrOfConversion = 2U;
  hadc1.Init.DMAContinuousRequests = ENABLE;
  hadc1.Init.EOCSelection = ADC_EOC_SEQ_CONV;
  if (HAL_ADC_Init(&hadc1) != HAL_OK) {
    return false;
  }

  channel.Channel = ADC_CHANNEL_4;
  channel.Rank = 1U;
  channel.SamplingTime = ADC_SAMPLETIME_144CYCLES;
  if (HAL_ADC_ConfigChannel(&hadc1, &channel) != HAL_OK) {
    return false;
  }
  channel.Channel = ADC_CHANNEL_VREFINT;
  channel.Rank = 2U;
  channel.SamplingTime = ADC_SAMPLETIME_480CYCLES;
  if (HAL_ADC_ConfigChannel(&hadc1, &channel) != HAL_OK) {
    return false;
  }

  htim2.Instance = TIM2;
  htim2.Init.Prescaler = 99U;
  htim2.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim2.Init.Period = 999U;
  htim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim2) != HAL_OK) {
    return false;
  }
  master.MasterOutputTrigger = TIM_TRGO_UPDATE;
  master.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  return HAL_TIMEx_MasterConfigSynchronization(&htim2, &master) == HAL_OK;
}

static bool uart_init(void) {
  huart2.Instance = USART2;
  huart2.Init.BaudRate = APP_RS485_BAUD;
  huart2.Init.WordLength = UART_WORDLENGTH_8B;
  huart2.Init.StopBits = UART_STOPBITS_1;
  huart2.Init.Parity = UART_PARITY_NONE;
  huart2.Init.Mode = UART_MODE_TX_RX;
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
  return HAL_UART_Init(&huart2) == HAL_OK;
}

uint32_t board_reset_cause(void) {
  return saved_reset_cause;
}

uint32_t board_unique_id_word(uint8_t index) {
  static const uint32_t *const uid = (const uint32_t *)UID_BASE;
  return (index < 3U) ? uid[index] : 0U;
}

void board_lepton_power(bool enabled) {
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_8, enabled ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

void board_lepton_reset(bool released) {
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_9, released ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

void board_lepton_cs(bool selected) {
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_12, selected ? GPIO_PIN_RESET : GPIO_PIN_SET);
}

void board_lepton_spi_clock_hold(void) {
  GPIO_InitTypeDef gpio = {0};
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_13, GPIO_PIN_SET);
  gpio.Pin = GPIO_PIN_13;
  gpio.Mode = GPIO_MODE_OUTPUT_PP;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  HAL_GPIO_Init(GPIOB, &gpio);
}

void board_lepton_spi_clock_enable(void) {
  GPIO_InitTypeDef gpio = {0};
  gpio.Pin = GPIO_PIN_13;
  gpio.Mode = GPIO_MODE_AF_PP;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  gpio.Alternate = GPIO_AF5_SPI2;
  HAL_GPIO_Init(GPIOB, &gpio);
}

void board_rs485_de(bool enabled) {
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_7, enabled ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

static IWDG_HandleTypeDef hiwdg;

void board_watchdog_init(void) {
  /* LSI is nominally 32 kHz but specified over 17 to 47 kHz, so the real
   * timeout spans roughly 11 to 31 seconds at this setting. The slow end is
   * acceptable and the fast end still leaves several times the longest
   * legitimate stall, which is a flash sector erase. */
  hiwdg.Instance = IWDG;
  hiwdg.Init.Prescaler = IWDG_PRESCALER_256;
  hiwdg.Init.Reload = 2047U;
  /* Stop the counter while a debugger has the core halted, or every breakpoint
   * would reset the part. This bit does nothing without a debugger attached. */
  __HAL_DBGMCU_FREEZE_IWDG();
  if (HAL_IWDG_Init(&hiwdg) != HAL_OK) {
    /* Nothing to be done: without the watchdog the firmware still runs, and
     * refusing to start would be worse than running unprotected. */
    g_health.fatal_code = 0xB201U;
  }
}

void board_watchdog_refresh(void) {
  (void)HAL_IWDG_Refresh(&hiwdg);
}

void board_fatal(uint32_t code) {
  g_health.fatal_code = code;
  /* Let go of the bus and the sensor before stopping, so a dead experiment
   * cannot hold the shared RS-422 line down or leave the Lepton selected. */
  board_rs485_de(false);
  board_lepton_cs(false);
  __disable_irq();
  /* Stop refreshing and wait. The watchdog is independent of the interrupt
   * mask and of the core clock, so it resets the part from here; this is a
   * pause of tens of seconds, not the end of the mission. The reset cause in
   * telemetry will show the watchdog fired. */
  for (;;) {
    __WFI();
  }
}

void HAL_I2C_MspInit(I2C_HandleTypeDef *handle) {
  GPIO_InitTypeDef gpio = {0};
  if (handle->Instance != I2C1) {
    return;
  }
  __HAL_RCC_I2C1_CLK_ENABLE();
  gpio.Pin = GPIO_PIN_8 | GPIO_PIN_9;
  gpio.Mode = GPIO_MODE_AF_OD;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  gpio.Alternate = GPIO_AF4_I2C1;
  HAL_GPIO_Init(GPIOB, &gpio);
}

void HAL_SPI_MspInit(SPI_HandleTypeDef *handle) {
  GPIO_InitTypeDef gpio = {0};
  if (handle->Instance != SPI2) {
    return;
  }
  __HAL_RCC_SPI2_CLK_ENABLE();
  gpio.Pin = GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15;
  gpio.Mode = GPIO_MODE_AF_PP;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  gpio.Alternate = GPIO_AF5_SPI2;
  HAL_GPIO_Init(GPIOB, &gpio);

  hdma_spi2_rx.Instance = DMA1_Stream3;
  hdma_spi2_rx.Init.Channel = DMA_CHANNEL_0;
  hdma_spi2_rx.Init.Direction = DMA_PERIPH_TO_MEMORY;
  hdma_spi2_rx.Init.PeriphInc = DMA_PINC_DISABLE;
  hdma_spi2_rx.Init.MemInc = DMA_MINC_ENABLE;
  hdma_spi2_rx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
  hdma_spi2_rx.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
  /* Circular so VoSPI clocking never pauses between chunks; the Lepton
   * invalidates segments when the host stops reading mid-frame. */
  hdma_spi2_rx.Init.Mode = DMA_CIRCULAR;
  hdma_spi2_rx.Init.Priority = DMA_PRIORITY_VERY_HIGH;
  hdma_spi2_rx.Init.FIFOMode = DMA_FIFOMODE_DISABLE;
  hdma_spi2_rx.Init.FIFOThreshold = DMA_FIFO_THRESHOLD_1QUARTERFULL;
  hdma_spi2_rx.Init.MemBurst = DMA_MBURST_SINGLE;
  hdma_spi2_rx.Init.PeriphBurst = DMA_PBURST_SINGLE;
  if (HAL_DMA_Init(&hdma_spi2_rx) != HAL_OK) {
    board_fatal(0xD201U);
  }
  __HAL_LINKDMA(handle, hdmarx, hdma_spi2_rx);

  hdma_spi2_tx.Instance = DMA1_Stream4;
  hdma_spi2_tx.Init.Channel = DMA_CHANNEL_0;
  hdma_spi2_tx.Init.Direction = DMA_MEMORY_TO_PERIPH;
  hdma_spi2_tx.Init.PeriphInc = DMA_PINC_DISABLE;
  hdma_spi2_tx.Init.MemInc = DMA_MINC_DISABLE;
  hdma_spi2_tx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
  hdma_spi2_tx.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
  hdma_spi2_tx.Init.Mode = DMA_CIRCULAR;
  hdma_spi2_tx.Init.Priority = DMA_PRIORITY_VERY_HIGH;
  hdma_spi2_tx.Init.FIFOMode = DMA_FIFOMODE_DISABLE;
  hdma_spi2_tx.Init.FIFOThreshold = DMA_FIFO_THRESHOLD_1QUARTERFULL;
  hdma_spi2_tx.Init.MemBurst = DMA_MBURST_SINGLE;
  hdma_spi2_tx.Init.PeriphBurst = DMA_PBURST_SINGLE;
  if (HAL_DMA_Init(&hdma_spi2_tx) != HAL_OK) {
    board_fatal(0xD204U);
  }
  __HAL_LINKDMA(handle, hdmatx, hdma_spi2_tx);
}

void HAL_ADC_MspInit(ADC_HandleTypeDef *handle) {
  if (handle->Instance != ADC1) {
    return;
  }
  __HAL_RCC_ADC1_CLK_ENABLE();
  hdma_adc1.Instance = DMA2_Stream0;
  hdma_adc1.Init.Channel = DMA_CHANNEL_0;
  hdma_adc1.Init.Direction = DMA_PERIPH_TO_MEMORY;
  hdma_adc1.Init.PeriphInc = DMA_PINC_DISABLE;
  hdma_adc1.Init.MemInc = DMA_MINC_ENABLE;
  hdma_adc1.Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD;
  hdma_adc1.Init.MemDataAlignment = DMA_MDATAALIGN_HALFWORD;
  hdma_adc1.Init.Mode = DMA_CIRCULAR;
  hdma_adc1.Init.Priority = DMA_PRIORITY_MEDIUM;
  hdma_adc1.Init.FIFOMode = DMA_FIFOMODE_DISABLE;
  if (HAL_DMA_Init(&hdma_adc1) != HAL_OK) {
    board_fatal(0xD101U);
  }
  __HAL_LINKDMA(handle, DMA_Handle, hdma_adc1);
}

void HAL_TIM_Base_MspInit(TIM_HandleTypeDef *handle) {
  if (handle->Instance == TIM2) {
    __HAL_RCC_TIM2_CLK_ENABLE();
  }
}

void HAL_UART_MspInit(UART_HandleTypeDef *handle) {
  GPIO_InitTypeDef gpio = {0};
  if (handle->Instance != USART2) {
    return;
  }
  __HAL_RCC_USART2_CLK_ENABLE();
  gpio.Pin = GPIO_PIN_2 | GPIO_PIN_3;
  gpio.Mode = GPIO_MODE_AF_PP;
  gpio.Pull = GPIO_PULLUP;
  gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  gpio.Alternate = GPIO_AF7_USART2;
  HAL_GPIO_Init(GPIOA, &gpio);

  hdma_usart2_rx.Instance = DMA1_Stream5;
  hdma_usart2_rx.Init.Channel = DMA_CHANNEL_4;
  hdma_usart2_rx.Init.Direction = DMA_PERIPH_TO_MEMORY;
  hdma_usart2_rx.Init.PeriphInc = DMA_PINC_DISABLE;
  hdma_usart2_rx.Init.MemInc = DMA_MINC_ENABLE;
  hdma_usart2_rx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
  hdma_usart2_rx.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
  hdma_usart2_rx.Init.Mode = DMA_CIRCULAR;
  hdma_usart2_rx.Init.Priority = DMA_PRIORITY_HIGH;
  hdma_usart2_rx.Init.FIFOMode = DMA_FIFOMODE_DISABLE;
  if (HAL_DMA_Init(&hdma_usart2_rx) != HAL_OK) {
    board_fatal(0xD202U);
  }
  __HAL_LINKDMA(handle, hdmarx, hdma_usart2_rx);

  hdma_usart2_tx.Instance = DMA1_Stream6;
  hdma_usart2_tx.Init.Channel = DMA_CHANNEL_4;
  hdma_usart2_tx.Init.Direction = DMA_MEMORY_TO_PERIPH;
  hdma_usart2_tx.Init.PeriphInc = DMA_PINC_DISABLE;
  hdma_usart2_tx.Init.MemInc = DMA_MINC_ENABLE;
  hdma_usart2_tx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
  hdma_usart2_tx.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
  hdma_usart2_tx.Init.Mode = DMA_NORMAL;
  hdma_usart2_tx.Init.Priority = DMA_PRIORITY_HIGH;
  hdma_usart2_tx.Init.FIFOMode = DMA_FIFOMODE_DISABLE;
  if (HAL_DMA_Init(&hdma_usart2_tx) != HAL_OK) {
    board_fatal(0xD203U);
  }
  __HAL_LINKDMA(handle, hdmatx, hdma_usart2_tx);

  HAL_NVIC_SetPriority(USART2_IRQn, 8U, 0U);
  HAL_NVIC_EnableIRQ(USART2_IRQn);
}
