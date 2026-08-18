---
source_file: "src/lepton/vospi_parser.c"
type: "code"
community: "VoSPI and CRC"
location: "L32"
tags:
  - graphify/code
  - graphify/EXTRACTED
  - community/VoSPI_and_CRC
---

# vospi_parse_segment()

## Connections
- [[packet_is_discard()]] - `calls` [EXTRACTED]
- [[process_segments()]] - `calls` [INFERRED]
- [[test_vospi_assembles_four_segments()]] - `calls` [INFERRED]
- [[test_vospi_rejects_bad_crc()]] - `calls` [INFERRED]
- [[vospi_packet_crc()]] - `calls` [EXTRACTED]
- [[vospi_parser.c]] - `contains` [EXTRACTED]
- [[vospi_parser_t]] - `references` [EXTRACTED]
- [[vospi_result_t]] - `references` [EXTRACTED]

#graphify/code #graphify/EXTRACTED #community/VoSPI_and_CRC