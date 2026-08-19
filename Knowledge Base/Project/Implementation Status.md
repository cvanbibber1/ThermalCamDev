---
type: implementation-status
status: active
last-reviewed: 2026-08-18
---

# Firmware implementation and test status

## Implemented baseline

- STM32F412CGU6 PlatformIO/STM32Cube HAL target at 100 MHz from the MAX7375 HSE.
- PLLI2SQ-derived 48 MHz USB clock with VBUS sensing disabled.
- Safe reset states: Lepton reset and power-down low, chip select high, MOSI low, and
  ADM2582E DE low before peripheral initialization.
- PA0, PA1, and PA7 remain analog/no-pull for the unused Lepton GPIO signals.
- Lepton power/reset/boot state machine with a 950 ms earliest CCI access, 100 kHz CCI
  transport, Raw14, TLinear 0.01 K,
  telemetry disable, GPIO3 VSYNC selection, and software FFC command.
- SPI2 mode-3 DMA capture at 12.5 MHz with two segment buffers, VoSPI CRC validation,
  four-segment frame assembly, resynchronization, and two published frame buffers.
- ADC1 PA4 plus VREFINT sampling at 1 kHz using TIM2 and circular DMA. The default final
  conversion is 2.5 mV/rad with raw, voltage, filtered voltage, and nominal radiation data.
- USB Full-Speed composite device: 160 x 120, 16-bit Y16 UVC plus CDC ACM commands.
- Shared bounded COBS/CRC-32C request protocol for CDC and USART2.
- Provisional four-wire multidrop implementation: one-megabaud USART2 DMA, addressed
  requests, PB7 DE release after UART transmission-complete, broadcast discovery with
  UID-derived response delay, UID-targeted runtime address assignment, and chunked
  full-frame access. Runtime address assignments revert to the default after reset until
  flash settings storage is implemented.
- Python host CLI for device information, health, camera status, raw CCI access, FFC,
  dosimeter telemetry, bus status, and frame retrieval.

## Automated verification completed

- ARM target build succeeds for `genericSTM32F412CG`.
- Current image size: 35,192 bytes flash and 152,468 bytes RAM.
- Eight native tests pass: CRC-32C, Lepton CRC-16, COBS round trip, wire framing and CRC
  rejection, streaming delimiter parsing, four-segment VoSPI assembly, corrupt-packet
  rejection, discard-before-segment-ID recognition, and segment-ID-zero sequence reset.
- Cppcheck reports zero high- or medium-severity defects. Low findings are callback and
  whole-program-analysis style findings.
- Python host utility compiles and its command-line parser runs.

## Hardware validation completed 2026-08-17

- Binding the FT2232H parent `VID 0403:PID 6010` device to WinUSB made Channel A MPSSE
  accessible to OpenOCD. Binding only the child interfaces produced libusb pipe errors.
- OpenOCD identified SWD DPIDR `0x2BA01477`, Cortex-M4 r0p1, STM32 device ID `0x441`,
  and 1024 KiB flash. Program, verify, reset, halt, resume, and memory/register reads work.
- The firmware is programmed and running. Direct RCC inspection confirms the external HSE,
  100 MHz SYSCLK, locked PLLI2S, and selected 48 MHz PLLI2SQ USB source.
- Windows enumerates `1209:F412` automatically after reset as a composite device, a
  `Lepton 3.1R Radiometric Camera`, and `USB Serial Device (COM54)`. A guaranteed 100 ms
  software detach was added because debugger reset did not always create a host-visible
  disconnect interval.
- CDC commands return firmware `0.2.0`, UID, health counters, and live PA4 dosimeter
  telemetry. The sampled idle input during this run was approximately 0.8 mV.
- DirectShow discovers the UVC capture pin and its 160 x 120, 16-bit Y16, 9 fps mode.
  This FFmpeg build reports Y16 as an unsupported/unknown compression type, so actual frame
  transfer remains blocked until the Lepton produces frames and must be tested with a Y16-
  aware application.
- Lepton CCI does not yet respond. PB8/SCL and PB9/SDA idle high, PA8/PWR_DWN_L and
  PA9/RESET_L read high during the boot window, and HAL reports address NACK (`AF`) for
  7-bit address `0x2A` at both 400 kHz and 100 kHz. This now points to camera rails, master
  clock, connector/pin continuity, or module state rather than MCU I2C timing.

## Replacement-board update 2026-08-18

- The Lepton is seated and the corrected VDDC/VDDIO replacement board now ACKs CCI at
  `0x2A`; boot/ready status is `0x0006`.
- USB enumerates on the replacement board as UVC plus CDC `COM55`. CDC info, health, CCI,
  FFC, and dosimeter commands work.
- CCI GET byte-length handling was corrected and the module identifies as `500-0758-03`.
- VoSPI is no longer a blocker. SPI2 uses circular RX/TX DMA over two chunk buffers with
  automatic bit-phase detection, table-driven packet CRC, and a snapshot hold for chunked
  frame reads. A 60-second soak sustained 8.76 frames/s with zero dropped chunks, CRC
  errors, or sequence errors, and the transport clocks at 100% duty cycle. The image is
  35,988 bytes flash and 77.0% RAM. See [[Handoff - USB Lepton and RS422]].
- USB UVC Y16 video is proven on Windows: 210 seconds of capture delivered 1,827 unique
  frames at 8.71 fps with zero malformed, torn, or dropped frames. `tools/uvc_capture.py`
  captures and validates the stream over DirectShow with colour conversion disabled.
- Flat-field correction runs automatically from the camera's own auto-shutter policy
  (180 s / 1.5 C). The firmware verifies the policy every 30 s and forces a RUN FFC only if
  the camera is in manual mode or a correction is overdue. The shutter-mode object is never
  written; block writes to it corrupt it on this module.
- Flash-backed settings in the last sector (`src/settings.c`) hold the dosimeter zero and
  survive reset and reflashing. Saves are deferred to the end of the task loop because the
  sector erase stalls the core for over a second; VoSPI reacquires by itself afterwards.
- The dosimeter reports dose relative to a stored zero at 2.5 mV per rad, signed so an
  under-driven detector reads negative instead of wrapping. `dosimeter-zero` averages 32 ADC
  blocks, about two seconds, and writes the result to flash.
- `tools/render_frame.py` renders a raw Y16 frame with plateau/linear/equalize AGC,
  ironbow/rainbow/gray colormaps, column-stripe removal, and optional flat-field division.

## Required hardware-in-loop sequence

1. Keep the FT2232H parent `VID 0403`, `PID 6010` device on WinUSB while SWD debugging.
2. Measure Lepton VDDC, VDD, VDDIO, 25 MHz master clock, PWR_DWN_L, and RESET_L at the
   module connector. Resolve the `0x2A` address NACK before VoSPI testing.
3. Confirm `RS485_DE` becomes low immediately after reset.
4. Run sustained CDC and UVC transfers with a Y16-aware host application.
5. Scope VSYNC, SPI clock, and chip select after CCI boot succeeds, then
   chip select. Verify camera identity and radiometric frames for at least 24 hours.
6. Compare PA4 ADC voltage against a calibrated meter and known radiation input.
7. Remove the ADM2582E DE pull-up and document A/B/Y/Z, RE, termination, and bias before
   connecting more than one camera transmitter. Then run contention, truncation, noise,
   reset, cable-length, and multi-node tests.

## Release blockers

- Lepton CCI is blocked by a repeatable `0x2A` address NACK; camera power, clock, and
  connector-level signals require measurement.
- The MAX7375-derived clock runs the MCU and USB successfully, but frequency/error tolerance
  still needs scope or counter qualification.
- UVC Y16 enumerates on Windows but actual frames and Linux interoperability remain untested.
- The provisional multidrop electrical topology and termination remain TBD.
- Development VID/PID `1209:F412` must be replaced with an allocated production identity.
