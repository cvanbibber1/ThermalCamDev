---
source_file: "src/board/board.c"
type: "code"
community: "Board Clock and Peripherals"
location: "L266"
tags:
  - graphify/code
  - graphify/EXTRACTED
  - community/Board_Clock_and_Peripherals
---

# board_fatal()

## Connections
- [[BusFault_Handler()]] - `calls` [INFERRED]
- [[HAL_ADC_MspInit()]] - `calls` [EXTRACTED]
- [[HAL_SPI_MspInit()]] - `calls` [EXTRACTED]
- [[HAL_UART_MspInit()]] - `calls` [EXTRACTED]
- [[HardFault_Handler()]] - `calls` [INFERRED]
- [[MemManage_Handler()]] - `calls` [INFERRED]
- [[UsageFault_Handler()]] - `calls` [INFERRED]
- [[board.c]] - `contains` [EXTRACTED]
- [[board_lepton_cs()]] - `calls` [EXTRACTED]
- [[board_rs485_de()]] - `calls` [EXTRACTED]
- [[main()]] - `calls` [INFERRED]

#graphify/code #graphify/EXTRACTED #community/Board_Clock_and_Peripherals