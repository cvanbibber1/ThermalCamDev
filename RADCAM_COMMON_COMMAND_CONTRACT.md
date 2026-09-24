# Shared camera command contract for visual-team review

Status: thermal HRT GO behavior updated 2026-09-24 to match the reported
visual tap behavior. Common capture/recording semantics still need visual-team
approval. Do not present unsupported thermal storage as a successful common
operation.

Both nodes share STP Target ID `0xC7` at 921600 8N1. V3 command payloads use
opcode u8, big-endian sequence u16, argument length u8, force u8, big-endian
CRC-16/CCITT-FALSE over the header and little-endian arguments, then arguments
and padding. The packet has its own outer CRC. The selected node alone may
transmit. The eight-byte ACK means transport acceptance; a sequence-matched
LRT result establishes command completion.

| Operation | Opcode and arguments | Thermal result | Visual result / decision needed |
|---|---|---|---|
| Bus ownership | `0x6F`, node index u8; `0xFF` means none | Moves transmit token; only new owner ACKs | Same bus rule. `0xFF` has no ACK because no node owns the bus. |
| Local sensor selection | `0x6D`, local index u8 | Fixed Lepton `0` or off `0xFF`; never grants bus access | Must be made independent of bus permission. Current visual firmware can produce overlapping replies if `0x6D 0` is sent while thermal owns the bus. |
| Still image | `0x30`, no arguments | One ephemeral corrected HRT frame, then idle; no media ID | Existing visual legacy capture stores media. Decide whether visual `0x30` should become an ephemeral frame, or explicitly document this as a capability-dependent outcome. `0x66` remains visual slot capture. |
| Recording start/stop | `0x31` / `0x32`, no arguments | `UNSUPPORTED`: no device-side stored recording | Existing visual recording stores media. Keep these storage operations visual-only unless both teams agree to redefine them. The host can record either live stream to its own disk. |
| Live stream start/stop | `0x78` / `0x79`, no arguments | Starts/stops radiometric HRT frames | Starts/stops visual HRT video. Frame payload formats differ by node kind; the host must use the selected node's decoder. |
| Thermal compatibility aliases | `0x02` still, `0x03` / `0x04` corrected live stream start/stop, `0x05` / `0x06` uncorrected live stream start/stop | Accepted | Do not send to visual as common commands. |
| Persistent slots/download | `0x64`–`0x6C`, media list/request `0x21` / `0x40` | `UNSUPPORTED` | Visual capabilities only. |
| Thermal image mode | `0x74`, mode/palette/depth u8 | Radiometric mode 0 and depth 16 are supported; palette modes 1/2 return `UNSUPPORTED` | Palette display can be applied in the host to radiometric pixels. |

Thermal layout 3 boots with its HRT data tap closed. Only STOP/STOP WITH LOSS
received while thermal owns the bus closes it; a standalone HRT GO (type
`0x87`) opens it. Bus deselection halts capture and output but preserves
gate state. Available packets flow without repeated GO. GO alone does not
start capture. A new capture or stream begins with a keyframe. Thermal
layout 3 reports the gate state at LRT bytes 244–245; older layout 2
reported packet credits there.

Before declaring this contract final, the visual team must decide the `0x30`
observable result and correct visual `SELECT_CAMERA` so it never grants RS-422
transmit permission. Keep the shared host's capability-aware controls and
sequence-correlated thermal LRT layout 2 decoding in the packaged release.

## Radcam Host setup against this firmware

1. Run the checked-out Radcam Host **source** with the FTDI serial port at
   921600 8N1 and Target ID `0xC7`. The packaged executable was not tested
   against this image.
2. Send `BUS_SELECT_CAMERA 1` to grant the thermal node the RS-422 bus. Read a
   layout 3 thermal LRT and require a valid extension CRC, `node_index = 1`,
   `bus_owner = 1`, and the sequence-matched `0x6F` result OK. Do not send
   `SELECT_CAMERA 0` while thermal owns the bus; the current visual firmware
   has previously replied concurrently in that condition.
3. For a still, send `CAPTURE_IMAGE 0x30`, poll LRT for its result, then send
   one standalone GO if layout 3 LRT says gate=0, then collect the single 160×120
   radiometric frame. For continuous frames, use `STREAM_START 0x78` and
   collect while gate=1. After an explicit thermal STOP, send one standalone
   GO to reopen. End with HRT STOP (`0x85`) and `STREAM_STOP 0x79`. Decode the thermal
   keyframe/interframe format and request a keyframe after loss. Store a host
   recording locally if desired; thermal device slots do not exist.
4. Before handing the bus to visual node 0, stop the stream, close the HRT
   tap, let the current packet drain, and send `BUS_SELECT_CAMERA 0`. Check
   the new owner's LRT before enabling camera-specific controls.
   `BUS_SELECT_CAMERA 0xFF` makes the bus
   silent.

The checked-out Radcam Host `GroundSession` decodes thermal layouts 2 and 3,
matches command sequences, uses one persistent GO for layout 3, and decodes
thermal frames. The 2026-09-24 flashed layout 3 candidate passed two normal
COM56 runs with one complete still and 29–30 stream frames per run, zero CRC
errors, and one GO for each capture phase. The proposed common still semantics need
cross-team agreement before treating the two-node system as fully verified.
