---
type: community
cohesion: 0.35
members: 22
---

# Command Dispatcher

**Cohesion:** 0.35 - loosely connected
**Members:** 22 nodes

## Members
- [[append_u32()]] - code - src/protocol/command_dispatch.c
- [[assign_bus_address()]] - code - src/protocol/command_dispatch.c
- [[begin_response()]] - code - src/protocol/command_dispatch.c
- [[board_unique_id_word()]] - code - src/board/board.c
- [[cci_get()]] - code - src/protocol/command_dispatch.c
- [[cci_run()]] - code - src/protocol/command_dispatch.c
- [[cci_set()]] - code - src/protocol/command_dispatch.c
- [[command_dispatch()]] - code - src/protocol/command_dispatch.c
- [[command_dispatch.c]] - code - src/protocol/command_dispatch.c
- [[dosimeter_status()]] - code - src/protocol/command_dispatch.c
- [[frame_chunk()]] - code - src/protocol/command_dispatch.c
- [[get_health()]] - code - src/protocol/command_dispatch.c
- [[get_info()]] - code - src/protocol/command_dispatch.c
- [[get_lepton_status()]] - code - src/protocol/command_dispatch.c
- [[lepton_capture_latest_frame()]] - code - src/lepton/lepton_capture.c
- [[register_read()]] - code - src/protocol/command_dispatch.c
- [[register_write()]] - code - src/protocol/command_dispatch.c
- [[set_result()]] - code - src/protocol/command_dispatch.c
- [[stream_status()]] - code - src/protocol/command_dispatch.c
- [[wire_get_u16()]] - code - src/protocol/wire_protocol.c
- [[wire_message_t_1]] - code - src/protocol/command_dispatch.c
- [[wire_put_u16()]] - code - src/protocol/wire_protocol.c

## Live Query (requires Dataview plugin)

```dataview
TABLE source_file, type FROM #community/Command_Dispatcher
SORT file.name ASC
```

## Connections to other communities
- 11 edges to [[_COMMUNITY_RS-485 Driver]]
- 8 edges to [[_COMMUNITY_Lepton Capture State Machine]]
- 2 edges to [[_COMMUNITY_Dosimeter ADC]]
- 1 edge to [[_COMMUNITY_Board Clock and Peripherals]]
- 1 edge to [[_COMMUNITY_USB Descriptors]]
- 1 edge to [[_COMMUNITY_VoSPI and CRC]]
- 1 edge to [[_COMMUNITY_UVC Frame Publisher]]

## Top bridge nodes
- [[board_unique_id_word()]] - degree 5, connects to 3 communities
- [[lepton_capture_latest_frame()]] - degree 5, connects to 3 communities
- [[command_dispatch()]] - degree 20, connects to 2 communities
- [[command_dispatch.c]] - degree 17, connects to 1 community
- [[append_u32()]] - degree 10, connects to 1 community