---
type: interface-spec
status: review
implementation-phase: last
transport: ADM2582E
---

# Four-wire multidrop RS-485 protocol

## 1. Electrical topology

The required multi-camera behavior is not a multidriver RS-422 bus. Implement the
ADM2582E network as four-wire multidrop RS-485:

- Master TX pair fans out to every camera receiver A/B.
- All camera driver Y/Z pairs share the return pair to the master receiver.
- Only the addressed/granted camera may assert DE and transmit.
- Camera receivers remain enabled; RE wiring must be confirmed from the schematic.
- Terminate each pair at the two physical ends only; do not terminate every camera.
- Install fail-safe bias at one controlled location per pair.
- Use a daisy-chain trunk with short stubs, not a star, unless SI testing approves it.
- Follow ADM2582E isolation-plane, ferrite, decoupling, creepage, thermal, and emissions
  layout guidance.

The current hard pull-up on DE must be removed. Fit a pulldown so the driver is disabled
during reset, boot, watchdog recovery, and unpowered MCU states.

## 2. UART and DE timing

- USART2 on PA2/PA3, 8 data bits, no parity, 1 stop bit.
- Bring-up rates: 1,000,000 and 3,125,000 baud.
- Full-video qualification rate: 6,250,000 baud, the practical USART2 limit with a
  50 MHz APB1 clock and oversampling by 8.
- PB7 is software-controlled DE.
- Assert DE at least one character time before the first start bit.
- Deassert only after the USART transmission-complete flag, not DMA-complete, plus a
  configurable guard interval initially set to one bit time.

The ADM2582E is rated to 16 Mbit/s, but MCU clocking, cable length, topology, termination,
EMC, and isolation layout determine the qualified system baud rate.

## 3. Bus rules

The bus is master-centric. A camera never transmits unless one of these is true:

1. It has received a unicast request addressed to it.
2. It holds an explicit time-limited stream grant from the master.
3. It is participating in a discovery window and its assigned slot is active.

Only one response may occupy the camera-to-master pair at a time. Unsolicited alarms are
latched and returned in the next poll; they do not seize the bus.

Version 1 addresses:

- `0x01` to `0xEF`: camera node addresses; the firmware default is `0x01`.
- `0xF0`: master/host source address.
- `0xF1` to `0xFE`: reserved.
- `0xFF`: broadcast destination from the master. Cameras respond only to the discovery
  and UID-targeted address-assignment commands.

Each camera also exposes the complete 96-bit STM32 unique ID. Address assignment contains
that UID and therefore only the selected camera responds. The implemented assignment is
runtime-only; versioned, CRC-protected flash persistence is a release item.

## 4. Byte framing

Messages are COBS encoded and terminated by `0x00`. The decoded binary record uses
little-endian integers:

| Field | Bytes | Description |
|---|---:|---|
| Magic | 2 | Little-endian `0x4354`; wire bytes `54 43` (`TC`) |
| Version | 1 | Protocol major version, initially 1 |
| Kind | 1 | 1=request, 2=response, 3=event, 4=frame chunk |
| Flags | 1 | bit 0 error, bit 1 more, bit 2 acknowledgement required |
| Source | 1 | Node address |
| Destination | 1 | Node address |
| Reserved | 1 | Must be zero in version 1 |
| Sequence | 4 | Request/response correlation |
| Opcode | 2 | Operation identifier |
| Payload length | 2 | 0 to 2048 bytes |
| Payload | N | Type-specific data |
| CRC-32C | 4 | Castagnoli CRC over the 16-byte header and payload |

Reject wrong magic/version, impossible length, oversized payload, invalid address, or bad
CRC before dispatch. A parser timeout drops the partial record at the next zero delimiter.

## 5. Version 1 implemented operations

Every response repeats the request opcode and sequence. Its payload starts with a
little-endian signed 16-bit result (`0` success, negative error), followed by operation
data.

| Opcode | Name | Request payload | Successful response data |
|---:|---|---|---|
| `0x0001` | `GET_INFO` | none | firmware/protocol versions, 96-bit UID, capabilities |
| `0x0002` | `GET_HEALTH` | none | health counters |
| `0x0003` | `DISCOVER` | none | same identity block as `GET_INFO` |
| `0x0100` | `LEPTON_STATUS` | none | state, last CCI result, frame generation, VSYNC time |
| `0x0101` | `LEPTON_CCI_GET` | command ID, maximum word count | count and result words |
| `0x0102` | `LEPTON_CCI_SET` | command ID, count, words | none |
| `0x0103` | `LEPTON_RUN_FFC` | none | none |
| `0x0104` | `LEPTON_CCI_RUN` | command ID | none |
| `0x0105` | `LEPTON_REG_READ` | register address | register value |
| `0x0106` | `LEPTON_REG_WRITE` | register address and value | none |
| `0x0200` | `STREAM_STATUS` | none | frame generation and byte count |
| `0x0201` | `FRAME_CHUNK` | generation, byte offset, requested length | generation, total, offset, bytes |
| `0x0300` | `DOSIMETER_STATUS` | none | timestamp, ADC statistics, voltages, radiation, flags |
| `0x0400` | `BUS_STATUS` | none | baud rate and node address |
| `0x0401` | `BUS_ASSIGN_ADDRESS` | 96-bit UID and new address | assigned address |

The raw register operations are maintenance functions. The command-ID operations are the
preferred way to access Lepton CCI. UVC video remains a USB interface; RS-485 exposes the
same radiometric frame through bounded snapshot chunks.

Configuration persistence, calibration writes, autonomous stream grants, reboot, and
bootloader entry are planned extensions and are not represented as implemented commands.

## 6. Discovery

The current baseline broadcasts `DISCOVER`; each camera schedules its reply after
`1 + (UID0 xor UID1 xor UID2) mod 31` milliseconds. This spreads typical replies but does
not guarantee collision-free discovery. The master retries until every expected UID is
observed, then broadcasts `BUS_ASSIGN_ADDRESS` with a specific 96-bit UID and address.

The production revision should add a request nonce, slot width, and slot count, choosing a
slot from `CRC32C(UID || nonce) mod slot_count`. That change requires a protocol minor
revision and multi-node validation.

## 7. Video transfer and capacity

Frames are requested in chunks. A request contains frame generation (zero selects the
latest frame), byte offset, and requested size. A response identifies its generation,
total byte count, and offset; `MORE` is set while bytes remain. The current host requests
1900 data bytes per record. If the producer publishes a different generation during a
multi-request snapshot, the camera returns `STALE_FRAME` and the host restarts.

At 6.25 Mbaud with 8N1, the theoretical byte rate is 625 kB/s. One 38.4 kB stream near
8.6 fps consumes roughly 330 kB/s before protocol overhead. Therefore one camera can be
carried at full frame rate with control margin; two full-rate cameras cannot be guaranteed.
The master must allocate lower frame rates or snapshot slots when several cameras share a
bus.

## 8. Reliability and security

- Every request has a bounded response deadline and at most a configured retry count.
- The host uses a fresh sequence for every request and verifies the response sequence and
  opcode. Duplicate-response caching is not implemented yet.
- CRC errors are silent on a shared bus and counted locally.
- Future configuration writes and reboot/bootloader commands must require maintenance
  mode and confirmation.
- A future authenticated envelope may be added without changing COBS framing; version 1
  provides integrity, not cryptographic authenticity or confidentiality.

## 9. Acceptance criteria

- No driver assertion during reset or address collision.
- 24-hour multidrop soak with zero undetected corruption.
- Recovery after cable disconnect, truncated packet, injected CRC error, and node reset.
- Qualified BER and EMC at every supported baud and maximum cable/topology configuration.
- Command parity with USB for all supported camera and telemetry operations.
- One-camera full-rate radiometric stream at the qualified maximum baud with bounded
  command latency.
