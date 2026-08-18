---
source_file: "test/test_core/test_main.c"
type: "code"
community: "VoSPI and CRC"
location: "L71"
tags:
  - graphify/code
  - graphify/EXTRACTED
  - community/VoSPI_and_CRC
---

# make_segment()

## Connections
- [[test_main.c]] - `contains` [EXTRACTED]
- [[test_vospi_assembles_four_segments()]] - `calls` [EXTRACTED]
- [[test_vospi_rejects_bad_crc()]] - `calls` [EXTRACTED]
- [[vospi_packet_crc()]] - `calls` [INFERRED]

#graphify/code #graphify/EXTRACTED #community/VoSPI_and_CRC