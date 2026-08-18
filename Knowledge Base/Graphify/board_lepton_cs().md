---
source_file: "src/board/board.c"
type: "code"
community: "Lepton Capture State Machine"
location: "L258"
tags:
  - graphify/code
  - graphify/INFERRED
  - community/Lepton_Capture_State_Machine
---

# board_lepton_cs()

## Connections
- [[HAL_SPI_ErrorCallback()]] - `calls` [INFERRED]
- [[HAL_SPI_RxCpltCallback()]] - `calls` [INFERRED]
- [[board.c]] - `contains` [EXTRACTED]
- [[board_fatal()]] - `calls` [EXTRACTED]
- [[lepton_capture_init()]] - `calls` [INFERRED]
- [[lepton_capture_task()]] - `calls` [INFERRED]
- [[stop_spi_and_resync()]] - `calls` [INFERRED]

#graphify/code #graphify/INFERRED #community/Lepton_Capture_State_Machine