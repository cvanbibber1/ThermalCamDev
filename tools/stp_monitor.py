#!/usr/bin/env python3
"""Receive and decode STP/DICE RS-422 traffic from the camera.

Point this at the serial port of an RS-422 to USB converter wired to the
camera's transceiver. It syncs on the packet marker, checks CRCs, prints
housekeeping, and reassembles thermal frames from the high-rate stream.

    python tools/stp_monitor.py --port COM7
    python tools/stp_monitor.py --port COM7 --save-frames captures/rs422
    python tools/stp_monitor.py --port COM7 --request lrt

The parameters the interface control document does not define -- wire byte
order, CRC seed and Target ID -- are exposed as options so a mismatch can be
found without editing firmware.
"""

from __future__ import annotations

import argparse
import struct
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import frame_codec

try:
    import serial
except ImportError:  # pragma: no cover
    sys.exit("pyserial is required: pip install pyserial")

SYNC_WORD = 0x1ACFFC1D

TYPE_COMMAND = 0x10
TYPE_LRT = 0x81
TYPE_HRT_STOP = 0x85
TYPE_HRT_STOP_WITH_LOSS = 0x86
TYPE_HRT_GO = 0x87

ACK_SIZE = 8
LRT_DATA_SIZE = 1256
HRT_DATA_SIZE = 1288
REQUEST_SIZE = 14
COMMAND_SIZE = 120
COMMAND_PAYLOAD_OFFSET = 12

# Experiment command ids, carried in the command payload. See
# include/protocol/stp_protocol.h; the specification does not define this.
COMMANDS = {
    "run-ffc": 0x01,
    "take-image": 0x02,
    "start-record": 0x03,
    "stop-record": 0x04,
    "stream-on": 0x05,
    "stream-off": 0x06,
    "dosimeter-zero": 0x07,
}

CAPTURE_STATES = {0: "idle", 1: "correcting", 2: "single image", 3: "recording"}
SHUTTER_MODES = {0: "manual", 1: "auto", 2: "external"}

FRAME_BYTES = 160 * 120 * 2
HRT_HEADER = 16
HRT_CHUNK_BYTES = 1280 - HRT_HEADER
# A compressed frame is far smaller, but an uncompressed one still has to fit.
MAX_ENCODED_BYTES = FRAME_BYTES

LEPTON_STATES = {
    0: "power-off", 1: "reset-hold", 2: "booting", 3: "configuring",
    4: "wait-vsync", 5: "streaming", 6: "resync", 7: "retry",
}


def crc16_ccitt(data: bytes, seed: int) -> int:
    crc = seed
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


class Codec:
    """Byte-order dependent helpers, kept in one place like the firmware."""

    def __init__(self, big_endian: bool, crc_seed: int) -> None:
        self.order = ">" if big_endian else "<"
        self.crc_seed = crc_seed

    def u16(self, data: bytes, offset: int) -> int:
        return struct.unpack_from(self.order + "H", data, offset)[0]

    def u32(self, data: bytes, offset: int) -> int:
        return struct.unpack_from(self.order + "I", data, offset)[0]

    def i32(self, data: bytes, offset: int) -> int:
        return struct.unpack_from(self.order + "i", data, offset)[0]

    def sync_bytes(self) -> bytes:
        return struct.pack(self.order + "I", SYNC_WORD)

    def crc(self, data: bytes) -> int:
        return crc16_ccitt(data, self.crc_seed)


def transmitted_size(packet_type: int) -> int | None:
    """Experiment-to-DICE packet sizes, which is all this tool receives."""
    return {TYPE_COMMAND: ACK_SIZE, TYPE_LRT: LRT_DATA_SIZE,
            TYPE_HRT_GO: HRT_DATA_SIZE}.get(packet_type)


def build_request(codec: Codec, packet_type: int, target_id: int) -> bytes:
    packet = bytearray(REQUEST_SIZE)
    packet[0:4] = codec.sync_bytes()
    struct.pack_into(codec.order + "I", packet, 4, int(time.time()))
    struct.pack_into(codec.order + "H", packet, 8, 0)
    packet[10] = packet_type
    packet[11] = target_id
    struct.pack_into(codec.order + "H", packet, 12, codec.crc(bytes(packet[4:12])))
    return bytes(packet)


def build_command(codec: Codec, command_id: int, target_id: int,
                  parameter: int = 0) -> bytes:
    """A 120-byte command packet carrying one experiment command."""
    packet = bytearray(COMMAND_SIZE)
    packet[0:4] = codec.sync_bytes()
    struct.pack_into(codec.order + "I", packet, 4, int(time.time()))
    struct.pack_into(codec.order + "H", packet, 8, 0)
    packet[10] = TYPE_COMMAND
    packet[11] = target_id
    packet[COMMAND_PAYLOAD_OFFSET] = command_id
    packet[COMMAND_PAYLOAD_OFFSET + 1] = 0
    struct.pack_into(codec.order + "H", packet, COMMAND_PAYLOAD_OFFSET + 2, parameter)
    struct.pack_into(codec.order + "H", packet, COMMAND_SIZE - 2,
                     codec.crc(bytes(packet[4:COMMAND_SIZE - 2])))
    return bytes(packet)


def decode_lrt(codec: Codec, payload: bytes) -> dict:
    """Experiment-defined housekeeping layout; see include/stp_link.h."""
    scene = {}
    if codec.u16(payload, 48) or codec.u16(payload, 50):
        scene = {
            "scene_min_c": codec.u16(payload, 48) / 100.0 - 273.15,
            "scene_max_c": codec.u16(payload, 50) / 100.0 - 273.15,
            "scene_centre_c": codec.u16(payload, 52) / 100.0 - 273.15,
        }
    return {
        "layout": codec.u32(payload, 0),
        "uptime_ms": codec.u32(payload, 4),
        "coarse_time": codec.u32(payload, 8),
        "lepton_state": LEPTON_STATES.get(payload[14], payload[14]),
        "settings_status": payload[15],
        "frame_generation": codec.u32(payload, 16),
        "dose_rad": codec.i32(payload, 24) / 1e6,
        "dosimeter_uv": codec.u32(payload, 28),
        "dosimeter_zero_uv": codec.i32(payload, 32),
        "dosimeter_flags": codec.u32(payload, 36),
        "vdda_mv": codec.u32(payload, 40),
        "ffc_elapsed_ms": codec.u32(payload, 56),
        "shutter_mode": SHUTTER_MODES.get(payload[60], payload[60]),
        "capture_state": CAPTURE_STATES.get(payload[61], payload[61]),
        "images_sent": codec.u16(payload, 62),
        **scene,
    }


class FrameAssembler:
    """Collects HRT chunks into whole frames, discarding mixed generations.

    Images may arrive compressed. The header byte at offset 14 says which
    layout is in use: version 1 was always raw pixels, version 2 adds a codec
    mode at offset 15 and a frame is then a variable number of chunks. Both are
    accepted so captures taken before compression still decode.
    """

    def __init__(self) -> None:
        self.generation = None
        self.buffer = bytearray(MAX_ENCODED_BYTES)
        self.seen: set[int] = set()
        self.expected = 0
        self.encoded_bytes = 0
        self.have_final = False
        self.mode = frame_codec.MODE_RAW
        self.completed = 0
        self.discarded = 0
        self.undecodable = 0
        self.compressed_bytes = 0
        self.stream = frame_codec.Stream()

    def complete(self) -> bool:
        """Every chunk up to the flagged last one has arrived."""
        return self.expected > 0 and len(self.seen) >= self.expected

    def push(self, codec: Codec, payload: bytes) -> bytes | None:
        generation = codec.u32(payload, 0)
        index = codec.u16(payload, 4)
        total = codec.u16(payload, 6)
        offset = codec.u32(payload, 8)
        length = codec.u16(payload, 12)
        version = payload[14]
        raw_mode = payload[15] if version >= 2 else frame_codec.MODE_RAW
        mode = raw_mode & frame_codec.MODE_MASK
        final = bool(raw_mode & frame_codec.MODE_FINAL)
        if offset + length > MAX_ENCODED_BYTES:
            return None
        if total == 0 and not (version >= 2):
            return None

        if generation != self.generation:
            if self.generation is not None and not self.complete():
                self.discarded += 1
                # A frame that never completed leaves the decoder without the
                # reference the next difference frame needs.
                self.stream.previous = None
            self.generation = generation
            self.seen.clear()
            self.expected = total
            self.encoded_bytes = 0
            self.mode = mode

        self.buffer[offset:offset + length] = payload[HRT_HEADER:HRT_HEADER + length]
        self.seen.add(index)
        self.encoded_bytes = max(self.encoded_bytes, offset + length)
        if final:
            # The flagged chunk is the last, so now the count is known.
            self.expected = index + 1
        if not self.complete():
            return None

        encoded = bytes(self.buffer[:self.encoded_bytes])
        self.seen.clear()
        self.generation = None
        frame = self.stream.push(encoded, self.mode)
        if frame is None:
            self.undecodable += 1
            return None
        self.completed += 1
        self.compressed_bytes += self.encoded_bytes
        return frame.astype("<u2").tobytes()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True, help="RS-422 converter serial port")
    # Test branch: the firmware here runs at 2 Mbaud, not the 921600 of flight.
    parser.add_argument("--baud", type=int, default=2000000)
    parser.add_argument("--target", type=lambda v: int(v, 0), default=0xC7,
                        help="Target ID of the camera. Default 0xC7")
    parser.add_argument("--seconds", type=float, default=0.0, help="0 runs until interrupted")
    # Byte order and CRC parameters are confirmed; these remain so a mismatch
    # can still be diagnosed from the ground without rebuilding firmware.
    parser.add_argument("--little-endian", action="store_true",
                        help="diagnostic only; the link is big endian")
    parser.add_argument("--crc-seed", type=lambda v: int(v, 0), default=0xFFFF,
                        help="diagnostic only; CRC-16/CCITT-FALSE seeds 0xFFFF")
    parser.add_argument("--save-frames", type=Path, help="write reassembled frames here")
    parser.add_argument("--request",
                        choices=["lrt", "hrt-go", "hrt-stop", "hrt-stop-loss", "command"],
                        help="send a DICE request before listening")
    parser.add_argument("--poll", type=float, default=1.0, metavar="SECONDS",
                        help="ask for vitals this often, standing in for the "
                             "flight computer. Flight firmware only speaks when "
                             "spoken to, so without this nothing arrives. "
                             "0 disables it")
    parser.add_argument("--repeat-request", type=int, default=1,
                        help="send the request this many times, to prove it is honoured")
    parser.add_argument("--command", choices=sorted(COMMANDS),
                        help="send an experiment command before listening")
    parser.add_argument("--raw", action="store_true", help="dump every packet header")
    args = parser.parse_args()

    codec = Codec(not args.little_endian, args.crc_seed)
    sync = codec.sync_bytes()
    if args.save_frames:
        args.save_frames.mkdir(parents=True, exist_ok=True)

    port = serial.Serial(args.port, args.baud, timeout=0.2)
    # Decoding a compressed frame takes tens of milliseconds, and nothing is
    # read from the port while it happens. The driver's default buffer is small
    # enough that the gap loses bytes, which shows up as CRC errors and partial
    # frames that look like a bad link. Ask for a megabyte instead.
    try:
        port.set_buffer_size(rx_size=1 << 20, tx_size=1 << 16)
    except (AttributeError, NotImplementedError):
        pass  # Not a Windows driver; the default is usually generous enough.
    if args.request:
        types = {"lrt": TYPE_LRT, "hrt-go": TYPE_HRT_GO,
                 "hrt-stop": TYPE_HRT_STOP,
                 "hrt-stop-loss": TYPE_HRT_STOP_WITH_LOSS,
                 "command": TYPE_COMMAND}
        request = build_request(codec, types[args.request], args.target)
        for _ in range(max(1, args.repeat_request)):
            port.write(request)
            port.flush()
            time.sleep(0.05)
        print(f"sent {args.request} x{max(1, args.repeat_request)} to target "
              f"0x{args.target:02X}: {request.hex(' ')}")

    if args.command:
        packet = build_command(codec, COMMANDS[args.command], args.target)
        port.write(packet)
        port.flush()
        print(f"sent command {args.command} (0x{COMMANDS[args.command]:02X}) "
              f"to target 0x{args.target:02X}")

    assembler = FrameAssembler()
    buffer = bytearray()
    counts = {"ack": 0, "lrt": 0, "hrt": 0, "crc_error": 0, "other_target": 0}
    started = time.time()
    last_report = started

    print(f"listening on {args.port} at {args.baud} baud, sync {sync.hex(' ')}")
    # The experiment is a slave: in flight it transmits only in reply to a
    # request, so something has to play the flight computer. This does, at a
    # cadence matching the vitals rate DICE is expected to use.
    lrt_request = build_request(codec, 0x81, args.target)
    last_poll = 0.0
    try:
        while args.seconds <= 0.0 or (time.time() - started) < args.seconds:
            if args.poll > 0.0 and (time.time() - last_poll) >= args.poll:
                last_poll = time.time()
                try:
                    port.write(lrt_request)
                    port.flush()
                except Exception:  # noqa: BLE001 - reported by the read below
                    pass
            chunk = port.read(4096)
            if chunk:
                buffer.extend(chunk)

            while True:
                start = buffer.find(sync)
                if start < 0:
                    # Keep a partial sync across reads.
                    del buffer[:max(0, len(buffer) - 3)]
                    break
                if start:
                    del buffer[:start]
                if len(buffer) < 6:
                    break
                size = transmitted_size(buffer[4])
                if size is None:
                    del buffer[:4]
                    continue
                if len(buffer) < size:
                    break

                packet = bytes(buffer[:size])
                del buffer[:size]
                stored = codec.u16(packet, size - 2)
                if stored != codec.crc(packet[4:size - 2]):
                    counts["crc_error"] += 1
                    continue
                if packet[5] != args.target:
                    counts["other_target"] += 1
                    continue

                if size == ACK_SIZE:
                    counts["ack"] += 1
                    print("ACK")
                elif size == LRT_DATA_SIZE:
                    counts["lrt"] += 1
                    fields = decode_lrt(codec, packet[6:6 + 1248])
                    print(
                        f"LRT  up={fields['uptime_ms'] / 1000:8.1f}s  "
                        f"{fields['lepton_state']:<10} gen={fields['frame_generation']:<7} "
                        f"dose={fields['dose_rad']:+.3f} rad  "
                        f"ffc {fields['ffc_elapsed_ms'] / 1000:5.1f}s  "
                        f"{fields['capture_state']:<12} "
                        + (f"scene {fields['scene_min_c']:.1f}..{fields['scene_max_c']:.1f} C"
                           if "scene_min_c" in fields else "no frame")
                    )
                elif size == HRT_DATA_SIZE:
                    counts["hrt"] += 1
                    payload = packet[6:6 + 1280]
                    if args.raw:
                        print(f"HRT  gen={codec.u32(payload,0)} chunk="
                              f"{codec.u16(payload,4)}/{codec.u16(payload,6)}")
                    frame = assembler.push(codec, payload)
                    if frame is not None and args.save_frames:
                        name = args.save_frames / f"frame-{assembler.completed:05d}.raw"
                        name.write_bytes(frame)

            now = time.time()
            if now - last_report >= 5.0:
                last_report = now
                print(f"  [{counts['lrt']} LRT, {counts['hrt']} HRT, "
                      f"{assembler.completed} frames, {counts['crc_error']} CRC errors, "
                      f"{counts['other_target']} other-target]")
    except KeyboardInterrupt:
        pass
    finally:
        port.close()

    elapsed = max(time.time() - started, 1e-6)
    print(f"\n{elapsed:.1f}s: {counts['lrt']} LRT, {counts['hrt']} HRT packets, "
          f"{assembler.completed} frames reassembled, {assembler.discarded} partial, "
          f"{counts['crc_error']} CRC errors, {counts['other_target']} for other targets")
    if assembler.completed:
        mean = assembler.compressed_bytes / assembler.completed
        print(f"{assembler.completed / elapsed:.2f} frames/s, "
              f"{mean:.0f} bytes/frame encoded, {FRAME_BYTES / mean:.2f}x compression"
              + (f", {assembler.undecodable} undecodable "
                 f"({assembler.stream.checksum_failed} corrupt, "
                 f"{assembler.stream.no_reference} without a reference)"
                 if assembler.undecodable else ""))
    if counts["lrt"] == 0 and counts["hrt"] == 0:
        print("Nothing decoded. Check wiring and baud, then try --little-endian "
              "or a different --crc-seed; neither is fixed by the specification.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
