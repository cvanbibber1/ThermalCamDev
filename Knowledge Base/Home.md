---
type: project-memory
status: active
project: ThermalCamDev
last-reviewed: 2026-08-17
---

# ThermalCamDev project memory

This vault is the durable source of project facts, decisions, unresolved questions,
bugs, and work sequencing. Update it in the same change as firmware or hardware
decisions, then update the Graphify graph.

## Current outcome

The project will provide:

1. Radiometric 160 x 120 Lepton 3.1R acquisition over VoSPI.
2. USB Full-Speed UVC video plus a command/telemetry interface.
3. Low-rate dosimeter telemetry from `PA4 / ADC1_IN4`.
4. A master-controlled multidrop differential serial interface implemented last.
5. SWD programming and recovery through FT2232H Channel A.

## Canonical notes

- [[Architecture/System Specification v0.1]]
- [[Hardware/Pin Map]]
- [[Interfaces/Lepton Interface]]
- [[Interfaces/USB Interface]]
- [[Interfaces/Dosimeter Telemetry]]
- [[Interfaces/RS-485 Multidrop Protocol]]
- [[Project/Risks and Bugs]]
- [[Project/TODO]]
- [[Project/Implementation Status]]
- [[Project/Decision Log]]
- [[Sources/Primary Sources]]
- [[../FT2232 Information|FT2232 Information]]

## Naming corrections

- `DOSI` means the **dosimeter analog input**, not a Lepton signal. Its canonical
  firmware name is `DOSIMETER_ADC`.
- The ADM2582E can implement RS-422 or RS-485. A bus on which several cameras can
  transmit is electrically a multidriver RS-485 application. The preferred four-wire
  topology retains the familiar full-duplex RS-422 cabling arrangement, but the design
  and protocol are specified as **four-wire multidrop RS-485**.
- The current Lepton datasheet marks GPIO0, GPIO1, and GPIO2 as reserved and says they
  should not be connected.

## Review gate

Do not generate the STM32Cube project or start firmware implementation until the
P0 hardware questions in [[Project/Risks and Bugs]] are resolved.
