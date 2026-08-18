---
type: risk-register
status: active
last-reviewed: 2026-08-18
---

# Risks, bugs, and unresolved hardware issues

## P0 - blocks connection of a multidrop bus

- [ ] **ADM2582E topology is unknown.** Confirm whether A/B and Y/Z are routed as four-
  wire or tied for two-wire, how RE is connected, termination/bias, isolation grounds,
  connector pinout, and whether DE pull-up removal is a PCB change.

## P1 - resolve before peripheral bring-up

- [x] **VoSPI produces complete frames at the native rate.** A 60-second soak on
  2026-08-18 measured 8.76 frames/s with zero dropped chunks, CRC errors, or
  sequence errors. Four defects were fixed: a task-loop check-then-check race on
  the transfer flags, an undetected one-bit SPI sampling offset, inter-chunk
  clock gaps (now circular DMA), and a bit-serial CRC that blew the per-chunk
  budget. See [[Handoff - USB Lepton and RS422]].
- [ ] **One-bit VoSPI sampling offset is compensated, not eliminated.** The
  firmware derives the shift from the data after each reacquisition. Locate the
  spurious clock edge (likely the PB13 GPIO-hold to AF5 handover or the SPI
  enable ordering in `LEPTON_STATE_RESYNC`) and remove it at the source.
- [ ] **`vospi_link_stalls` recorded one event in a 60-second soak.** Recovery is
  automatic. Confirm the rate over a multi-hour soak.
- [ ] **CCI SET value order is wrong in the currently unused configuration arrays.** CCI
  lengths are bytes and numeric 32-bit values return low word first. Change `{0, value}` to
  `{value, 0}`, then read back each setting before re-enabling startup configuration.

- [x] **Lepton CCI address NACK resolved.** The replacement board has corrected VDDC/VDDIO,
  the seated module ACKs at `0x2A`, and status reads booted/ready (`0x0006`).
- [ ] Measure the independent always-on Lepton master clock at the module: 24.975 to
  25.025 MHz, 45-55% duty cycle, and present before reset is released.
- [ ] Qualify the MAX7375-derived MCU/USB clock on hardware. The firmware has bounded
  clock-startup failure handling, but USB enumeration is the practical first acceptance test.
- [ ] Confirm Lepton VDDC, VDD, and VDDIO regulators, sequencing, ripple, decoupling, and
  shutter-current capability. VDDIO can demand hundreds of milliamps during FFC.
- [ ] Confirm the 2.2 kOhm SCL/SDA pull-ups terminate at 2.8-3.1 V Lepton VDDIO.
- [ ] Review 3.3 V MCU outputs into the Lepton VDDIO domain and select level translation
  or document the operating-margin argument for every output.
- [ ] Add/verify safe pulls for Lepton RESET_L, PWR_DWN_L, CS_L, and ADM2582E DE.
- [ ] Confirm USB connector protection, 90 Ohm differential routing, D+/D- series resistor
  footprints if required, VDDUSB decoupling, and shield/ESD strategy.
- [ ] Confirm FT2232 ADBUS4 reset drive and target reset network do not create contention.
- [x] Confirm there is no intended Lepton UART. PA2/PA3 are USART2 signals for ADM2582E.

## P2 - implementation risks

- [ ] UVC 16-bit/Y16 support varies by host. Qualify Windows and Linux applications and
  provide a secondary display-friendly format if required.
- [x] STM32Cube composite UVC+CDC enumerates on the STM32F412 hardware under Windows.
- [x] USB endpoint count and Rx/Tx FIFO allocation enumerate successfully.
- [x] Y16 delivery is qualified on Windows through DirectShow with colour conversion
  disabled (`tools/uvc_capture.py`). FFmpeg still cannot decode `0x20363159`; a
  display-friendly mode remains optional.
- [ ] **The UVC payload header never sets the end-of-frame bit.** Windows tolerates this
  because the frame-ID bit toggles, but set EOF before qualifying Linux V4L2.
- [ ] **Never write the SYS FFC shutter-mode object (CID 0x023C).** Block writes to it are
  acknowledged but only partly applied, which silently disables automatic FFC. Read it and
  fall back to RUN FFC instead. See [[Handoff - USB Lepton and RS422]].
- [ ] UVC reads the published frame without pinning it, which is safe only while a payload
  completes within one camera frame period. Revisit if the frame rate, frame size, or USB
  speed changes.
- [ ] SPI2 at 12.5 MHz must sustain all Lepton segments with DMA and bounded parsing.
- [ ] PC13 VSYNC timing/cadence must be measured on the actual 3.1R.
- [ ] The FT2232 Channel B console UART has no assigned pins because USART2 is the field bus.
- [ ] Confirm whether `rad` denotes accumulated dose or a rate quantity in the dosimeter
  source and define integration/reset semantics.
- [ ] Continuous video from more than one camera exceeds the conservative field-bus budget.
- [x] The USART2 field bus is capped at 921600 baud (`APP_RS485_BAUD`). Lower
  fallback rates still need qualification on hardware.

## Known temporary hardware behavior

- ADM2582E DE is currently pulled high to 3.3 V. Do not connect multiple transmitting
  camera nodes to one live return pair until that pull-up is removed and firmware/hardware
  default DE low is verified.

- FT2232H SWD currently requires the parent `VID 0403:PID 6010` device bound to WinUSB on
  this PC. Binding only interfaces 0/1 caused libusb pipe errors. This removes the FTDI VCP
  presentation until the original driver is restored.

## Clarifications accepted 2026-08-17

- MAX7375 is the STM32 HSE source; Lepton has a separate always-on master clock.
- Lepton GPIO0/1/2 are unused floating signals and will remain high impedance in firmware.
- USB VBUS is not connected; firmware disables VBUS sensing.
- PA2/PA3 are confirmed ADM2582E USART2 TX/RX.
- Dosimeter sensitivity is 2.5 mV/rad at PA4 after the 25x analog gain.
