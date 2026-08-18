---
type: todo
status: active
last-reviewed: 2026-08-18
---

# Project TODO

## Review gate

- [x] Approve firmware implementation from [[../Architecture/System Specification v0.1]].
- [x] Confirm independent always-on Lepton clock, floating GPIO0/1/2, data-only USB,
  PA2/PA3 ADM2582 wiring, and final 2.5 mV/rad dosimeter sensitivity.
- [ ] Provide the latest schematic or netlist for pin/electrical validation.
- [x] Use a deterministic bare-metal/HAL event-loop baseline; no RTOS is required.

## Hardware validation

- [x] Prove the MAX7375-derived clock tree boots the MCU and enumerates USB.
- [ ] Measure MAX7375 frequency/tolerance with a scope or counter.
- [ ] Scope the independent always-on 25 MHz Lepton MASTER_CLK.
- [x] Configure unused Lepton GPIO0/1/2 MCU pins as analog/no-pull.
- [x] Retain PWR_DWN_L on PA8; Lepton clock is independent.
- [x] Disable USB VBUS sensing and retain RESET_L on PA9.
- [ ] Document all Lepton rail measurements during boot and shutter FFC.
- [x] Resolve Lepton CCI `0x2A` address NACK on the corrected replacement board.
- [ ] Scope the Lepton connector and resolve discard-only VoSPI; follow
  [[Handoff - USB Lepton and RS422]].
- [ ] Document ADM2582E A/B/Y/Z/RE/DE/termination/bias wiring.
- [ ] Remove DE pull-up and verify hardware default low.

## Phase 1 - MCU and debug

- [x] Create a PlatformIO STM32Cube/HAL project for STM32F412CGU6.
- [x] Configure 100 MHz SYSCLK and independent 48 MHz USB clock.
- [x] Add bounded HSE/PLL startup failure handling and a clock self-test.
- [x] Verify OpenOCD connect, program, verify, reset, halt/resume, and memory access.
- [x] Prove PA13/PA14 remain SWD throughout development.
- [x] Add reset-cause logging, monotonic time, fatal fault capture, and health counters.
- [ ] Add and qualify the independent watchdog after hardware bring-up.

## Phase 2 - USB/control

- [x] Integrate STM32 USB Device composite CDC + UVC and allocate the OTG FS FIFOs.
- [x] Implement CDC descriptors with deterministic serial number from MCU UID.
- [x] Implement COBS + CRC-32C bounded command protocol and host test utility.
- [x] Enumerate composite UVC+CDC on Windows and exercise CDC commands on COM54.
- [ ] Add production VID/PID task and descriptor version policy.

## Phase 3 - Lepton CCI and VoSPI

- [x] Implement camera power/reset state machine and 5 s boot deadline.
- [x] Implement CCI 16-bit big-endian register and command transport.
- [x] Read camera part identity (`500-0758-03`) and current Raw14/telemetry/TLinear values.
- [ ] Read and record camera software revision.
- [x] Implement Raw14/TLinear setup, telemetry disable, VSYNC, and FFC commands.
- [x] Implement SPI2 DMA segment capture, packet CRC, assembly, and resynchronization.
- [ ] Run 24-hour capture soak and retain fault counters.

## Phase 4 - UVC and dosimeter

- [x] Implement UVC 160 x 120 16-bit Y16 mode.
- [ ] Add UVC presentation timestamps after first host enumeration test.
- [ ] Qualify actual UVC frame transfer on Windows and Linux after Lepton CCI/VoSPI works;
  add a display mode if necessary.
- [x] Implement timer-triggered ADC DMA, VREF compensation, filtering, and telemetry.
- [x] Use nominal final sensitivity of 2.5 mV/rad after gain.
- [ ] Obtain dosimeter calibration data and define accumulated-dose versus dose-rate semantics.
- [ ] Associate nearest dosimeter sample with each frame.

## Phase 5 - multidrop field bus, last

- [ ] Freeze [[../Interfaces/RS-485 Multidrop Protocol]] after schematic review.
- [x] Implement USART2 DMA RX/TX, PB7 DE timing, COBS, CRC, addressing, and deadlines.
- [x] Implement collision-spread discovery responses using MCU UID hashing.
- [x] Implement UID-targeted runtime node-address assignment over broadcast.
- [ ] Persist the assigned address in a versioned, CRC-protected flash settings page.
- [x] Implement bounded chunked snapshot transfer over either command transport.
- [ ] Build a multi-node bus test jig and inject contention, truncation, noise, and resets.
- [ ] Qualify supported baud rates, cable lengths, node counts, EMC, and thermal behavior.
- [ ] Change the provisional field-bus maximum from 1 Mbaud to 921600 baud.

## Documentation maintenance

- [x] Update this vault in the same change as every decision or discovered defect.
- [x] Run Graphify update after each material code or documentation change.
