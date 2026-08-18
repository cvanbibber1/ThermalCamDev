---
type: community
cohesion: 0.10
members: 33
---

# RS-485 Driver

**Cohesion:** 0.10 - loosely connected
**Members:** 33 nodes

## Members
- [[HAL_UART_ErrorCallback()]] - code - src/drivers/rs485.c
- [[HAL_UART_TxCpltCallback()]] - code - src/drivers/rs485.c
- [[UART_HandleTypeDef_1]] - code - src/drivers/rs485.c
- [[begin_transmit()]] - code - src/drivers/rs485.c
- [[board_rs485_de()]] - code - src/board/board.c
- [[cdc_control()]] - code - src/usb/usb_cdc_if.c
- [[cdc_deinit()]] - code - src/usb/usb_cdc_if.c
- [[cdc_init()]] - code - src/usb/usb_cdc_if.c
- [[cdc_receive()]] - code - src/usb/usb_cdc_if.c
- [[cdc_transmit_complete()]] - code - src/usb/usb_cdc_if.c
- [[handle_message()]] - code - src/drivers/rs485.c
- [[rs485.c]] - code - src/drivers/rs485.c
- [[rs485_get_status()]] - code - src/drivers/rs485.c
- [[rs485_init()]] - code - src/drivers/rs485.c
- [[rs485_set_address()]] - code - src/drivers/rs485.c
- [[rs485_status_t]] - code - src/drivers/rs485.c
- [[rs485_task()]] - code - src/drivers/rs485.c
- [[test_wire_round_trip_and_crc_rejection()]] - code - test/test_core/test_main.c
- [[test_wire_stream_delimiter()]] - code - test/test_core/test_main.c
- [[transmit_response()]] - code - src/usb/usb_cdc_if.c
- [[usb_cdc_if.c]] - code - src/usb/usb_cdc_if.c
- [[usb_cdc_task()]] - code - src/usb/usb_cdc_if.c
- [[wire_decode()]] - code - src/protocol/wire_protocol.c
- [[wire_encode()]] - code - src/protocol/wire_protocol.c
- [[wire_get_u32()]] - code - src/protocol/wire_protocol.c
- [[wire_message_t]] - code - src/drivers/rs485.c
- [[wire_message_t_2]] - code - src/protocol/wire_protocol.c
- [[wire_message_t_3]] - code - src/usb/usb_cdc_if.c
- [[wire_protocol.c]] - code - src/protocol/wire_protocol.c
- [[wire_put_u32()]] - code - src/protocol/wire_protocol.c
- [[wire_stream_init()]] - code - src/protocol/wire_protocol.c
- [[wire_stream_push()]] - code - src/protocol/wire_protocol.c
- [[wire_stream_t]] - code - src/protocol/wire_protocol.c

## Live Query (requires Dataview plugin)

```dataview
TABLE source_file, type FROM #community/RS-485_Driver
SORT file.name ASC
```

## Connections to other communities
- 11 edges to [[_COMMUNITY_Command Dispatcher]]
- 4 edges to [[_COMMUNITY_Lepton Capture State Machine]]
- 3 edges to [[_COMMUNITY_Dosimeter ADC]]
- 2 edges to [[_COMMUNITY_Board Clock and Peripherals]]
- 2 edges to [[_COMMUNITY_VoSPI and CRC]]

## Top bridge nodes
- [[usb_cdc_task()]] - degree 5, connects to 2 communities
- [[wire_encode()]] - degree 8, connects to 1 community
- [[wire_protocol.c]] - degree 8, connects to 1 community
- [[begin_transmit()]] - degree 7, connects to 1 community
- [[board_rs485_de()]] - degree 6, connects to 1 community