---
source_file: "src/health.c"
type: "code"
community: "Lepton Capture State Machine"
location: "L7"
tags:
  - graphify/code
  - graphify/INFERRED
  - community/Lepton_Capture_State_Machine
---

# health_increment()

## Connections
- [[HAL_ADC_ConvCpltCallback()]] - `calls` [INFERRED]
- [[HAL_ADC_ConvHalfCpltCallback()]] - `calls` [INFERRED]
- [[HAL_SPI_ErrorCallback()]] - `calls` [INFERRED]
- [[HAL_SPI_RxCpltCallback()]] - `calls` [INFERRED]
- [[HAL_UART_ErrorCallback()]] - `calls` [INFERRED]
- [[begin_transmit()]] - `calls` [INFERRED]
- [[board_init()]] - `calls` [INFERRED]
- [[cdc_receive()]] - `calls` [INFERRED]
- [[health.c]] - `contains` [EXTRACTED]
- [[lepton_capture_task()]] - `calls` [INFERRED]
- [[process_segments()]] - `calls` [INFERRED]
- [[read_bytes()]] - `calls` [INFERRED]
- [[stop_spi_and_resync()]] - `calls` [INFERRED]
- [[transmit_response()]] - `calls` [INFERRED]
- [[wait_ready()]] - `calls` [INFERRED]
- [[write_bytes()]] - `calls` [INFERRED]

#graphify/code #graphify/INFERRED #community/Lepton_Capture_State_Machine