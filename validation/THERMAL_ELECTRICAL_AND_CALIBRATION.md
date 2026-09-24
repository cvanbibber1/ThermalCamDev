# Thermal board acceptance still requiring physical measurements

The candidate built from this checkout on 2026-09-23 is 46,668 bytes with
`firmware.bin` SHA-256
`321DBB56FEF53DC9FA1D0314002439A533BEAC45A6C0733129A9984195A07B4A`.
Its embedded commit is `3795985ab06485dfd9b4a395efb054e06647cb84` and
source digest is
`EC1FFA1E7EF2C64317F8D684B26E6D8E66F3A2E6AA7956B555E80CFE3EADC42B`.
OpenOCD `program ... verify reset exit` passed on the connected FT2232H SWD
target. An independent 46,668-byte readback from `0x08000000` matched the
binary SHA-256 above. After reset, COM56 returned an ACK to
`BUS_SELECT_CAMERA 1` and seven CRC-valid LRTs reporting this exact commit and
source digest, a valid identity block, thermal ownership and command result
OK. The previous flashed binary is identified separately in
`THERMAL_ACCEPTANCE_COM56.md`; its live transport results do not prove this
new firmware's electrical timing or calibration.

With visual selected (owner 0), COM56 returned one ACK and five CRC-valid
LRTs from the visual node; these have its different layout and were not
interpreted as thermal health. With owner `0xFF`, repeated LRT polling for
2.2 seconds returned zero packets. Reselecting thermal returned one ACK and
five CRC-valid thermal LRTs with the same identity. These checks establish
protocol behavior only; they do not measure DE edges or electrical overlap.
With visual still owning the bus, a local `SELECT_CAMERA 0` returned visual
ACK/LRT traffic and zero packet CRC errors; no thermal identity was observed.
Thermal was then reselected successfully.

## Electrical bus handoff

Use the final flashed thermal binary with the visual board connected and
powered. Record its binary SHA-256, the telemetry `build_commit` and
`build_source_sha256`, both board identities, baud rate, analyzer sample rate,
and firmware settings alongside the raw trace. Probe thermal DE (PB7),
thermal UART TX (PA2), visual DE/TX, and the shared differential pair. Decode
921600 baud, 8N1. Sample fast enough to resolve a bit (at least 10 MS/s).

1. Select thermal with `BUS_SELECT_CAMERA 1`; request ACK, LRT and a live HRT
   stream. Grant one HRT GO at a time, then queue several while a frame is in
   progress. Capture a handoff command to visual (`BUS_SELECT_CAMERA 0`) during
   a thermal HRT packet, followed by LRT and additional HRT GO requests.
2. Repeat with owner `0xFF`. Repeat with a local `SELECT_CAMERA 0` command
   addressed while visual owns the bus. Include a transfer with ACK and LRT
   queued on thermal before the handoff.
3. For every thermal packet, mark the final UART stop bit and the thermal DE
   falling edge. DE must stay asserted through that stop bit and fall after
   it. Once handoff is parsed, the in-progress packet may finish; no later
   thermal packet or DE rising edge may occur until thermal is reselected.
   Thermal and visual DE must never overlap. With owner `0xFF`, both DE lines
   must remain low after any already active packet completes.
4. Re-select thermal and verify that stale ACK/LRT/HRT credits do not transmit;
   a new stream starts at keyframe chunk zero. Check packet CRCs and correlate
   the capture to the command sequence in LRT.

Save analyzer project, exported CSV, and a screenshot showing the active HRT
handoff and final stop bit. Record measured DE release delay from final stop
bit and maximum handoff latency. The existing COM56 protocol run did not
measure these signals; it is not an electrical pass.

## Radiometric accuracy

Byte-exact frame transport is recorded in `THERMAL_ACCEPTANCE_COM56.md`.
For flight calibration acceptance, use traceable blackbody targets spanning
the intended scene range and chamber conditions spanning intended board and
sensor temperatures. Record target certificate, distance, ambient and
reflected apparent temperature, target emissivity, programmed emissivity,
frame generation, sensor FFC/NUC state, and raw centikelvin pixels. Measure
center and corner ROIs at each point, both before and after automatic and
commanded NUC, and during warm-up and steady state. Compare absolute error,
repeatability and drift to mission-approved limits; preserve raw frames and
the calculation sheet. The current host decode result is transport evidence,
not evidence of absolute temperature accuracy.

## Layout 2 validity and build identity

Thermal LRT payload bytes 268–325 contain optional identity extension version
1. Bytes 268 and 269 hold extension version and validity bits: bit 0 build
identity, bit 1 thermal counters, bit 2 reset cause, bit 3 fault code. Byte
270 availability bits 0–2 would indicate CPU load, device storage and safe-mode
health respectively; all are zero in this firmware because those measurements
are not implemented. Bytes 272–291 hold the 20-byte Git commit; bytes 292–323
hold a SHA-256 of build input source files; bytes 324–325 hold CRC-16/CCITT-
FALSE over 268–323. A decoder must verify extension version and CRC before
using these fields. The source digest identifies inputs, not the flashed
binary. Record the full `firmware.bin` SHA-256 separately and verify a flash
readback before claiming that binary is on a board. Existing LRT health words
at 64–191 include Lepton/VoSPI, codec, transport, reset and fault counters.

Thermal slot listing, device recording and media commands report
`UNSUPPORTED`. Host-side recording of decoded radiometric frames remains the
supported persistence path.

## Reflash and live checks — 2026-09-23

The board was flashed again from this checkout on branch `flight-ready` with
OpenOCD `program ... verify reset exit`. A separate 46,668-byte flash readback
matched `firmware.bin` SHA-256
`321DBB56FEF53DC9FA1D0314002439A533BEAC45A6C0733129A9984195A07B4A`.
The pre-flash readback had the same hash. Run
`python tools/verify_flashed_thermal.py --port COM56` to repeat the protocol
check; its full local result is written to ignored
`tmp/verify_flashed_thermal.json`. All eight protocol checks passed on the
reflash. The script explicitly reports `electrical_de_measured: false`.

COM56 at 921600 baud returned a CRC-valid layout 2 LRT with identity-block
CRC valid, source digest and commit matching this flashed image, and CPU,
storage, and safe-mode validity all false. Common stored recording start/stop,
slot list, and slot recording start each returned sequence-matched
`UNSUPPORTED`. After the 1.5-second FFC settle interval, `CAPTURE_IMAGE`
produced one decoded 160×120×16-bit frame (38,400 bytes) in 13 CRC-valid HRT
packets. The run had zero packet CRC errors.

During a live stream, the host sent a handoff to visual after it observed the
start of an HRT packet, with ACK, LRT, and HRT requests immediately ahead of
the handoff. The HRT packet passed CRC. Subsequent polling with visual as owner
returned visual traffic and no thermal identity or HRT; local sensor selection
did not revive thermal traffic. With owner `0xFF`, polling returned no packets.
Thermal re-selection restored its identity response. This is protocol-level
evidence; it cannot show when the thermal MCU parsed the handoff relative to
the UART stop bit or whether DE lines overlapped electrically.
