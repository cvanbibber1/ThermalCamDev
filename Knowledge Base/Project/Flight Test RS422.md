---
type: handoff
status: active
last-reviewed: 2026-08-19
---

# Flight test branch: RS-422 STP link

`flight-test/rs422` carries the STP/DICE packet protocol on USART2 while keeping
USB video, USB CDC and the desktop application, so the same data can be watched
on both transports during bring-up.

## Confirmed link settings

Packet sizes, type codes, field offsets and CRC coverage come from
`dice_experiment_rs422_protocol.md` and `rs422_command_packet_format.md`.

The project confirmed the following on 2026-08-19. They were previously
assumptions and are now settled, held in `include/protocol/stp_protocol.h`:

| Setting | Value | Where |
|---|---|---|
| Wire byte order | Big endian, sync goes out `1A CF FC 1D` | `STP_BIG_ENDIAN` |
| CRC-16 | CCITT-FALSE, poly 0x1021, seed 0xFFFF | `STP_CRC16_SEED` |
| CRC position | Last two bytes of every packet, both directions | - |
| Target ID | `0xC7`, also stored in flash settings | `STP_DEFAULT_TARGET_ID` |

The CRC confirmation also resolves the two bytes the LRT table did not account
for: 1256 total is 6 header, 1248 payload and 2 CRC.

### Still open

- **Coarse time epoch.** Received timestamps are echoed back in vitals but not
  interpreted, so telemetry cannot yet be placed on DICE's clock.
- **Command payload structure.** The 105 bytes are undefined, so commands are
  acknowledged but nothing is decoded from them.
- **LRT and HRT payload layouts.** Ours, documented in `include/stp_link.h` and
  decoded by `tools/stp_monitor.py`. They must be agreed with DICE.

`tools/stp_monitor.py` still exposes byte order and CRC seed as options so a
mismatch can be proved from the ground without rebuilding.

## Layout of our payloads

LRT (1248 bytes) carries housekeeping: uptime, echoed DICE time, camera state
and frame generation, the dosimeter block including dose in microrad and its
stored zero, a scene temperature summary, and the whole health counter block.

HRT (1280 bytes) carries one slice of the published thermal frame behind a
16-byte reassembly header of generation, chunk index, chunk count, byte offset
and length. That is 1264 image bytes per packet and 31 packets per frame.

## Measured

Bench measurement 2026-08-19 with the free-run bench mode enabled, after the
image stream was allowed to run back to back:

- LRT 1 packet/s carrying vitals only, HRT 61 packets/s carrying the image, so
  1.98 frames/s over RS-422 at 87% of the 921600 baud link. The ceiling is
  2.3 frames/s: a 1288-byte packet takes 14.0 ms and a frame is 31 packets.
  Halving the pixel depth would roughly double the rate at the cost of the
  radiometry.
- USB video continued at 8.93 unique frames/s at the same time, unaffected.
- The host decoder and the firmware encoder were cross-checked packet by
  packet: sizes, CRCs and every decoded field match.

## Not yet verified

**No RS-422 to USB converter was connected**, so nothing has been confirmed on
the wire. What is proven is that the firmware builds the packets correctly and
transmits them; what is unproven is the electrical link, the byte order, the
CRC parameters, and whether the far end can sync. Run:

```powershell
python .\tools\stp_monitor.py --port COM<converter>
```

If nothing decodes, try `--little-endian` and a different `--crc-seed` before
suspecting the wiring; neither is fixed by the specification.

## Before connecting to a real bus

- `STP_BENCH_FREERUN` in `src/drivers/stp_link.c` defaults to 1, which makes
  this node transmit without being asked. That is fine point to point with a
  converter and **must be 0 for flight**, where transmission is only allowed in
  response to an LRT request or while HRT is enabled.
- The ADM2582E DE line is still externally pulled high. Do not connect this
  node to a bus carrying other transmitters until that pull-up is removed and
  reset-default DE low is confirmed on hardware.

## USB and RS-422 run together

Confirmed 2026-08-19 on the flight build: USB CDC commands, USB CDC frame
transfer, USB video at 8.8 frames/s, and RS-422 transmission all work at the
same time. RS-422 was sending while USB video ran, at 1 LRT/s and 24 HRT/s.

An earlier report that USB had stopped responding was not the firmware. A
previous instance of the application had been left running across a firmware
load; the device re-enumerated underneath it, and the application sat on the
now-dead serial handle, which blocked every other client. The application now
drops and reconnects the control link instead of holding a dead one.

### Known limitation: video after a device reset

The control link recovers from a reset or firmware load by itself. Video does
not always. When the camera re-enumerates, DirectShow can leave the reading
thread blocked inside `read()` on the stale handle; the thread cannot detect
this, and it keeps hold of the device. A watchdog in the window notices that
frames have stopped, starts a fresh reader and reports the stall, but it cannot
free a device held by a blocked driver call, so the application has to be
restarted to get video back after a reflash. This affects development only:
the camera is not reset during normal use.

## Bench results 2026-08-20

### The image path is proven

Captures from the DICE emulator decoded exactly: two vitals packets and 53
image packets, of which one generation was complete. That frame reassembled
into a coherent 160 x 120 image spanning 27.8 to 40.1 C with no zero pixels.
Live over the converter the monitor reassembles about 1.6 to 2.0 frames a
second with no checksum failures.

`tools/thermalcam_app.py --source rs422 --rs422-port COMn` now runs the whole
window from the RS-422 stream, with the dosimeter panel fed from vitals.

### HRT stop was ignored, now fixed

The bench build set `stream` unconditionally, so `hrt_enabled` was updated by
HRT stop and stop-with-loss but never consulted. The stream therefore never
stopped. Free running now only changes the starting state; flow control is
always obeyed. Proven by forcing `hrt_enabled` to zero over SWD, which stopped
the image stream while vitals continued.

A second defect was found alongside it: an acknowledgement or vitals reply was
dropped if the request arrived while an image packet was going out, because the
transmitter cannot be interrupted. Replies are now queued and sent ahead of
image traffic as soon as the transmitter frees.

### The camera cannot receive: hardware

**The computer to camera direction does not work at all.** Read from the
camera's own memory while running, all receive counters are zero, including
the corrupt-packet counters, so not even a malformed byte is arriving.

| Test | Result |
|---|---|
| 14 bytes at 921600, CPU halted | 1 byte reached the UART |
| 50 bytes at 460800 or above | none |
| 50 bytes at 115200 with both ends rebuilt to match | none decoded |
| Same with DE forced inactive via SWD | none |

The transmit direction is faultless at 921600 in the same session, so the cable
and the converter's receiver are fine. Suspect the camera's receive pair:
continuity, polarity, the transceiver's receiver-enable pin, or termination.
Until it is fixed the stop fix cannot be exercised over the wire, though it is
verified by the SWD test above.

## Still to do for flight

- Decode the command payload once DICE defines it; commands are currently
  acknowledged but not acted on.
- Remove USB once the RS-422 path is confirmed, as intended for the flight
  build.
- The reliability, safety and autonomy reviews called for in
  `STP_protocol_req.md`, including any radiation mitigation such as triple
  modular redundancy, have not been started.
