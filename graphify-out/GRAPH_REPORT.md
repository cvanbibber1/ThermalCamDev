# Graph Report - ThermalCamDev  (2026-08-18)

## Corpus Check
- 59 files · ~48,493 words
- Verdict: corpus is large enough that graph structure adds value.

## Summary
- 548 nodes · 814 edges · 66 communities (62 shown, 4 thin omitted)
- Extraction: 85% EXTRACTED · 15% INFERRED · 0% AMBIGUOUS · INFERRED: 124 edges (avg confidence: 0.81)
- Token cost: 0 input · 0 output

## Graph Freshness
- Built from commit: `e40a659f`
- Run `git rev-parse HEAD` and compare to check if the graph is stale.
- Run `graphify update .` after code changes (no API cost).

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
- [[_COMMUNITY_Community 35|Community 35]]
- [[_COMMUNITY_Community 36|Community 36]]
- [[_COMMUNITY_Community 37|Community 37]]
- [[_COMMUNITY_Community 38|Community 38]]
- [[_COMMUNITY_Community 39|Community 39]]
- [[_COMMUNITY_Community 40|Community 40]]
- [[_COMMUNITY_Community 41|Community 41]]
- [[_COMMUNITY_Community 42|Community 42]]
- [[_COMMUNITY_Community 43|Community 43]]
- [[_COMMUNITY_Community 44|Community 44]]
- [[_COMMUNITY_Community 45|Community 45]]
- [[_COMMUNITY_Community 46|Community 46]]
- [[_COMMUNITY_Community 47|Community 47]]
- [[_COMMUNITY_Community 48|Community 48]]
- [[_COMMUNITY_Community 49|Community 49]]
- [[_COMMUNITY_Community 50|Community 50]]
- [[_COMMUNITY_Community 51|Community 51]]
- [[_COMMUNITY_Community 52|Community 52]]
- [[_COMMUNITY_Community 53|Community 53]]
- [[_COMMUNITY_Community 54|Community 54]]
- [[_COMMUNITY_Community 55|Community 55]]
- [[_COMMUNITY_Community 56|Community 56]]
- [[_COMMUNITY_Community 57|Community 57]]
- [[_COMMUNITY_Community 58|Community 58]]
- [[_COMMUNITY_Community 59|Community 59]]
- [[_COMMUNITY_Community 60|Community 60]]
- [[_COMMUNITY_Community 61|Community 61]]
- [[_COMMUNITY_Community 65|Community 65]]

## God Nodes (most connected - your core abstractions)
1. `command_dispatch()` - 20 edges
2. `health_increment()` - 17 edges
3. `wire_message_t` - 16 edges
4. `set_result()` - 16 edges
5. `Four-wire multidrop RS-485 protocol` - 16 edges
6. `USBD_HandleTypeDef` - 14 edges
7. `lepton_capture_task()` - 13 edges
8. `main()` - 13 edges
9. `PCD_HandleTypeDef` - 13 edges
10. `ThermalCamDev Project Memory` - 13 edges

## Surprising Connections (you probably didn't know these)
- `test_wire_stream_delimiter()` --calls--> `wire_stream_init()`  [INFERRED]
  test/test_core/test_main.c → src/protocol/wire_protocol.c
- `MAX7375 MCU and USB Clock Qualification` --semantically_similar_to--> `Release blockers`  [INFERRED] [semantically similar]
  Knowledge Base/Project/Risks and Bugs.md → Knowledge Base/Project/Implementation Status.md
- `UVC Y16 Host Compatibility Risk` --semantically_similar_to--> `Release blockers`  [INFERRED] [semantically similar]
  Knowledge Base/Project/Risks and Bugs.md → Knowledge Base/Project/Implementation Status.md
- `Host control` --conceptually_related_to--> `pySerial 3.5`  [INFERRED]
  README.md → requirements-dev.txt
- `Generated Graphify Knowledge Graph` --conceptually_related_to--> `ThermalCamDev Project Memory`  [EXTRACTED]
  README.md → Knowledge Base/Home.md

## Import Cycles
- None detected.

## Hyperedges (group relationships)
- **Shared Command Architecture Across USB and Field Bus** — architecture_system_specification_v0_1_shared_command_model, interfaces_usb_interface_cdc_command_channel, interfaces_rs_485_multidrop_protocol_operation_set, project_decision_log_one_shared_command_model [EXTRACTED 1.00]
- **Radiometric Frame Acquisition and Publication Pipeline** — architecture_system_specification_v0_1_frame_broker, interfaces_usb_interface_uvc_y16_stream, interfaces_rs_485_multidrop_protocol_chunked_frame_transfer, interfaces_dosimeter_telemetry_frame_metadata_sample [INFERRED 0.95]
- **Hardware Validation and Release Gate** — knowledge_base_home_review_gate, project_risks_and_bugs_adm2582e_topology_unknown, project_risks_and_bugs_clock_qualification, project_implementation_status_release_blockers, project_todo_hardware_validation [INFERRED 0.95]

## Communities (66 total, 4 thin omitted)

### Community 0 - "System Architecture and Specs"
Cohesion: 0.23
Nodes (10): Single-Ownership Runtime Services, Static Memory and Explicit Buffer Ownership, Thermal Camera System Specification v0.2, ThermalCamDev Project Memory, Decision log, D-001 Durable Project Memory, Automated Firmware Verification, Firmware Implementation and Test Status (+2 more)

### Community 1 - "Board Clock and Peripherals"
Cohesion: 0.06
Nodes (30): adc_timer_init(), board_clock_valid(), board_fatal(), board_init(), board_lepton_spi_clock_enable(), board_reset_cause(), dma_init(), gpio_init() (+22 more)

### Community 2 - "Lepton Capture State Machine"
Cohesion: 0.12
Nodes (33): board_lepton_cs(), board_lepton_power(), board_lepton_reset(), board_lepton_spi_clock_hold(), lepton_capture_status_t, enter_state(), HAL_SPI_ErrorCallback(), HAL_SPI_TxRxCpltCallback() (+25 more)

### Community 3 - "RS-485 Driver"
Cohesion: 0.11
Nodes (17): 1. Sync Bytes, 2. Coarse Time, 3. Fine Time, 4. Packet Type, 5. Target ID, 6. Command Payload, 7. Spare, 8. CRC (+9 more)

### Community 4 - "USB Device Low-Level"
Cohesion: 0.14
Nodes (30): PCD_HandleTypeDef, HAL_PCD_ConnectCallback(), HAL_PCD_DataInStageCallback(), HAL_PCD_DataOutStageCallback(), HAL_PCD_DisconnectCallback(), HAL_PCD_ISOINIncompleteCallback(), HAL_PCD_ISOOUTIncompleteCallback(), HAL_PCD_MspDeInit() (+22 more)

### Community 5 - "Hardware Pin and Bus Design"
Cohesion: 0.06
Nodes (41): Accepted MCU Clock Architecture, USART2 and PB7 Field-Bus Pin Assignment, FT2232H Channel A SWD Mapping, Lepton VDDIO Electrical Domain, STM32F412CGU6 Pin Map, Hardware-Safe Reset States, 1. Electrical topology, 2. UART and DE timing (+33 more)

### Community 6 - "Command Dispatcher"
Cohesion: 0.22
Nodes (29): board_unique_id_word(), lepton_capture_latest_frame(), append_u32(), assign_bus_address(), begin_response(), cci_get(), cci_run(), cci_set() (+21 more)

### Community 7 - "Dosimeter ADC"
Cohesion: 0.12
Nodes (19): board_rs485_de(), begin_transmit(), HAL_UART_ErrorCallback(), HAL_UART_TxCpltCallback(), handle_message(), rs485_get_status(), rs485_init(), rs485_set_address() (+11 more)

### Community 8 - "VoSPI and CRC"
Cohesion: 0.19
Nodes (14): packet_is_discard(), vospi_latest_frame(), vospi_packet_crc(), vospi_parse_segment(), vospi_parser_init(), crc16_ccitt(), vospi_result_t, make_segment() (+6 more)

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
Cohesion: 0.33
Nodes (6): CCI contract, Electrical contract, Frame representation, Lepton 3.1R interface, Lepton Electrical Contract, VoSPI contract

### Community 14 - "Camera Bring-Up and Health"
Cohesion: 0.67
Nodes (3): Lepton Camera Bring-up Sequence, Fault Observability and Recovery Counters, Phase 3 Lepton CCI and VoSPI

### Community 35 - "Community 35"
Cohesion: 0.18
Nodes (11): Accepted for specification v0.1, D-001 - Durable project memory, D-002 - DOSI naming, D-003 - One shared command model, D-004 - RS-485 terminology and topology, D-005 - Field bus implemented last, D-006 - Capture never blocks on transport, D-007 - Independent Lepton master clock (+3 more)

### Community 36 - "Community 36"
Cohesion: 0.15
Nodes (12): 1. Stabilize VoSPI restart cadence, 2. Prove USB camera delivery, 3. Finish RS422/UART last, Completed and verified, Current flashed firmware state, Current hardware and host state, Final objective, Handoff: USB Lepton imaging and RS422/UART (+4 more)

### Community 37 - "Community 37"
Cohesion: 0.15
Nodes (12): 1. Purpose and status, 2. Functional requirements, 3. Non-functional requirements, 4.1 Clock tree, 4.2 Memory budget, 4. Hardware architecture, 5. Boot and camera bring-up, 6. Runtime architecture (+4 more)

### Community 38 - "Community 38"
Cohesion: 0.17
Nodes (12): Acquisition, Timer-Triggered ADC DMA Acquisition, Calibration model, Dosimeter telemetry, Electrical input, Nearest Dosimeter Sample in Frame Metadata, Nominal 2.5 mV per rad Conversion, Telemetry fields (+4 more)

### Community 39 - "Community 39"
Cohesion: 0.17
Nodes (11): Development Requirements, Final Intended Architecture, FT2232H SWD Debug / Programming Interface Changes, OpenOCD Configuration, Optional UART Debug Interface, Programming Workflow, Required FT2232H-to-STM32F4 Connections, Reset / Boot Configuration (+3 more)

### Community 40 - "Community 40"
Cohesion: 0.29
Nodes (6): Clarifications accepted 2026-08-17, Known temporary hardware behavior, P0 - blocks connection of a multidrop bus, P1 - resolve before peripheral bring-up, P2 - implementation risks, Risks, bugs, and unresolved hardware issues

### Community 41 - "Community 41"
Cohesion: 0.40
Nodes (4): Accepted clock architecture, Debug mapping, Electrical notes, STM32F412CGU6 pin map

### Community 42 - "Community 42"
Cohesion: 0.17
Nodes (11): 10. Recommended Experiment-Side Receive Dispatcher, 12. Suggested State Model, 14. Packet Type Constants, 15. Traffic This Implementation Can Ignore, 17. AI / Code-Generation Constraints, 18. Implementation-Oriented Summary, 1. High-Level Behavior, 2. Relevant Packet Types (+3 more)

### Community 43 - "Community 43"
Cohesion: 0.25
Nodes (7): Command and telemetry channel, Data-Only Self-Powered USB, Device composition, USB hardware constraints, USB interface, UVC video mode, D-008 Data-Only USB Connector

### Community 44 - "Community 44"
Cohesion: 0.18
Nodes (10): Documentation maintenance, Hardware validation, Phase 1 - MCU and debug, Phase 2 - USB/control, Phase 3 - Lepton CCI and VoSPI, Phase 4 - UVC and dosimeter, Phase 5 - multidrop field bus, last, Project TODO (+2 more)

### Community 45 - "Community 45"
Cohesion: 0.25
Nodes (8): Canonical notes, Current outcome, Naming corrections, Review gate, ThermalCamDev project memory, Release blockers, P0 ADM2582E Topology Unknown, Risks, Bugs, and Unresolved Hardware Issues

### Community 46 - "Community 46"
Cohesion: 0.20
Nodes (11): Immutable Frame Broker, Transport-Independent Nonblocking Capture, Multidrop Video Capacity Budget, Bounded Chunked Frame Transfer, UVC and CDC ACM Composite Device, USB Endpoint and FIFO Layout, Drop-Oldest UVC Backpressure Policy, Radiometric UVC Y16 Stream (+3 more)

### Community 47 - "Community 47"
Cohesion: 0.48
Nodes (7): Shared Command Model, COBS and CRC-32C Wire Framing, Integrity Without Cryptographic Authentication, Version 1 Command Operation Set, CDC Shared Command and Telemetry Channel, D-003 One Shared Command Model, Host control

### Community 48 - "Community 48"
Cohesion: 0.29
Nodes (7): 13. Fixed Sizes and Offsets, Command ACK, Command Packet, HRT Data, HRT Flow Control, LRT Data, LRT Request

### Community 49 - "Community 49"
Cohesion: 0.33
Nodes (6): P-001 - MCU clock qualification, P-002 - USB composite, P-003 - Raw video mode, P-004 - 400 kHz CCI and 12.5 MHz VoSPI, P-005 - No RTOS decision yet, Proposed, awaiting review

### Community 50 - "Community 50"
Cohesion: 0.33
Nodes (5): Automated verification completed, Firmware implementation and test status, Hardware validation completed 2026-08-17, Replacement-board update 2026-08-18, Required hardware-in-loop sequence

### Community 51 - "Community 51"
Cohesion: 0.40
Nodes (5): 3.1 Sync Bytes, 3.2 Target ID, 3.3 Coarse and Fine Time, 3.4 CRC, 3. Common Protocol Concepts

### Community 52 - "Community 52"
Cohesion: 0.40
Nodes (5): 4. Command Packet — DICE → Experiment, Conceptual Representation, Layout, Purpose, Receive Behavior

### Community 53 - "Community 53"
Cohesion: 0.40
Nodes (5): 6. LRT Request — DICE → Experiment, Conceptual Representation, Layout, Purpose, Receive Behavior

### Community 54 - "Community 54"
Cohesion: 0.40
Nodes (5): 7. LRT Data Packet — Experiment → DICE, Known Conceptual Representation, Likely CRC, but not proven by the supplied image, Purpose, Visible Source Layout

### Community 55 - "Community 55"
Cohesion: 0.40
Nodes (5): 8. HRT Data Packet — Experiment → DICE, Conceptual Representation, CRC Coverage, Layout, Purpose

### Community 56 - "Community 56"
Cohesion: 0.40
Nodes (5): 9. HRT Flow Control Requests — DICE → Experiment, CRC Coverage, Flow-Control Meanings, Layout, Purpose

### Community 57 - "Community 57"
Cohesion: 0.50
Nodes (4): 11. Recommended Experiment-Side Transmit Behavior, Command Acknowledge, HRT Data, LRT Data

### Community 58 - "Community 58"
Cohesion: 0.50
Nodes (4): 16. Minimum Functional Implementation, Filtering/Validation, Receive, Transmit

### Community 59 - "Community 59"
Cohesion: 0.50
Nodes (4): 5. Command Acknowledge Packet — Experiment → DICE, Conceptual Representation, Layout, Purpose

### Community 60 - "Community 60"
Cohesion: 0.67
Nodes (4): Build and test, Development Python Dependencies, PlatformIO 6.1.19, pySerial 3.5

### Community 65 - "Community 65"
Cohesion: 0.27
Nodes (9): dosimeter_snapshot_t, dosimeter_get_snapshot(), dosimeter_init(), dosimeter_task(), HAL_ADC_ConvCpltCallback(), HAL_ADC_ConvHalfCpltCallback(), integer_sqrt(), process_half() (+1 more)

## Knowledge Gaps
- **184 isolated node(s):** `recommendations`, `unwantedRecommendations`, `I2C_HandleTypeDef`, `SPI_HandleTypeDef`, `ADC_HandleTypeDef` (+179 more)
  These have ≤1 connection - possible missing edges or undocumented components.
- **4 thin communities (<3 nodes) omitted from report** — run `graphify query` to explore isolated nodes.

## Suggested Questions
_Questions this graph is uniquely positioned to answer:_

- **Why does `ThermalCamDev Project Memory` connect `System Architecture and Specs` to `Hardware Pin and Bus Design`, `Community 38`, `Community 43`, `Community 44`, `Community 45`?**
  _High betweenness centrality (0.041) - this node is a cross-community bridge._
- **Why does `board_fatal()` connect `Board Clock and Peripherals` to `Lepton Capture State Machine`, `Dosimeter ADC`?**
  _High betweenness centrality (0.023) - this node is a cross-community bridge._
- **Why does `health_increment()` connect `Lepton Capture State Machine` to `Community 65`, `Board Clock and Peripherals`, `Dosimeter ADC`?**
  _High betweenness centrality (0.022) - this node is a cross-community bridge._
- **Are the 3 inferred relationships involving `command_dispatch()` (e.g. with `handle_message()` and `lepton_capture_run_ffc()`) actually correct?**
  _`command_dispatch()` has 3 INFERRED edges - model-reasoned connections that need verification._
- **Are the 16 inferred relationships involving `health_increment()` (e.g. with `board_init()` and `HAL_ADC_ConvCpltCallback()`) actually correct?**
  _`health_increment()` has 16 INFERRED edges - model-reasoned connections that need verification._
- **What connects `recommendations`, `unwantedRecommendations`, `I2C_HandleTypeDef` to the rest of the system?**
  _191 weakly-connected nodes found - possible documentation gaps or missing edges._
- **Should `Board Clock and Peripherals` be split into smaller, more focused modules?**
  _Cohesion score 0.05673758865248227 - nodes in this community are weakly interconnected._