---
type: community
cohesion: 0.14
members: 18
---

# Dosimeter ADC

**Cohesion:** 0.14 - loosely connected
**Members:** 18 nodes

## Members
- [[ADC_HandleTypeDef_1]] - code - src/drivers/dosimeter.c
- [[HAL_ADC_ConvCpltCallback()]] - code - src/drivers/dosimeter.c
- [[HAL_ADC_ConvHalfCpltCallback()]] - code - src/drivers/dosimeter.c
- [[board_reset_cause()]] - code - src/board/board.c
- [[command_dispatch_init()]] - code - src/protocol/command_dispatch.c
- [[dosimeter.c]] - code - src/drivers/dosimeter.c
- [[dosimeter_get_snapshot()]] - code - src/drivers/dosimeter.c
- [[dosimeter_init()]] - code - src/drivers/dosimeter.c
- [[dosimeter_snapshot_t]] - code - src/drivers/dosimeter.c
- [[dosimeter_task()]] - code - src/drivers/dosimeter.c
- [[integer_sqrt()]] - code - src/drivers/dosimeter.c
- [[main()]] - code - src/main.c
- [[main.c]] - code - src/main.c
- [[process_half()]] - code - src/drivers/dosimeter.c
- [[usb_device.c]] - code - src/usb/usb_device.c
- [[usb_device_configured()]] - code - src/usb/usb_device.c
- [[usb_device_init()]] - code - src/usb/usb_device.c
- [[usb_device_task()]] - code - src/usb/usb_device.c

## Live Query (requires Dataview plugin)

```dataview
TABLE source_file, type FROM #community/Dosimeter_ADC
SORT file.name ASC
```

## Connections to other communities
- 4 edges to [[_COMMUNITY_Lepton Capture State Machine]]
- 3 edges to [[_COMMUNITY_Board Clock and Peripherals]]
- 3 edges to [[_COMMUNITY_RS-485 Driver]]
- 2 edges to [[_COMMUNITY_Command Dispatcher]]

## Top bridge nodes
- [[main()]] - degree 13, connects to 3 communities
- [[dosimeter_get_snapshot()]] - degree 3, connects to 1 community
- [[HAL_ADC_ConvCpltCallback()]] - degree 3, connects to 1 community
- [[HAL_ADC_ConvHalfCpltCallback()]] - degree 3, connects to 1 community
- [[usb_device_task()]] - degree 3, connects to 1 community