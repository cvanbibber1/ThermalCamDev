---
source_file: "src/protocol/wire_protocol.c"
type: "code"
community: "RS-485 Driver"
location: "L29"
tags:
  - graphify/code
  - graphify/INFERRED
  - community/RS-485_Driver
---

# wire_encode()

## Connections
- [[begin_transmit()]] - `calls` [INFERRED]
- [[test_wire_round_trip_and_crc_rejection()]] - `calls` [INFERRED]
- [[test_wire_stream_delimiter()]] - `calls` [INFERRED]
- [[transmit_response()]] - `calls` [INFERRED]
- [[wire_message_t_2]] - `references` [EXTRACTED]
- [[wire_protocol.c]] - `contains` [EXTRACTED]
- [[wire_put_u16()]] - `calls` [EXTRACTED]
- [[wire_put_u32()]] - `calls` [EXTRACTED]

#graphify/code #graphify/INFERRED #community/RS-485_Driver