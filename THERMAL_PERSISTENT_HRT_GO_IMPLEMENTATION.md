# Thermal HRT GO behavior for the shared flight bus

Status: explicit GO/STOP gate implemented and flashed on 2026-09-24. Individual
COM56 command acceptance passed; coordinator acceptance remains. See
`validation/PERSISTENT_HRT_ACCEPTANCE_2026-09-24.md`. This replaces thermal layout 2's
one-packet-per-GO credit scheme with the visual camera's persistent HRT tap.

## Wire behavior

1. Thermal boots with the HRT gate closed. Only the selected bus owner may act
   on a standalone 14-byte STP HRT GO request (type `0x87`, target `0xC7`).
   GO opens a gate closed by boot or STOP. GO does not start a capture or create a
   frame by itself.
2. Once a capture or stream has image data available, the selected node sends
   all queued HRT packets while the gate is open. No additional GO request is
   needed between packets or frames. ACK and requested LRT retain priority.
3. HRT STOP (`0x85`) and STOP WITH LOSS (`0x86`) close the gate and stop
   capture only when thermal owns the bus. Thermal ignores a visual-owner
   STOP. Bus ownership loss stops capture and drops queued replies, but does
   not change the gate. Re-selection preserves its prior open or stopped state.
4. GO must precede CAPTURE_IMAGE. The gate remains open during FFC; the
   completed one-shot frame flows when ready and then capture becomes idle.
   The gate stays open until a thermal-owner STOP, so a subsequent capture
   can use the same tap.
   CAPTURE_IMAGE and STREAM_START while closed return HRT_STOPPED (result 7)
   without starting FFC or capture. GO followed by a new command recovers.
5. Layout 3 thermal LRT retains layout 2 offsets and CRCs. Its u16 at
   bytes 244–245 is `0` for closed or `1` for open, rather than packet
   credits. Main layout word at offset 0 and extension version at 232 are `3`.
   The identity block at 268–325 is unchanged.

## Implementation locations

- `include/protocol/hrt_gate.h` and `src/protocol/hrt_gate.c` implement the
  persistent gate and transmit predicate.
- `src/drivers/stp_link.c` opens the gate on GO, closes it on a
  thermal-owner STOP, and gates the transmit loop on selected ownership, capture
  readiness and image readiness. It no longer decrements a packet credit.
- `include/stp_link.h` defines thermal LRT layout 3. `tools/stp_monitor.py`
  decodes the new gate field. The Radcam Host decoder handles both old layout
  2 credits and new layout 3 gate state; its session suppresses GO while the
  layout 3 gate is already open and retains old credit pacing for layout 2.

## Acceptance on the flashed unit

1. Build the target firmware and run native tests. Record source revision,
   binary SHA-256, flash verify and independent readback SHA-256.
2. After reset, select thermal and verify CRC-valid layout 3 LRT gate=0.
   CAPTURE_IMAGE must return HRT_STOPPED and send no HRT. Send one GO, then
   CAPTURE_IMAGE; require one complete 160×120×16-bit frame and valid CRCs.
3. Send thermal STOP; require gate=0. CAPTURE_IMAGE and STREAM_START must
   return HRT_STOPPED. Send one GO, then STREAM_START.
   Require multiple complete frames over several seconds. Confirm packet
   sequence, frame assembly, absence of bus collisions and fair LRT replies.
4. Send STOP/STOP WITH LOSS; require no further HRT and gate value 0. Restart
   with GO and require a keyframe. Switch ownership during active HRT and
   verify no thermal packet begins after handoff, no DE overlap, and no stale
   data after thermal re-selection. If STOP was sent while thermal owned the
   bus, GO is required to reopen its gate.
5. Repeat the coordinator emulator's cycle B with one standalone GO before
   CAPTURE_IMAGE, then STOP/GO recovery. A 14-byte GO placed in the 105-byte
   command body still fails because that is a malformed envelope.

Bench COM56 function tests do not substitute for a synchronized DE/UART/A-B
trace or the real coordinator's end-to-end transport test.
