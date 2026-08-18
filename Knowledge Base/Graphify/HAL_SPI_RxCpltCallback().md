---
source_file: "src/lepton/lepton_capture.c"
type: "code"
community: "Lepton Capture State Machine"
location: "L181"
tags:
  - graphify/code
  - graphify/EXTRACTED
  - community/Lepton_Capture_State_Machine
---

# HAL_SPI_RxCpltCallback()

## Connections
- [[SPI_HandleTypeDef_1]] - `references` [EXTRACTED]
- [[board_lepton_cs()]] - `calls` [INFERRED]
- [[health_increment()]] - `calls` [INFERRED]
- [[lepton_capture.c]] - `contains` [EXTRACTED]
- [[start_segment_dma()]] - `calls` [EXTRACTED]

#graphify/code #graphify/EXTRACTED #community/Lepton_Capture_State_Machine