---
source_file: "src/protocol/wire_protocol.c"
type: "code"
community: "RS-485 Driver"
location: "L106"
tags:
  - graphify/code
  - graphify/EXTRACTED
  - community/RS-485_Driver
---

# wire_stream_push()

## Connections
- [[rs485_task()]] - `calls` [INFERRED]
- [[test_wire_stream_delimiter()]] - `calls` [INFERRED]
- [[usb_cdc_task()]] - `calls` [INFERRED]
- [[wire_decode()]] - `calls` [EXTRACTED]
- [[wire_message_t_2]] - `references` [EXTRACTED]
- [[wire_protocol.c]] - `contains` [EXTRACTED]
- [[wire_stream_t]] - `references` [EXTRACTED]

#graphify/code #graphify/EXTRACTED #community/RS-485_Driver