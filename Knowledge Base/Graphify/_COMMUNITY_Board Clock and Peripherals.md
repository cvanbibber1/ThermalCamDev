---
type: community
cohesion: 0.07
members: 38
---

# Board Clock and Peripherals

**Cohesion:** 0.07 - loosely connected
**Members:** 38 nodes

## Members
- [[ADC_HandleTypeDef]] - code - src/board/board.c
- [[BusFault_Handler()]] - code - src/board/stm32f4xx_it.c
- [[DMA1_Stream3_IRQHandler()]] - code - src/board/stm32f4xx_it.c
- [[DMA1_Stream5_IRQHandler()]] - code - src/board/stm32f4xx_it.c
- [[DMA1_Stream6_IRQHandler()]] - code - src/board/stm32f4xx_it.c
- [[DMA2_Stream0_IRQHandler()]] - code - src/board/stm32f4xx_it.c
- [[DebugMon_Handler()]] - code - src/board/stm32f4xx_it.c
- [[EXTI15_10_IRQHandler()]] - code - src/board/stm32f4xx_it.c
- [[HAL_ADC_MspInit()]] - code - src/board/board.c
- [[HAL_I2C_MspInit()]] - code - src/board/board.c
- [[HAL_SPI_MspInit()]] - code - src/board/board.c
- [[HAL_TIM_Base_MspInit()]] - code - src/board/board.c
- [[HAL_UART_MspInit()]] - code - src/board/board.c
- [[HardFault_Handler()]] - code - src/board/stm32f4xx_it.c
- [[I2C_HandleTypeDef]] - code - src/board/board.c
- [[MemManage_Handler()]] - code - src/board/stm32f4xx_it.c
- [[NMI_Handler()]] - code - src/board/stm32f4xx_it.c
- [[OTG_FS_IRQHandler()]] - code - src/board/stm32f4xx_it.c
- [[PendSV_Handler()]] - code - src/board/stm32f4xx_it.c
- [[SPI_HandleTypeDef]] - code - src/board/board.c
- [[SVC_Handler()]] - code - src/board/stm32f4xx_it.c
- [[SysTick_Handler()]] - code - src/board/stm32f4xx_it.c
- [[TIM_HandleTypeDef]] - code - src/board/board.c
- [[UART_HandleTypeDef]] - code - src/board/board.c
- [[USART2_IRQHandler()]] - code - src/board/stm32f4xx_it.c
- [[UsageFault_Handler()]] - code - src/board/stm32f4xx_it.c
- [[adc_timer_init()]] - code - src/board/board.c
- [[board.c]] - code - src/board/board.c
- [[board_clock_valid()]] - code - src/board/board.c
- [[board_fatal()]] - code - src/board/board.c
- [[board_init()]] - code - src/board/board.c
- [[dma_init()]] - code - src/board/board.c
- [[gpio_init()]] - code - src/board/board.c
- [[i2c_init()]] - code - src/board/board.c
- [[spi_init()]] - code - src/board/board.c
- [[stm32f4xx_it.c]] - code - src/board/stm32f4xx_it.c
- [[system_clock_config()]] - code - src/board/board.c
- [[uart_init()]] - code - src/board/board.c

## Live Query (requires Dataview plugin)

```dataview
TABLE source_file, type FROM #community/Board_Clock_and_Peripherals
SORT file.name ASC
```

## Connections to other communities
- 5 edges to [[_COMMUNITY_Lepton Capture State Machine]]
- 3 edges to [[_COMMUNITY_Dosimeter ADC]]
- 2 edges to [[_COMMUNITY_RS-485 Driver]]
- 1 edge to [[_COMMUNITY_Command Dispatcher]]

## Top bridge nodes
- [[board.c]] - degree 21, connects to 4 communities
- [[board_fatal()]] - degree 11, connects to 3 communities
- [[board_init()]] - degree 11, connects to 2 communities