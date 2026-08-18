---
type: decision-log
status: active
last-reviewed: 2026-08-17
---

# Decision log

## Accepted for specification v0.1

### D-001 - Durable project memory

Use this Obsidian-compatible `Knowledge Base/` vault as the human-editable source of
facts and use Graphify output as the generated relationship/index layer.

### D-002 - DOSI naming

Interpret `DOSI` as the dosimeter input on PA4, not a Lepton signal. Rename it
`DOSIMETER_ADC` in firmware and documentation.

### D-003 - One shared command model

USB and the field bus use one internal command dispatcher. Transport adapters must not
implement independent camera-control behavior.

### D-004 - RS-485 terminology and topology

Specify the multi-camera bus as four-wire multidrop RS-485 using the ADM2582E. Retain
"RS-422" only as a user-facing legacy label if required.

### D-005 - Field bus implemented last

Complete USB, camera acquisition, and dosimeter functionality before field-bus firmware.
The wire protocol is specified early to avoid incompatible host/device architectures.

### D-006 - Capture never blocks on transport

The frame acquisition service owns synchronization and may overwrite/drop old unpublished
frames when USB or field-bus consumers are slow.

### D-007 - Independent Lepton master clock

The MAX7375 on PH0 clocks only the STM32. The Lepton has a separate always-on master
clock, so PA8 remains `PWR_DWN_L` and is not used as MCO1.

### D-008 - Data-only USB connector

USB VBUS is not connected. Disable VBUS sensing in firmware and retain PA9 as Lepton
`RESET_L`.

### D-009 - Unused Lepton GPIOs

PA0, PA1, and PA7 are not driven or sampled. Configure them as analog/no-pull so the
Lepton GPIO0/1/2 signals remain electrically floating from the MCU perspective.

### D-010 - Dosimeter nominal conversion

The PA4 voltage is the final amplified output. Use 2.5 mV/rad as the nominal conversion,
while retaining raw ADC and voltage fields and a versioned calibration record.

## Proposed, awaiting review

### P-001 - MCU clock qualification

Use the MAX7375 8 MHz HSE to generate 100 MHz SYSCLK and 48 MHz USB from PLLI2SQ.
Qualify USB enumeration and clock behavior on the target hardware.

### P-002 - USB composite

Use UVC radiometric video plus CDC ACM commands/telemetry for the first implementation.

### P-003 - Raw video mode

Use Lepton Raw14 with TLinear enabled and publish a 16-bit radiometric UVC mode. Add a
secondary display-friendly mode only if host compatibility requires it.

### P-004 - 400 kHz CCI and 12.5 MHz VoSPI

Use conservative, exactly-derived peripheral clocks for bring-up before qualification.

### P-005 - No RTOS decision yet

Select bare-metal event loop versus FreeRTOS only after the USB stack and concurrency
requirements are reviewed.
