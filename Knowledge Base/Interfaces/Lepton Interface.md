---
type: interface-spec
status: review
device: FLIR Lepton 3.1R
---

# Lepton 3.1R interface

## Electrical contract

- Sensor resolution: 160 x 120.
- Radiometry: supported; TLinear default output resolution is 0.01 K.
- Effective unique frame rate: just below 9 Hz.
- Master clock: 25 MHz nominal, 24.975 to 25.025 MHz permitted, 45% to 55% duty.
- VDDC: 1.20 V nominal.
- VDD: 2.80 V nominal.
- VDDIO: 2.8 to 3.1 V.
- CCI: I2C-like, 7-bit address `0x2A`, 100 kHz/400 kHz/1 MHz supported.
- VoSPI maximum SCK: 20 MHz.

GPIO0, GPIO1, and GPIO2 are reserved and must not be driven. GPIO3 is the optional
VSYNC signal. There is no supported Lepton UART in the current public pin definition.

## CCI contract

Use I2C1 on PB8/PB9 at 400 kHz for bring-up. Registers and data are 16-bit, MSB first.
The low-level driver must support repeated-start reads, sequential 16-bit reads/writes,
timeouts, and bus recovery.

Important base registers:

| Register | Address | Purpose |
|---|---:|---|
| Power-on | `0x0000` | Write zero to wake after software power-off |
| Status | `0x0002` | Busy, boot mode/status, command result |
| Command | `0x0004` | Module, command ID, GET/SET/RUN |
| Data length | `0x0006` | Number of 16-bit words |
| Data 0..15 | `0x0008`..`0x0026` | Short command data |
| Block buffer 0 | `0xF800`..`0xFBFF` | 1024-byte block |
| Block buffer 1 | `0xFC00`..`0xFFFF` | 1024-byte block |

Never issue a command while BUSY is set. Poll with a deadline, decode the signed response
code, and preserve the exact Lepton error in the external response.

## VoSPI contract

Use SPI2 with PB13 SCK, PB14 MISO, PB12 software CS, and PB15 held low. Configure mode 3
(`CPOL=1`, `CPHA=1`), MSB first. With APB1 at 50 MHz, use 12.5 MHz SCK for the baseline.

Raw14 packet size is 164 bytes: a 4-byte ID/CRC header and 160-byte payload. With
telemetry disabled, each segment has 60 packets and each valid frame has four segments.
Packet 20 encodes segment 1..4; segment zero is invalid and must be discarded.

Acquisition algorithm:

1. On unsynchronized entry, keep CS high and SCK idle for more than 185 ms.
2. Clock complete 164-byte packets and ignore discard IDs (`xFxx`).
3. Receive a full segment with DMA, validate packet numbering and segment ID from packet 20.
4. Verify CRC-16 polynomial `x^16 + x^12 + x^5 + 1` for each packet.
5. Copy payload half-lines into the correct 160 x 120 frame positions.
6. Accept a frame only after segments 1, 2, 3, and 4 arrive in order.
7. Invalid partial frames have segment ID zero and are consumed but not published.
8. On sequence or CRC failure, count the fault and resynchronize with CS high for >185 ms.

The Lepton produces segments at about 106 Hz (~26.5 frame opportunities/s), but only one
of every three frame groups is unique and valid. The capture service must consume invalid
groups to maintain synchronization while publishing only complete valid frames.

VSYNC on PC13 is an optimization and timing reference, not the sole integrity signal.
Packet and segment validation remains authoritative.

## Frame representation

Canonical in-memory pixels are unsigned 16-bit big-endian values converted to native
little-endian words during assembly. A frame record contains:

- stream epoch and monotonically increasing MCU frame ID;
- Lepton telemetry frame counter when telemetry is enabled;
- capture start/end timestamps;
- width, height, pixel format, and TLinear scale;
- camera FPA/aux temperature when available;
- dosimeter snapshot nearest the frame timestamp;
- frame-integrity and calibration flags.
