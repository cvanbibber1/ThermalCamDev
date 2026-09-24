# Thermal persistent HRT GO checkout — 2026-09-24

## Status: explicit GO/STOP COM56 pass; coordinator and physical trace pending

### Current flashed candidate: explicit GO/STOP

The 46,776-byte flashed image and independent SWD readback both have SHA-256
`07467DC066A88AA6016D97C2CEFD588D1FD80F944BB05AA357FB69D07460AE22`.
Its LRT layout 3 source digest is
`b60039916bcb89858f4f99c30bc0dfc0454037342c90dd3a7e08df70454d6b96`.
Two individual-command COM56 runs passed all 15 state-machine checks:
gate=0 after reset; CAPTURE_IMAGE refused as HRT_STOPPED; GO then one
complete 38,400-byte still; STOP then both CAPTURE_IMAGE and STREAM_START
refused; GO then 19 and 22 complete stream frames respectively; STOP
prevented further HRT; GO then another complete still. Both runs had zero
outer CRC errors. Reports: `explicit_go_stop_individual_com56.json` and
`explicit_go_stop_individual_repeat_com56.json`. No GUI/app command sequence
was used for these tests.

The boot-open experiments below are historical and are not the image now
flashed on COM56.

### Superseded boot-open candidate

The thermal board was reflashed with the boot-open gate candidate. Its
46,736-byte firmware SHA-256 is
`F544FC27B901EDD4C8B6EA0147A0E3DE9D5F77F1595795B0C554290C7F87A00B`;
OpenOCD verification and independent 46,736-byte SWD readback matched.
Thermal LRT reports layout 3, valid identity CRC and source digest
`6d9a1b381defe83822edb0a2fe4bb9c17915c82b11c84bf00f6a83baba050cce`.
The test selected visual, sent STOP while visual owned the bus, selected
thermal, then captured a still with **zero GO requests**. LRT showed the gate
open, the still assembled to 38,400 bytes, and outer CRC errors were zero.
After an explicit thermal STOP, one GO produced 26 complete stream frames in
five seconds; the final STOP closed the gate. All nine checks passed in
`boot_open_cycle_b_com56.json`. The earlier candidate below is historical.

A second run, `boot_open_cycle_b_handoff_repeat_com56.json`, additionally
switched to visual and back after an explicit thermal STOP. LRT still showed
gate=0; one GO then yielded 31 complete stream frames. All ten checks passed
with zero outer CRC errors. The independent Radcam Host checkout at
`C:\Github\Radcam-Host\validation\boot_open_host_checkout.json` used no
thermal GO before its still or stream: one still, 31 complete thermal stream
frames, 48 visual frames, 756 valid packets and zero packet CRC errors.

The flashed thermal candidate is 46,736 bytes, SHA-256
`C62D5A7928E9E3A5494153B59E8EDC86B0FE5EC43DF1FDCA0E632C70E0414CB3`.
OpenOCD `program ... verify reset exit` succeeded over the connected FT2232H
SWD adapter. An independent readback from `0x08000000` of exactly 46,736
bytes has the same SHA-256. Copies are
`validation/persistent_hrt_candidate_2026-09-24.bin` and
`validation/persistent_hrt_final_readback.bin`. Thermal LRT layout 3 reported
a valid identity CRC, source digest
`88866d4ef4057cd9fead480ea0c9e692117e1413a22ce3492d856f955ce7a87f`,
and embedded commit `3795985ab06485dfd9b4a395efb054e06647cb84`.
The source tree has uncommitted changes, so that embedded commit names the
repository base, not a final release commit; the source digest and binary
hash identify this exact candidate.

The target PlatformIO build succeeded, and all 29 native tests passed. Radcam
Host tests passed after adding layout 3 decoding and single-GO session
behavior. `tools/verify_persistent_hrt.py` ran twice on COM56 with a 1 MB
serial receive buffer. Each run sent exactly two GOs total: one during FFC
for a one-shot, and one for a five-second stream. Results:

| Run | One-shot | Stream | HRT packets | Outer CRC errors | STOP/gate closed |
|---|---|---:|---:|---:|---|
| `persistent_hrt_final_com56_buffered.json` | 1 complete 38,400-byte frame | 30 complete frames | 354 | 0 | Pass |
| `persistent_hrt_final_com56_buffered_repeat.json` | 1 complete 38,400-byte frame | 29 complete frames | 347 | 0 | Pass |

A Radcam Host checkout on the flashed image is stored at
`C:\Github\Radcam-Host\validation\persistent_hrt_host_checkout.json`.
It decoded layout 3 with valid identity CRC, one thermal still and 30 thermal
stream frames, plus 48 visual frames. Its framer counted 765 good packets and
zero bad packet CRCs. The host stream stopped at an arbitrary time and had
one incomplete trailing thermal frame; the 30 completed frames were clean.

The older `tools/verify_flashed_thermal.py` intentionally switches bus
ownership while an HRT packet is active. That stress run delivered the
one-shot frame and active stream but counted one CRC error at handoff. It
does not establish whether an electrical collision occurred. A synchronized
thermal/visual DE, UART TX, A/B and host trace is still required before
flight signoff. Do not attribute the CRC to one node without that trace.

The first normal verifier, without the 1 MB serial receive buffer, also
counted a corrupted packet while stopping a saturated stream. Adding the
same buffer size already used by `stp_monitor.py` yielded the two clean runs
above. This indicates a host receive-buffer limitation in that verifier,
not a demonstrated firmware CRC defect.

The old cycle-B emulator HRT request was wrapped as a type `0x10` command.
The new gate behavior does not accept that malformed envelope. Repeat the
cycle with one standalone 14-byte type `0x87` GO, then STOP, and preserve the
returned `.dat` files.
