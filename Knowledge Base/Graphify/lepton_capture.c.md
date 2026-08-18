---
source_file: "src/lepton/lepton_capture.c"
type: "code"
community: "Lepton Capture State Machine"
location: "L1"
tags:
  - graphify/code
  - graphify/EXTRACTED
  - community/Lepton_Capture_State_Machine
---

# lepton_capture.c

## Connections
- [[HAL_GPIO_EXTI_Callback()]] - `contains` [EXTRACTED]
- [[HAL_SPI_ErrorCallback()]] - `contains` [EXTRACTED]
- [[HAL_SPI_RxCpltCallback()]] - `contains` [EXTRACTED]
- [[enter_state()]] - `contains` [EXTRACTED]
- [[lepton_capture_get_status()]] - `contains` [EXTRACTED]
- [[lepton_capture_init()]] - `contains` [EXTRACTED]
- [[lepton_capture_latest_frame()]] - `contains` [EXTRACTED]
- [[lepton_capture_run_ffc()]] - `contains` [EXTRACTED]
- [[lepton_capture_task()]] - `contains` [EXTRACTED]
- [[process_segments()]] - `contains` [EXTRACTED]
- [[start_segment_dma()]] - `contains` [EXTRACTED]
- [[stop_spi_and_resync()]] - `contains` [EXTRACTED]

#graphify/code #graphify/EXTRACTED #community/Lepton_Capture_State_Machine