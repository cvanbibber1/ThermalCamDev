# Thermal HRT control: current flashed image

Thermal layout 3 gate at LRT payload bytes 244–245 is **0 after reset**.
Only a standalone STP HRT GO request while thermal owns the bus opens it.
HRT STOP or STOP WITH LOSS closes it and ends an active capture. GO never
starts a capture on its own. While closed, CAPTURE_IMAGE (0x30), STREAM_START
(0x78), and their thermal aliases return HRT_STOPPED (result 7) without
starting FFC or sending HRT. A new command sequence after GO recovers.

## Individual-packet checkout on COM56

Run from `C:\Github\ThermalCamDev`:

```powershell
python tools/verify_persistent_hrt.py --port COM56 --output validation/explicit_go_stop_wire_trace_com56.json
```

The verifier uses direct pyserial writes, not either camera app. It records
every request's complete wire hex, command opcode/sequence, and matching LRT
result/gate in `wire_steps`. The exact successful run is also rendered as
`validation/EXACT_INDIVIDUAL_COMMANDS_COM56.md`, including every 105-byte
DICE command payload. GO is a 14-byte type 0x87 request
(`1ACFFC1D00000000000087C71A9A` at zero timestamp); STOP is type 0x85
(`1ACFFC1D00000000000085C77CF8`). They must not be sent inside the
105-byte command body.

Expected order:

1. Select thermal (0x6F, index 1); LRT gate=0.
2. CAPTURE_IMAGE; result HRT_STOPPED, no HRT packets.
3. GO, then a new CAPTURE_IMAGE; result OK and one complete 38,400-byte frame.
4. STOP; CAPTURE_IMAGE and STREAM_START each return HRT_STOPPED; no HRT resumes.
5. GO, then a new STREAM_START; multiple complete frames transmit without
   another GO.
6. STOP; after the current DMA packet drains, no further HRT starts and
   LRT gate remains 0.
7. GO, then a new CAPTURE_IMAGE; one complete frame proves recovery.

The two initial COM56 runs and the final wire-trace run passed all 15 checks
with zero outer CRC errors. The current flashed 46,776-byte firmware and
independent SWD readback SHA-256 are
`07467DC066A88AA6016D97C2CEFD588D1FD80F944BB05AA357FB69D07460AE22`.
This bench result does not replace a coordinator cycle B or synchronized
RS-422/DE handoff trace for flight signoff.
