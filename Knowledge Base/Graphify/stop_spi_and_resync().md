---
source_file: "src/lepton/lepton_capture.c"
type: "code"
community: "Lepton Capture State Machine"
location: "L26"
tags:
  - graphify/code
  - graphify/EXTRACTED
  - community/Lepton_Capture_State_Machine
---

# stop_spi_and_resync()

## Connections
- [[board_lepton_cs()]] - `calls` [INFERRED]
- [[enter_state()]] - `calls` [EXTRACTED]
- [[health_increment()]] - `calls` [INFERRED]
- [[lepton_capture.c]] - `contains` [EXTRACTED]
- [[lepton_capture_task()]] - `calls` [EXTRACTED]
- [[process_segments()]] - `calls` [EXTRACTED]
- [[vospi_parser_init()]] - `calls` [INFERRED]

#graphify/code #graphify/EXTRACTED #community/Lepton_Capture_State_Machine