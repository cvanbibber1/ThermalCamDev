---
type: handoff
status: active
last-reviewed: 2026-08-19
---

# Flight test branch: RS-422 STP link

`flight-test/rs422` carries the STP/DICE packet protocol on USART2 while keeping
USB video, USB CDC and the desktop application, so the same data can be watched
on both transports during bring-up.

## What is authoritative and what is not

Packet sizes, type codes, field offsets and CRC coverage come from
`dice_experiment_rs422_protocol.md` and `rs422_command_packet_format.md`.

Four things are **not** defined by those documents and are assumptions gathered
at the top of `include/protocol/stp_protocol.h` so they can be corrected in one
place:

| Assumption | Current value | Where |
|---|---|---|
| Wire byte order | most significant byte first | `STP_BIG_ENDIAN` |
| CRC-16 parameters | CCITT poly 0x1021, seed 0xFFFF | `STP_CRC16_SEED` |
| Target ID | 1, stored in flash settings | `STP_DEFAULT_TARGET_ID` |
| Coarse time epoch | not interpreted, echoed back only | - |

The LRT and HRT payload contents are also undefined by the specification. The
layouts used here are this experiment's own and **must be agreed with DICE**;
they are documented in `include/stp_link.h` and decoded by
`tools/stp_monitor.py`.

`tools/stp_monitor.py` exposes byte order, CRC seed and Target ID as options,
so a mismatch can be identified from the ground side without rebuilding.

## Layout of our payloads

LRT (1248 bytes) carries housekeeping: uptime, echoed DICE time, camera state
and frame generation, the dosimeter block including dose in microrad and its
stored zero, a scene temperature summary, and the whole health counter block.

HRT (1280 bytes) carries one slice of the published thermal frame behind a
16-byte reassembly header of generation, chunk index, chunk count, byte offset
and length. That is 1264 image bytes per packet and 31 packets per frame.

## Measured

Bench measurement 2026-08-19 with the free-run bench mode enabled:

- LRT 1 packet/s, HRT 24 packets/s, so about 0.78 frames/s over RS-422 and
  roughly a third of the 921600 baud link.
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

## Still to do for flight

- Decode the command payload once DICE defines it; commands are currently
  acknowledged but not acted on.
- Remove USB once the RS-422 path is confirmed, as intended for the flight
  build.
- The reliability, safety and autonomy reviews called for in
  `STP_protocol_req.md`, including any radiation mitigation such as triple
  modular redundancy, have not been started.
