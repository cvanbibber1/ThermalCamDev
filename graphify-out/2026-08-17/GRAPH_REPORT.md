# Graph Report - .  (2026-08-17)

## Corpus Check
- Corpus is ~16,737 words - fits in a single context window. You may not need a graph.

## Summary
- 345 nodes · 607 edges · 35 communities (32 shown, 3 thin omitted)
- Extraction: 81% EXTRACTED · 19% INFERRED · 0% AMBIGUOUS · INFERRED: 116 edges (avg confidence: 0.82)
- Token cost: 0 input · 0 output

## Community Hubs (Navigation)
- [[_COMMUNITY_System Architecture and Specs|System Architecture and Specs]]
- [[_COMMUNITY_Board Clock and Peripherals|Board Clock and Peripherals]]
- [[_COMMUNITY_Lepton Capture State Machine|Lepton Capture State Machine]]
- [[_COMMUNITY_RS-485 Driver|RS-485 Driver]]
- [[_COMMUNITY_USB Device Low-Level|USB Device Low-Level]]
- [[_COMMUNITY_Hardware Pin and Bus Design|Hardware Pin and Bus Design]]
- [[_COMMUNITY_Command Dispatcher|Command Dispatcher]]
- [[_COMMUNITY_Dosimeter ADC|Dosimeter ADC]]
- [[_COMMUNITY_VoSPI and CRC|VoSPI and CRC]]
- [[_COMMUNITY_Host CLI and Wire Protocol|Host CLI and Wire Protocol]]
- [[_COMMUNITY_USB Descriptors|USB Descriptors]]
- [[_COMMUNITY_FT2232 SWD Debug|FT2232 SWD Debug]]
- [[_COMMUNITY_Lepton Interface Contracts|Lepton Interface Contracts]]
- [[_COMMUNITY_UVC Frame Publisher|UVC Frame Publisher]]
- [[_COMMUNITY_Camera Bring-Up and Health|Camera Bring-Up and Health]]
- [[_COMMUNITY_Project Goal|Project Goal]]
- [[_COMMUNITY_Runtime Model Decision|Runtime Model Decision]]

## God Nodes (most connected - your core abstractions)
1. `command_dispatch()` - 20 edges
2. `health_increment()` - 16 edges
3. `wire_message_t` - 16 edges
4. `set_result()` - 16 edges
5. `USBD_HandleTypeDef` - 14 edges
6. `main()` - 13 edges
7. `PCD_HandleTypeDef` - 13 edges
8. `ThermalCamDev Project Memory` - 13 edges
9. `lepton_capture_task()` - 12 edges
10. `USBD_StatusTypeDef` - 12 edges

## Surprising Connections (you probably didn't know these)
- `assign_bus_address()` --calls--> `rs485_set_address()`  [INFERRED]
  src/protocol/command_dispatch.c → src/drivers/rs485.c
- `cdc_receive()` --calls--> `health_increment()`  [INFERRED]
  src/usb/usb_cdc_if.c → src/health.c
- `video_data()` --calls--> `lepton_capture_latest_frame()`  [INFERRED]
  src/usb/usb_video_if.c → src/lepton/lepton_capture.c
- `test_vospi_assembles_four_segments()` --calls--> `vospi_parser_init()`  [INFERRED]
  test/test_core/test_main.c → src/lepton/vospi_parser.c
- `test_vospi_rejects_bad_crc()` --calls--> `vospi_parser_init()`  [INFERRED]
  test/test_core/test_main.c → src/lepton/vospi_parser.c

## Import Cycles
- None detected.

## Hyperedges (group relationships)
- **Shared Command Architecture Across USB and Field Bus** — architecture_system_specification_v0_1_shared_command_model, interfaces_usb_interface_cdc_command_channel, interfaces_rs_485_multidrop_protocol_operation_set, project_decision_log_one_shared_command_model [EXTRACTED 1.00]
- **Radiometric Frame Acquisition and Publication Pipeline** — architecture_system_specification_v0_1_frame_broker, interfaces_usb_interface_uvc_y16_stream, interfaces_rs_485_multidrop_protocol_chunked_frame_transfer, interfaces_dosimeter_telemetry_frame_metadata_sample [INFERRED 0.95]
- **Hardware Validation and Release Gate** — knowledge_base_home_review_gate, project_risks_and_bugs_adm2582e_topology_unknown, project_risks_and_bugs_clock_qualification, project_implementation_status_release_blockers, project_todo_hardware_validation [INFERRED 0.95]

## Communities (35 total, 3 thin omitted)

### Community 0 - "System Architecture and Specs"
Cohesion: 0.05
Nodes (53): Immutable Frame Broker, Transport-Independent Nonblocking Capture, Single-Ownership Runtime Services, Shared Command Model, Static Memory and Explicit Buffer Ownership, Thermal Camera System Specification v0.2, Timer-Triggered ADC DMA Acquisition, Versioned Two-Point Dosimeter Calibration (+45 more)

### Community 1 - "Board Clock and Peripherals"
Cohesion: 0.07
Nodes (24): adc_timer_init(), board_clock_valid(), board_fatal(), board_init(), dma_init(), gpio_init(), HAL_ADC_MspInit(), HAL_I2C_MspInit() (+16 more)

### Community 2 - "Lepton Capture State Machine"
Cohesion: 0.14
Nodes (29): board_lepton_cs(), board_lepton_power(), board_lepton_reset(), lepton_capture_status_t, enter_state(), HAL_SPI_ErrorCallback(), HAL_SPI_RxCpltCallback(), lepton_capture_get_status() (+21 more)

### Community 3 - "RS-485 Driver"
Cohesion: 0.10
Nodes (27): board_rs485_de(), begin_transmit(), HAL_UART_ErrorCallback(), HAL_UART_TxCpltCallback(), handle_message(), rs485_get_status(), rs485_init(), rs485_set_address() (+19 more)

### Community 4 - "USB Device Low-Level"
Cohesion: 0.14
Nodes (30): PCD_HandleTypeDef, HAL_PCD_ConnectCallback(), HAL_PCD_DataInStageCallback(), HAL_PCD_DataOutStageCallback(), HAL_PCD_DisconnectCallback(), HAL_PCD_ISOINIncompleteCallback(), HAL_PCD_ISOOUTIncompleteCallback(), HAL_PCD_MspDeInit() (+22 more)

### Community 5 - "Hardware Pin and Bus Design"
Cohesion: 0.09
Nodes (30): Accepted MCU Clock Architecture, USART2 and PB7 Field-Bus Pin Assignment, FT2232H Channel A SWD Mapping, Lepton VDDIO Electrical Domain, STM32F412CGU6 Pin Map, Hardware-Safe Reset States, USART2 Driver-Enable Timing, Four-Wire ADM2582E Multidrop Topology (+22 more)

### Community 6 - "Command Dispatcher"
Cohesion: 0.35
Nodes (21): board_unique_id_word(), lepton_capture_latest_frame(), append_u32(), assign_bus_address(), begin_response(), cci_get(), cci_run(), cci_set() (+13 more)

### Community 7 - "Dosimeter ADC"
Cohesion: 0.14
Nodes (14): board_reset_cause(), dosimeter_snapshot_t, dosimeter_get_snapshot(), dosimeter_init(), dosimeter_task(), HAL_ADC_ConvCpltCallback(), HAL_ADC_ConvHalfCpltCallback(), integer_sqrt() (+6 more)

### Community 8 - "VoSPI and CRC"
Cohesion: 0.19
Nodes (12): packet_is_discard(), vospi_latest_frame(), vospi_packet_crc(), vospi_parse_segment(), vospi_parser_init(), crc16_ccitt(), make_segment(), test_crc_known_vectors() (+4 more)

### Community 9 - "Host CLI and Wire Protocol"
Cohesion: 0.25
Nodes (12): Namespace, Path, CameraLink, cobs_decode(), cobs_encode(), crc32c(), decode_message(), encode_request() (+4 more)

### Community 10 - "USB Descriptors"
Cohesion: 0.36
Nodes (9): config_descriptor(), device_descriptor(), interface_descriptor(), lang_descriptor(), manufacturer_descriptor(), product_descriptor(), serial_descriptor(), unicode_hex() (+1 more)

### Community 11 - "FT2232 SWD Debug"
Cohesion: 0.33
Nodes (6): Connect Under Reset, FT2232H Channel A SWD, FT2232H Channel B UART, FT2232H SWD Debug / Programming Interface, FT2232H SWDIO Combining Circuit, OpenOCD FTDI/MPSSE Workflow

### Community 12 - "Lepton Interface Contracts"
Cohesion: 0.40
Nodes (5): Lepton CCI Contract, Thermal Frame Representation, Lepton 3.1R Interface, Lepton Electrical Contract, Lepton VoSPI Contract

### Community 14 - "Camera Bring-Up and Health"
Cohesion: 0.67
Nodes (3): Lepton Camera Bring-up Sequence, Fault Observability and Recovery Counters, Phase 3 Lepton CCI and VoSPI

## Knowledge Gaps
- **38 isolated node(s):** `I2C_HandleTypeDef`, `SPI_HandleTypeDef`, `ADC_HandleTypeDef`, `TIM_HandleTypeDef`, `UART_HandleTypeDef` (+33 more)
  These have ≤1 connection - possible missing edges or undocumented components.
- **3 thin communities (<3 nodes) omitted from report** — run `graphify query` to explore isolated nodes.

## Suggested Questions
_Questions this graph is uniquely positioned to answer:_

- **Why does `board_fatal()` connect `Board Clock and Peripherals` to `Lepton Capture State Machine`, `RS-485 Driver`, `Dosimeter ADC`?**
  _High betweenness centrality (0.052) - this node is a cross-community bridge._
- **Why does `health_increment()` connect `Lepton Capture State Machine` to `Board Clock and Peripherals`, `RS-485 Driver`, `Dosimeter ADC`?**
  _High betweenness centrality (0.050) - this node is a cross-community bridge._
- **Why does `main()` connect `Dosimeter ADC` to `Board Clock and Peripherals`, `Lepton Capture State Machine`, `RS-485 Driver`?**
  _High betweenness centrality (0.047) - this node is a cross-community bridge._
- **Are the 3 inferred relationships involving `command_dispatch()` (e.g. with `handle_message()` and `lepton_capture_run_ffc()`) actually correct?**
  _`command_dispatch()` has 3 INFERRED edges - model-reasoned connections that need verification._
- **Are the 15 inferred relationships involving `health_increment()` (e.g. with `board_init()` and `HAL_ADC_ConvCpltCallback()`) actually correct?**
  _`health_increment()` has 15 INFERRED edges - model-reasoned connections that need verification._
- **What connects `I2C_HandleTypeDef`, `SPI_HandleTypeDef`, `ADC_HandleTypeDef` to the rest of the system?**
  _45 weakly-connected nodes found - possible documentation gaps or missing edges._
- **Should `System Architecture and Specs` be split into smaller, more focused modules?**
  _Cohesion score 0.05297532656023222 - nodes in this community are weakly interconnected._