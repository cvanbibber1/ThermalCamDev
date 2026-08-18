---
type: community
cohesion: 0.14
members: 33
---

# Lepton Capture State Machine

**Cohesion:** 0.14 - loosely connected
**Members:** 33 nodes

## Members
- [[HAL_GPIO_EXTI_Callback()]] - code - src/lepton/lepton_capture.c
- [[HAL_SPI_ErrorCallback()]] - code - src/lepton/lepton_capture.c
- [[HAL_SPI_RxCpltCallback()]] - code - src/lepton/lepton_capture.c
- [[SPI_HandleTypeDef_1]] - code - src/lepton/lepton_capture.c
- [[board_lepton_cs()]] - code - src/board/board.c
- [[board_lepton_power()]] - code - src/board/board.c
- [[board_lepton_reset()]] - code - src/board/board.c
- [[enter_state()]] - code - src/lepton/lepton_capture.c
- [[health.c]] - code - src/health.c
- [[health_increment()]] - code - src/health.c
- [[lepton_capture.c]] - code - src/lepton/lepton_capture.c
- [[lepton_capture_get_status()]] - code - src/lepton/lepton_capture.c
- [[lepton_capture_init()]] - code - src/lepton/lepton_capture.c
- [[lepton_capture_run_ffc()]] - code - src/lepton/lepton_capture.c
- [[lepton_capture_status_t]] - code - src/lepton/lepton_capture.c
- [[lepton_capture_task()]] - code - src/lepton/lepton_capture.c
- [[lepton_cci.c]] - code - src/lepton/lepton_cci.c
- [[lepton_cci_booted()]] - code - src/lepton/lepton_cci.c
- [[lepton_cci_configure_radiometric()]] - code - src/lepton/lepton_cci.c
- [[lepton_cci_get()]] - code - src/lepton/lepton_cci.c
- [[lepton_cci_read_register()]] - code - src/lepton/lepton_cci.c
- [[lepton_cci_run()]] - code - src/lepton/lepton_cci.c
- [[lepton_cci_set()]] - code - src/lepton/lepton_cci.c
- [[lepton_cci_write_register()]] - code - src/lepton/lepton_cci.c
- [[lepton_state_t]] - code - src/lepton/lepton_capture.c
- [[process_segments()]] - code - src/lepton/lepton_capture.c
- [[read_bytes()]] - code - src/lepton/lepton_cci.c
- [[start_segment_dma()]] - code - src/lepton/lepton_capture.c
- [[stop_spi_and_resync()]] - code - src/lepton/lepton_capture.c
- [[wait_ready()]] - code - src/lepton/lepton_cci.c
- [[words_from_camera()]] - code - src/lepton/lepton_cci.c
- [[words_to_camera()]] - code - src/lepton/lepton_cci.c
- [[write_bytes()]] - code - src/lepton/lepton_cci.c

## Live Query (requires Dataview plugin)

```dataview
TABLE source_file, type FROM #community/Lepton_Capture_State_Machine
SORT file.name ASC
```

## Connections to other communities
- 8 edges to [[_COMMUNITY_Command Dispatcher]]
- 5 edges to [[_COMMUNITY_Board Clock and Peripherals]]
- 4 edges to [[_COMMUNITY_Dosimeter ADC]]
- 4 edges to [[_COMMUNITY_RS-485 Driver]]
- 3 edges to [[_COMMUNITY_VoSPI and CRC]]

## Top bridge nodes
- [[health_increment()]] - degree 16, connects to 3 communities
- [[lepton_capture_init()]] - degree 7, connects to 2 communities
- [[lepton_capture.c]] - degree 12, connects to 1 community
- [[lepton_capture_task()]] - degree 12, connects to 1 community
- [[board_lepton_cs()]] - degree 7, connects to 1 community