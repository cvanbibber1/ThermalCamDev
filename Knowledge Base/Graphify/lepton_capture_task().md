---
source_file: "src/lepton/lepton_capture.c"
type: "code"
community: "Lepton Capture State Machine"
location: "L84"
tags:
  - graphify/code
  - graphify/INFERRED
  - community/Lepton_Capture_State_Machine
---

# lepton_capture_task()

## Connections
- [[board_lepton_cs()]] - `calls` [INFERRED]
- [[board_lepton_power()]] - `calls` [INFERRED]
- [[board_lepton_reset()]] - `calls` [INFERRED]
- [[enter_state()]] - `calls` [EXTRACTED]
- [[health_increment()]] - `calls` [INFERRED]
- [[lepton_capture.c]] - `contains` [EXTRACTED]
- [[lepton_cci_booted()]] - `calls` [INFERRED]
- [[lepton_cci_configure_radiometric()]] - `calls` [INFERRED]
- [[main()]] - `calls` [INFERRED]
- [[process_segments()]] - `calls` [EXTRACTED]
- [[start_segment_dma()]] - `calls` [EXTRACTED]
- [[stop_spi_and_resync()]] - `calls` [EXTRACTED]

#graphify/code #graphify/INFERRED #community/Lepton_Capture_State_Machine