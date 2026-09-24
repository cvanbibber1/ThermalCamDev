# Thermal firmware acceptance — COM56 — 2026-09-22

## Artifact identity

- Git HEAD: `3795985ab06485dfd9b4a395efb054e06647cb84`. The working tree
  contains uncommitted firmware changes; HEAD alone does not identify this
  build. SHA-256 of the firmware-source diff (`src/drivers/stp_link.c` and the
  four relevant `include/` headers):
  `f2bbaa7caadcef189986258de0131a0b95de3ad87126184d2e60782f068ed15f`.
- Built `.pio/build/target/firmware.bin` length: 46,640 bytes. SHA-256:
  `C1788CC8B031A4F25277D2A3D82EC9A6C18891FB0881433CECFA077675C47C4C`.
- OpenOCD `program ... verify reset exit` passed over FT2232H SWD. An
  independent readback of flash address `0x08000000` for 46,640 bytes had the
  same SHA-256 as `firmware.bin`. A rebuild from the current source reproduced
  the same binary hash.
- After readback and reset, node 1 ACKed `BUS_SELECT_CAMERA 1`; LRT layout 2
  reported node index/kind `1/thermal`, bus owner 1, valid extension CRC,
  command sequence 1, opcode `0x6F`, and result OK.

## Live protocol results

Setup: thermal node 1 on shared Target ID `0xC7`, COM56, 921600 8N1. The
100-switch check from the preceding hardware run returned 100 ACKs and 100
CRC-valid LRTs, 50 from each node. A repeat after the final flash could not
run because visual node 0 gave no ACK or LRT when selected; thermal node 1
continued to ACK and send layout 2 LRTs after re-selection. This may be a
visual-node power/connection issue; the current setup does not establish its
cause. `BUS_SELECT_CAMERA 0xFF` left the thermal bus silent.

The final thermal run is recorded locally in ignored
`tmp/thermal_acceptance.json` and `tmp/thermal_acceptance.bin`; focused loss
and NUC checks are in `tmp/thermal_loss_probe.json` and
`tmp/thermal_nuc_stream_probe.json`.

| Check | Result |
|---|---|
| Command outcomes | 32 V3 commands in the main run had sequence-matched LRT results and valid extension CRCs. Bad inner CRC and wrong argument length produced BAD_PARAM and no ACK. Unknown opcode returned UNKNOWN_COMMAND. Sequence `0xFFFF` followed by `0` worked. Duplicate NUC sequence did not rerun NUC. |
| Unsupported capabilities | Common stored recording start/stop, 10 slot/media actions, and palette/both HRT output returned UNSUPPORTED. |
| Settings | Manual range including negative low bound, automatic range, emissivity 0.950, reflected temperature −12.50 °C, and a bottom-right spot rectangle passed. Emissivity endpoints 0.001/1.000 and reflected −327.68 °C were accepted. Palette indices 0–4 were accepted as telemetry settings. All four one-pixel image corners were accepted as spot regions. Invalid range, emissivity, palette, and spot parameters returned BAD_PARAM and preserved previous telemetry values. Partially overlapping spot `(159,119,8,8)` reported effective `(159,119,1,1)`. |
| NUC/FFC | NUC succeeded and incremented its counter once. During a live stream, NUC reported CORRECTING, emitted no immediate HRT, and resumed after settling at keyframe chunk 0. |
| One-shot | `CAPTURE_IMAGE 0x30` emitted exactly one complete 38,400-byte frame in 13 HRT chunks. Five extra GO requests emitted no more HRT. |
| Recording alias | `0x03` emitted three complete frames: first keyframe, then interframes; `0x04` stopped output. Five extra GO requests emitted no HRT. |
| Sustained stream | 100 sequential 160×120 radiometric frames decoded from 1,120 HRT chunks; 0 discarded frames, 0 undecodable frames, 0 outer CRC errors, 10 keyframes. |
| Loss recovery | Deliberately omitting interframe generation 1424 chunk 1 caused loss; `REQUEST_KEYFRAME 0x08` yielded a later keyframe (generation 1436) that decoded successfully. |
| Stop/deselect | HRT STOP_WITH_LOSS and bus deselection during an active frame produced no further HRT on five extra GO requests. Re-selection reported idle capture and zero credits; the next stream began at keyframe chunk 0. |
| Checked-out Radcam Host source | `GroundSession` on COM56 selected node 1 with sequence-matched result OK and valid thermal layout 2 LRT; common `CAPTURE_IMAGE` delivered exactly one decoded thermal frame; common `STREAM_START` delivered three more decoded frames and `STREAM_STOP` succeeded. The offscreen `MainWindow` displayed a live 160×120 radiometric thermal frame and updated its temperature readout. Final runs had zero thermal decode errors and zero outer CRC errors. |

## Remaining limits

- No calibrated blackbody or known-temperature target was available. Pixel
  geometry, little-endian `uint16` decoding, centikelvin telemetry, and frame
  checksums were verified; absolute temperature accuracy was not.
- The test checked packet CRCs and decoded frames, not DE timing with a logic
  analyzer. The earlier 100-switch run had no packet corruption; DE overlap was
  not measured electrically. The final-flash two-node rerun is blocked by node
  0 not responding.
- The visual team's approval of the common capture/recording contract remains
  open. See `RADCAM_COMMON_COMMAND_CONTRACT.md`.
- The checked-out Radcam Host source now recognizes thermal LRT layout 2 and
  sequence-correlates results; the packaged executable has not been rebuilt or
  exercised here. The visual node still needs a `SELECT_CAMERA` fix: in an
  earlier run, sending local selection `0` while thermal owned the bus caused
  corrupted overlapping LRT replies until bus ownership was reasserted.
