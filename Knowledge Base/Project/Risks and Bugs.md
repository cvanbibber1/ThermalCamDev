---
type: risk-register
status: active
last-reviewed: 2026-08-18
---

# Risks, bugs, and unresolved hardware issues

## P0 - blocks connection of a multidrop bus

- [ ] **The camera receives nothing usable over RS-422.** Transmission is
  faultless: 79,462 bytes/s at 921600 with no checksum failures. Reception
  never produces a packet, and the corrupt-packet counters stay at zero too,
  so the four-byte sync never survives intact.

  Measured 2026-08-20 by reading the camera's registers while it ran. These are
  ruled out:

  | Ruled out | Evidence |
  |---|---|
  | Receiver disabled | `RE` confirmed tied to ground by inspection |
  | Converter driver gated by handshake | RTS and DTR make no difference in any of four combinations |
  | Baud misconfiguration | `BRR` verified correct at both 921600 and 115200 |
  | Firmware receive path | UART is TX/RX, DMA runs, the buffer fills when bytes do arrive |
  | Grounds | connected, confirmed by inspection |

  What is left is the physical receive pair. It behaves as a marginal,
  intermittent, frequency-dependent connection:

  | Host rate | Bytes reaching the camera, of 200 sent |
  |---:|---|
  | 921600 | 0 |
  | 153600 to 250000 | 1 to 4 |
  | 128000 | 113 |
  | 115200 | 223 once, then 0 on six consecutive repeats |

  The idle line reads as `FF` with occasional `FC`, `F8`, `F0`, which are noise
  glitches being taken for start bits rather than data.

  The asymmetry is explained by the 120 ohm terminator fitted across A and B at
  the camera. Series resistance anywhere in that path, a cold joint or a screw
  terminal gripping insulation, forms a divider with that 120 ohm load and
  collapses the signal. The opposite direction drives into the converter's
  high-impedance receiver, where the same poor joint passes signal happily.

  Next steps, cheapest first:

  1. Lift one leg of the 120 ohm resistor and retest. If reception starts, the
     path has series resistance. This is the single most informative test.
  2. With `tools/rs422_diagnose.py --mode pattern` running, measure across A and
     B at the camera. Expect at least 1.5 V peak to peak differential; a few
     hundred millivolts means series resistance.
  3. Measure each leg end to end. Anything above about an ohm is suspect.
     Reflow the joints and check the terminal block grips copper.
  4. `--mode loopback` with the camera disconnected proves whether the
     converter and its wiring are sound, independently of the camera.
  5. Check whether the converter also terminates its own transmit pair; two
     terminators halve the drive.

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
- [ ] **The chunked frame command starves publication.** The snapshot hold blocks
  the parser from publishing, so a client issuing back-to-back `frame` commands
  keeps receiving the same generation; sixteen consecutive reads all returned
  generation 11568. The fix is a third assembly buffer (about 38 KB, affordable
  after halving the VoSPI chunk). Needed before RS422 snapshots.
- [ ] **Windows exposes only one UVC payload format.** Offering Y16 and YUY2
  together yields a YUY2-only device. Ship one format per build; see
  `UVC_ADVERTISE_SECOND_FORMAT`.
- [ ] UVC reads the published frame without pinning it, which is safe only while a payload
  completes within one camera frame period. Revisit if the frame rate, frame size, or USB
  speed changes.
- [ ] SPI2 at 12.5 MHz must sustain all Lepton segments with DMA and bounded parsing.
- [ ] PC13 VSYNC timing/cadence must be measured on the actual 3.1R.
- [ ] The FT2232 Channel B console UART has no assigned pins because USART2 is the field bus.
- [ ] Confirm whether `rad` denotes accumulated dose or a rate quantity in the dosimeter
  source and define integration/reset semantics. The firmware currently reports an
  instantaneous reading derived from the voltage offset, not an integral over time.
- [ ] **The dosimeter input is under-driven.** Against the correct transfer function,
  `DOSI = 0.1575 + 0.0025 * D_rad`, PA4 reads about 4.3 mV where zero dose should be
  157.5 mV, so the nominal calibration reports about -61 rad. That is the expected reading
  for an input that is not yet driven, not a fault in the conversion. Measured 2026-08-19
  the signal sits at 6 to 9 of 4095 ADC counts, where one count is about 730 uV, or 0.29 rad. After the two second
  filter the reading still wanders about 271 uV, roughly 0.11 rad, over ten seconds, and it
  drifts by several hundred microvolts over minutes. Dose is therefore only meaningful to
  about a tenth of a rad, and re-zeroing is needed after the input is properly driven.
- [ ] A settings save stalls the core for the flash sector erase, which is over a second on
  this part. USB is unresponsive for that time; the host has not been observed to drop the
  device, but this has only been exercised a few times.
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
