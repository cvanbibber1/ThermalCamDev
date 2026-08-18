---
type: community
cohesion: 0.19
members: 18
---

# VoSPI and CRC

**Cohesion:** 0.19 - loosely connected
**Members:** 18 nodes

## Members
- [[crc.c]] - code - src/protocol/crc.c
- [[crc16_ccitt()]] - code - src/protocol/crc.c
- [[crc32c()]] - code - src/protocol/crc.c
- [[main()_1]] - code - test/test_core/test_main.c
- [[make_segment()]] - code - test/test_core/test_main.c
- [[packet_is_discard()]] - code - src/lepton/vospi_parser.c
- [[test_cobs_round_trip()]] - code - test/test_core/test_main.c
- [[test_crc_known_vectors()]] - code - test/test_core/test_main.c
- [[test_main.c]] - code - test/test_core/test_main.c
- [[test_vospi_assembles_four_segments()]] - code - test/test_core/test_main.c
- [[test_vospi_rejects_bad_crc()]] - code - test/test_core/test_main.c
- [[vospi_latest_frame()]] - code - src/lepton/vospi_parser.c
- [[vospi_packet_crc()]] - code - src/lepton/vospi_parser.c
- [[vospi_parse_segment()]] - code - src/lepton/vospi_parser.c
- [[vospi_parser.c]] - code - src/lepton/vospi_parser.c
- [[vospi_parser_init()]] - code - src/lepton/vospi_parser.c
- [[vospi_parser_t]] - code - src/lepton/vospi_parser.c
- [[vospi_result_t]] - code - src/lepton/vospi_parser.c

## Live Query (requires Dataview plugin)

```dataview
TABLE source_file, type FROM #community/VoSPI_and_CRC
SORT file.name ASC
```

## Connections to other communities
- 3 edges to [[_COMMUNITY_Lepton Capture State Machine]]
- 2 edges to [[_COMMUNITY_RS-485 Driver]]
- 1 edge to [[_COMMUNITY_Command Dispatcher]]

## Top bridge nodes
- [[vospi_parse_segment()]] - degree 8, connects to 1 community
- [[test_main.c]] - degree 8, connects to 1 community
- [[vospi_parser_init()]] - degree 6, connects to 1 community
- [[vospi_latest_frame()]] - degree 4, connects to 1 community