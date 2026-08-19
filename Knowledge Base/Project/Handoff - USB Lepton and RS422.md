---
type: handoff
status: active
last-reviewed: 2026-08-18
---

# Handoff: USB Lepton imaging and RS422/UART

## Final objective

Deliver reliable 160 x 120 radiometric images from the FLIR Lepton 3.1R as a
Windows/Linux USB camera, with equivalent camera control and snapshot access over the
ADM2582E RS422/UART link. The field bus must not exceed 921600 baud. RS422 work remains
last, after USB video is proven.

## Current hardware and host state

- Replacement STM32F412CGU6 board is installed. The previously incorrect Lepton VDDC and
  VDDIO rails were corrected by the user.
- The Lepton is seated in its socket.
- USB data is connected. Windows enumerates:
  - `Lepton 3.1R Radiometric Camera` at `VID 1209:PID F412`, interface 0.
  - `USB Serial Device (COM55)` at interface 2.
- The FT2232H parent `VID 0403:PID 6010` is on WinUSB. OpenOCD SWD through Channel A works
  reliably with `debug/cjmcu-ft2232h-swd.cfg`; do not change this driver while continuing
  SWD work.
- OpenOCD detects STM32 ID `0x441`, Cortex-M4, and 1024 KiB flash. The current ELF programs
  and verifies successfully.
- The linked local Rev. 400 PDF is byte-identical to the official copy used during this
  investigation (SHA-256 `74F9D92C84163CEBAA1B89A190AFBDBB5B468CBEE38C38B0A57E2CF98F2C2AD5`).

## Completed and verified

- STM32 boots from the MAX7375 HSE, runs at 100 MHz, and provides a separate 48 MHz USB
  clock. USB VBUS sensing is disabled because the connector is data-only.
- Composite UVC plus CDC enumerates. CDC commands return firmware `0.2.0`, MCU UID, health,
  Lepton state, and live dosimeter telemetry.
- Dosimeter PA4 ADC path is live. The configured final conversion remains 2.5 mV/rad after
  the external 25x gain.
- Lepton CCI now ACKs at address `0x2A`; boot/ready register reads `0x0006`. FFC commands
  complete without a CCI error.
- CCI `DATA_LENGTH` was proven to be bytes, not 16-bit words. GET handling was corrected.
  Verified raw attributes:
  - OEM video format words `0x0007 0x0000`: Raw14.
  - Telemetry words `0x0000 0x0000`: disabled.
  - TLinear enable and resolution each return `0x0001 0x0000`.
  - OEM part-number bytes decode as `500-0758-03` when each returned 16-bit word is decoded
    little-byte-first.
- Native test suite passes 8/8, including regressions for recognizing a VoSPI discard
  marker before packet-20 segment decoding and resetting sequence state on segment ID zero.
- Target build succeeds. Current image uses 35,392 bytes flash and 211,620 bytes RAM
  (80.7%); the ping-pong DMA and rolling scan window should be compacted after restart
  cadence is fixed.
- The Rev. 400 shutdown requirement was incorporated: PWR_DWN_L is held low for 150 ms,
  exceeding the required 100 ms before startup.
- VoSPI resync now holds PB13/SCK physically high while SPI is disabled, restores AF5 and
  enables SPI while CS remains high, then asserts CS and starts clocks.
- SPI RX-only DMA was rejected because RXNE refills after NDTR reaches zero and HAL ends the
  transaction in error. Bounded/circular full-duplex DMA with a constant zero MOSI source
  works and keeps PB15 low on the wire.

## VoSPI resolved 2026-08-18 (full frame rate)

VoSPI now runs at the camera's native rate with no error counters advancing.
A 60-second soak measured 79.47 chunks/s (the theoretical maximum for 19,680
bytes at 12.5 MHz), 8.76 frames/s, 34.99 accepted segments/s, and zero dropped
chunks, CRC errors, or sequence errors. Four defects were found and fixed:

1. **Task-loop check-then-check race (the periodic reacquisitions).** The
   streaming state tested `transfer_ready` and then `spi_running`. A completion
   interrupt landing between the two reads made a healthy chunk look like a dead
   link, forcing a resync. The completion path now publishes `transfer_ready`
   before clearing `spi_running`, and the stall test re-reads `transfer_ready`
   after observing an idle transport. Resyncs went from ~1.4/s to zero.
2. **One-bit VoSPI misalignment.** SPI2 samples the Lepton one bit late, so the
   captured byte stream is a right-shifted copy of the wire packets. This was
   previously described as an "erroneous one-bit normalization"; it is real.
   Dumping the live DMA buffer over SWD showed a perfect 60-packet run with all
   60 CRCs valid only at a one-bit shift. `detect_bit_phase()` now derives the
   shift from the data after every reacquisition instead of assuming it, so the
   code stays correct if the extra clock edge is ever removed in hardware (the
   detected phase then reads zero).
3. **Inter-chunk clock gaps.** Restarting a normal-mode DMA per chunk left the
   SPI clock idle about 23% of the time; the Lepton responded by invalidating
   segments (segment ID 0) roughly 25 times per second. SPI2 RX and TX DMA are
   now circular over both chunk buffers, with the half-transfer and
   transfer-complete interrupts handing over one buffer each. Clocking is
   continuous and there is no per-chunk restart left to fail.
4. **Bit-serial CRC-16 exceeded the per-chunk budget.** Checking every packet
   byte through the bit loop took roughly 3 ms per segment, so the task loop
   could not consume a 12.6 ms chunk in time and overran about 13% of them.
   `crc16_ccitt()` is now nibble-table driven and `vospi_packet_crc()` masks the
   four header bytes once instead of calling per byte. Dropped chunks went to
   zero and the frame rate went from 1.49/s to 8.82/s.

A fifth defect surfaced once frames arrived at full rate: the published frame
was replaced several times during a chunked `frame` transfer, so the host got
`COMMAND_STALE_FRAME`. `lepton_capture_hold_frame()` now pins the published
buffer and generation for the duration of a chunked read while assembly
continues into the working buffer, with a 2-second watchdog release. This also
makes the RS422 snapshot path viable, where a full frame takes about 500 ms.

## USB camera and FFC resolved 2026-08-18

### USB video works end to end

- Y16 UVC frames are delivered and decoded on Windows. A 210-second capture
  measured 2,726 payloads at 12.99 fps carrying 1,827 unique frames at 8.71 fps
  (the camera rate), with zero malformed reads, zero torn frames, zero dropped
  chunks, and zero CRC or sequence errors.
- The installed FFmpeg build lists the pin but cannot decode Y16. DirectShow
  with colour conversion disabled does. `tools/uvc_capture.py` implements this
  and reports rate, uniqueness, tearing, and interval jitter.
- **The published frame is deliberately not pinned for UVC.** Pinning it
  collapsed delivery from 8.8 to 0.11 unique frames per second, because a
  payload occupies 77 of every 78 ms and assembly had almost no unpinned window
  in which to publish. Reading unpinned is safe because both sides walk the
  frame top to bottom and the reader is strictly faster: UVC drains 120 rows in
  about 77 ms while assembly fills them in about 114 ms. Verified over 525
  consecutive frames by checking each 30-row segment band against the previous
  frame; a frame captured mid-swap would show some bands unchanged and others
  changed, and none did. This invariant breaks if a payload ever takes longer
  than one camera frame period, which would need a third assembly buffer.
- The chunked `frame` command still pins, because it spans many round trips.
  Three consecutive CDC snapshots return three distinct generations.

### Flat-field correction is supervised, not configured

- The module already powers up in the wanted policy: auto shutter, 180,000 ms
  period, 1.5 C delta. The Lepton is power-cycled on every MCU reset, so those
  defaults are re-established each boot.
- **Do not write the SYS FFC shutter-mode object.** A 32-byte block write to the
  DATA registers is accepted (STATUS reports success) but only partly lands,
  leaving a mix of written and stale words and silently disabling automatic FFC.
  Proven on 2026-08-18: a clean boot reads a correct object; the same object
  reads back corrupted immediately after the firmware writes it; and a 16-word
  GET of a never-written object (OEM part number) is always correct. An early
  attempt to read-modify-write it also put the capture task into a boot-retry
  loop, because a failed FFC policy was treated as a fatal configuration error.
- The firmware now reads the policy every 30 seconds while streaming and issues
  an explicit RUN FFC only if the shutter mode is not auto or a correction is
  more than 60 seconds overdue. RUN carries no data payload and is reliable.
  Policy failures are recorded in `lepton-status` and `cci_errors` but never
  block imaging.
- Observed working: across a 210-second run the camera ran its own shutter at
  the 180-second mark (elapsed reset from 20.4 s to 51.0 s) with
  `ffc_forced_runs` staying at zero.

## Host application and format choice 2026-08-18

### Windows exposes one payload format, not a choice

Y16 and YUY2 were both advertised as payload formats on the streaming
interface, with a spec-correct descriptor verified byte by byte over SWD
(`bNumFormats` 2, `wTotalLength` 141, both GUIDs, chain parses exactly). The
Windows frame server still exposed only YUY2, and it did so regardless of
which format was index 1. So a device offering both is a YUY2 device as far as
Windows is concerned, and the radiometric counts become unreachable.

`UVC_ADVERTISE_SECOND_FORMAT` therefore defaults to 0 and the shipped build
offers Y16 alone, which restores 8.88 frames/s of radiometric video. Set it to
1 with `UVC_UNCOMPRESSED_GUID` set to YUY2 for a build that targets the stock
Windows Camera app; that build is preserved on `uvc/yuy2-windows-camera` and
delivers 12.87 frames/s of auto-gain white-hot video through the standard
Windows path.

Two descriptor defects were found and fixed while building this, both from
locating descriptors by scanning: the walk kept matching `CS_INTERFACE` subtype
1 past the video block and wrote into a CDC functional descriptor, and the
class-specific VS input header carries one `bmaControls` byte per format so it
has to grow with the second format. Without the second fix the video function
did not enumerate at all.

### Application

`tools/thermalcam_app.py` reads Y16 over the video interface and drives control
over CDC concurrently. `tools/thermal_imaging.py` holds the palettes, contrast
mapping, destriping, and temperature conversion shared with
`tools/render_frame.py`. Verified offscreen across every palette, contrast
mode, and unit, including snapshot (PNG, raw, CSV) and recording (MP4 plus
optional raw Y16), and the control link degrades cleanly when CDC is absent.

Verified on hardware 2026-08-19: live preview at 8.8 distinct frames per
second, automatic hottest and coldest markers, spot readings, and a live
control link reporting the shutter policy and time since the last automatic
correction. The two transports agree on temperature - a CDC frame and a UVC
frame taken seconds apart read 16.33/29.74 C and 16.31/30.03 C, a 0.07 K
difference in scene mean, which is drift between the grabs rather than a
calibration gap.

Both interfaces are single-client: the application holds the serial port while
it runs, so the command-line tool cannot use the same camera at the same time,
and a second video client cannot open the pin. An abandoned client keeps the
video interface locked until its process exits.

Branches parked for the routes not taken: `radiometric/cdc-frames`,
`radiometric/format-switch`, `radiometric/second-pin`, `uvc/yuy2-windows-camera`.

## Previous flashed firmware state

The VoSPI imaging blocker is solved and real frames are available:

- CCI SET scalar word order is corrected to low-word first. Startup reads back Raw14
  (`0x0007 0x0000`), telemetry disabled, TLinear enabled, and GPIO mode 5 (VSYNC).
- SPI2 is Mode 3 at 12.5 MHz. RX and TX DMA now use normal mode, not circular mode.
- Two 120-packet ping-pong DMA buffers keep clocks nearly continuous. A rolling two-chunk
  raw-byte window finds complete packet sequences without the previous erroneous one-bit
  normalization. Packet CRC is checked before frame assembly.
- An uninterrupted diagnostic proved one full segment at byte offset 15,744 (96 discard
  packets followed by IDs 0-59); all 60 packet CRCs passed.
- Complete 1-2-3-4 frames are assembled. USB CDC saved generation 7 as a 38,400-byte frame
  at `tmp/real_frame.raw`; its rendered image is `tmp/real_frame.png`.
- The retrieved frame contains 19,200/19,200 nonzero pixels, 536 unique values, and spans
  29,126-30,384 centikelvin (291.26-303.84 K), with coherent visible scene structure.
- Windows enumerates the UVC pin as Y16 (`0x20363159`), 160x120 at 9 fps. The installed
  FFmpeg DirectShow backend lists it correctly but cannot decode Y16, so a Y16-capable
  viewer/capture test remains.
- The last complete frame is preserved across VoSPI reacquisition, allowing CDC/UVC access
  even while the transport re-primes.

## Important known defects and cautions

1. ~~**P0 restart cadence.**~~ Resolved; see the VoSPI section above. One
   `vospi_link_stalls` event was still observed in a 60-second soak (0.02/s,
   recovered automatically). Worth watching in a longer soak but no longer a
   blocker.
2. **RAM is 77.0%**, down from 80.7% after the rolling scan window was replaced
   with a one-segment tail carry. Further reduction requires shrinking the
   19,680-byte chunk, which would double the interrupt rate; not worth doing
   while the current buffers meet timing.
3. **UVC payload not yet captured by a host application.** Enumeration is correct and the
   shared published frame is real, but this FFmpeg build reports Y16 DirectShow compression
   `0x20363159` as unsupported. Test with a Y16-aware application or a small Media Foundation
   capture utility.
4. **Do not connect multiple RS422 transmitters yet.** ADM2582E DE is still externally
   pulled high. Remove that pull-up and prove reset-default DE low first.

## Recommended next approach

### 1. Run a long VoSPI soak and chase the extra clock edge

- Soak for hours and confirm `vospi_link_stalls` stays negligible.
- Find the source of the one-bit sampling offset (most likely a glitch edge on
  PB13 when the pin switches from the GPIO clock hold to AF5, or the SPI enable
  ordering in `LEPTON_STATE_RESYNC`). Removing it is optional now that the phase
  is detected, but it would eliminate a whole class of alignment risk.

### 2. Remaining USB work

- Windows Y16 delivery is proven. Still to do: Linux V4L2, presentation
  timestamps, and reconnect/bus-reset behaviour.
- The UVC payload header never sets the end-of-frame bit; hosts rely on the FID
  toggle alone. Windows accepts this, but set EOF before testing Linux.
- Consider an optional display-friendly 8-bit mode only after Linux is proven.
- A host that stops mid-payload leaves the interface unusable to the next
  DirectShow client until the stream is released; confirm the firmware handles
  an abrupt host disappearance cleanly.

### 3. Finish RS422/UART last

- Freeze the ADM2582E A/B/Y/Z, RE, DE, isolation-ground, termination, and bias schematic.
- Remove the DE pull-up and verify PB7 keeps the transmitter disabled through reset/boot.
- Change the provisional 1,000,000-baud setting to a maximum of 921600 baud and qualify
  lower fallback rates.
- Retain PA2 USART2_TX, PA3 USART2_RX, and PB7 DE. PA13/PA14 should remain SWD: they are not
  a practical USART2 route on this design, repurposing them removes recovery/debug access,
  and software UART at 921600 baud is not a robust field-bus implementation.
- Exercise discovery, address assignment, control, FFC, telemetry, and chunked snapshots on
  one node before multi-node contention/noise/cable tests.
- Continuous full-rate 160 x 120 x 16-bit video is not feasible at 921600 baud. Specify
  snapshot/chunk transfer, reduced frame rate, and/or compression for RS422 while USB keeps
  full camera behavior.

## Key files

- `src/lepton/lepton_capture.c`: verified ping-pong normal-DMA, rolling alignment, CRC, and
  frame assembly path; restart cadence still needs cleanup.
- `src/lepton/lepton_cci.c`: corrected GET byte lengths and low-word-first SET attributes.
- `src/board/board.c`: clock tree, SPI/DMA, GPIO safe states, SCK hold/restore functions.
- `src/usb/*`: UVC plus CDC implementation.
- `src/drivers/rs485.c`: provisional ADM2582E transport.
- `tools/thermalcam_cli.py`: COM55 diagnostics and eventual frame retrieval.
- `Lepton Engineering Datasheet Rev 400 (500-0659-00-09).pdf`: authoritative Lepton
  electrical, power, CCI, and VoSPI reference.

## Reproduction commands

```powershell
& "C:\Users\cvanbibber\.platformio\penv\Scripts\platformio.exe" run -e target
& "C:\Users\cvanbibber\.platformio\penv\Scripts\platformio.exe" test -e native
& "C:\Python313\python.exe" tools\thermalcam_cli.py --port COM55 info
& "C:\Python313\python.exe" tools\thermalcam_cli.py --port COM55 health
& "C:\Python313\python.exe" tools\thermalcam_cli.py --port COM55 lepton-status
& "C:\Python313\python.exe" tools\thermalcam_cli.py --port COM55 stream-status
```

OpenOCD program/verify remains:

```powershell
& "C:\Users\cvanbibber\AppData\Local\Microsoft\WinGet\Packages\xpack-dev-tools.openocd-xpack_Microsoft.Winget.Source_8wekyb3d8bbwe\xpack-openocd-0.12.0-7\bin\openocd.exe" `
  -s "C:\Users\cvanbibber\AppData\Local\Microsoft\WinGet\Packages\xpack-dev-tools.openocd-xpack_Microsoft.Winget.Source_8wekyb3d8bbwe\xpack-openocd-0.12.0-7\openocd\scripts" `
  -f debug/cjmcu-ft2232h-swd.cfg -f target/stm32f4x.cfg `
  -c "program .pio/build/target/firmware.elf verify reset exit"
```
