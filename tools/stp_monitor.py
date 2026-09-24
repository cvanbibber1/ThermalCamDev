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
    "ping": 0x01,
    "take-image": 0x02,
    "start-record": 0x03,
    "stop-record": 0x04,
    "stream-on": 0x05,
    "stream-off": 0x06,
    "dosimeter-zero": 0x07,
    "request-keyframe": 0x08,
    "capture-image": 0x30,
    "stream-start": 0x78,
    "stream-stop": 0x79,
    # Camera chain, protocol v1.1. Every camera on the bus shares the Target
    # ID, so these are how one is singled out and driven.
    "select-camera": 0x6D,
    "camera-list": 0x6E,
    "camera-info": 0x61,
    # Hands the bus to one camera. Separate from select-camera, which the
    # visual payload also consumes to switch its own sensor.
    "bus-select-camera": 0x6F,
    "thermal-set-output": 0x74,
    "thermal-set-range": 0x75,
    "thermal-set-emissivity": 0x76,
    "thermal-nuc": 0x7C,
    # The correction, under the name the existing tooling uses for it.
    "run-ffc": 0x7C,
    "thermal-spot": 0x7D,
    "thermal-set-palette": 0x7E,
}

PALETTES = {"white-hot": 0, "black-hot": 1, "ironbow": 2, "rainbow": 3,
            "arctic": 4}
OUTPUT_MODES = {"radiometric": 0, "palette": 1, "both": 2}
RESULTS = {0: "ok", 1: "bad parameter", 2: "wrong camera type",
           3: "camera fault", 4: "not selected", 5: "unknown command",
           6: "unsupported", 7: "hrt stopped"}
CAMERA_NONE = 0xFF

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


def _crc16_table() -> list[int]:
    table = []
    for value in range(256):
        crc = value << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
        table.append(crc)
    return table


_CRC16_TABLE = _crc16_table()


def crc16_ccitt(data: bytes, seed: int) -> int:
    """CRC-16/CCITT-FALSE, one table lookup per byte.

    Every packet is checked, so this is on the hot path: the bitwise form it
    replaces took 746 us per 1282-byte packet against 93 us here. That is 12%
    of a core at 2 Mbaud, spent before any decoding starts, and the host has to
    keep up with the camera or the driver's receive buffer overflows.
    """
    table = _CRC16_TABLE
    crc = seed
    for byte in data:
        crc = ((crc << 8) & 0xFFFF) ^ table[(crc >> 8) ^ byte]
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


def command_arguments(args) -> bytes:
    """Pack the argument block for whichever command was asked for.

    Temperatures are given on the command line in degrees Celsius because that
    is what an operator thinks in, and sent as centi-degrees because that is
    what the protocol carries: 23.50 C becomes 2350, which keeps 0.01 C
    resolution without floating point on the camera.
    """
    name = args.command
    if name in ("select-camera", "bus-select-camera"):
        if args.camera is None:
            raise SystemExit(f"{name} needs --camera")
        return struct.pack("<B", args.camera)
    if name == "camera-list":
        return b"\x00\x01"
    if name == "thermal-set-palette":
        if args.palette is None:
            raise SystemExit("thermal-set-palette needs --palette")
        return struct.pack("<B", PALETTES[args.palette])
    if name == "thermal-set-output":
        if args.output_mode is None:
            raise SystemExit("thermal-set-output needs --output-mode")
        palette = PALETTES.get(args.palette, PALETTES["ironbow"])
        return struct.pack("<BBB", OUTPUT_MODES[args.output_mode], palette, 16)
    if name == "thermal-set-range":
        if args.range is None:
            return struct.pack("<Bhh", 0, 0, 0)      # automatic span
        low, high = (int(round(v * 100)) for v in args.range)
        return struct.pack("<Bhh", 1, low, high)
    if name == "thermal-set-emissivity":
        if args.emissivity is None:
            raise SystemExit("thermal-set-emissivity needs --emissivity")
        return struct.pack("<Hh", int(round(args.emissivity * 1000)),
                           int(round(args.reflected * 100)))
    if name == "thermal-spot":
        if args.spot is None:
            raise SystemExit("thermal-spot needs --spot X Y W H")
        return struct.pack("<HHHH", *args.spot)
    return b""


def build_request(codec: Codec, packet_type: int, target_id: int) -> bytes:
    packet = bytearray(REQUEST_SIZE)
    packet[0:4] = codec.sync_bytes()
    struct.pack_into(codec.order + "I", packet, 4, int(time.time()))
    struct.pack_into(codec.order + "H", packet, 8, 0)
    packet[10] = packet_type
    packet[11] = target_id
    struct.pack_into(codec.order + "H", packet, 12, codec.crc(bytes(packet[4:12])))
    return bytes(packet)


_command_sequence = 0


def build_command(codec: Codec, command_id: int, target_id: int,
                  parameter: int = 0, args: bytes = b"",
                  force: bool = False, sequence: int | None = None,
                  coarse_time: int | None = None) -> bytes:
    """A 120-byte command packet carrying one experiment command.

    `args` follows the seven-byte V3 command header and is little endian.
    """
    packet = bytearray(COMMAND_SIZE)
    packet[0:4] = codec.sync_bytes()
    struct.pack_into(codec.order + "I", packet, 4,
                     int(time.time()) if coarse_time is None else coarse_time)
    struct.pack_into(codec.order + "H", packet, 8, 0)
    packet[10] = TYPE_COMMAND
    packet[11] = target_id
    # Command payload v3, matching the visual payload's encoder so one host
    # drives both. The header is big endian; the arguments are little endian
    # by ICD convention, and the inner CRC covers the header and arguments
    # but not the padding.
    global _command_sequence
    if sequence is None:
        # A fresh sequence per command, so a reply can be matched to the one
        # that caused it. Callers that need reproducible bytes, such as the
        # published command list, pass an explicit value.
        _command_sequence = (_command_sequence + 1) & 0xFFFF
        sequence = _command_sequence
    if not args and parameter:
        args = struct.pack("<H", parameter)
    if len(args) > 98:
        raise ValueError("command arguments exceed 98 bytes")
    head = bytes([command_id]) + struct.pack(">HBB", sequence & 0xFFFF,
                                             len(args), int(force))
    inner = crc16_ccitt(head + args, 0xFFFF)
    body = head + struct.pack(">H", inner) + args
    packet[COMMAND_PAYLOAD_OFFSET:COMMAND_PAYLOAD_OFFSET + len(body)] = body
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
    # Camera chain and thermal state. Centi-degrees Celsius on the wire; the
    # camera holds them as signed 16-bit, so they are unpacked as such.
    def cc(offset):
        raw = codec.u16(payload, offset)
        return (raw - 65536 if raw >= 32768 else raw) / 100.0

    camera = {
        "camera_index": payload[192],
        "camera_active": payload[193],
        "camera_count": payload[194],
        "camera_is_active": bool(payload[195] & 1),
        "camera_is_thermal": bool(payload[195] & 2),
        "camera_selections": codec.u32(payload, 196),
        "camera_select_failures": codec.u32(payload, 200),
        "last_command": payload[204],
        "last_result": RESULTS.get(payload[205], payload[205]),
    }
    thermal = {}
    if payload[195] & 2:
        flags = payload[222]
        thermal = {
            "thermal_spot_mean_c": cc(208),
            "thermal_spot_min_c": cc(210),
            "thermal_spot_max_c": cc(212),
            "thermal_scene_min_c": cc(214),
            "thermal_scene_max_c": cc(216),
            "thermal_emissivity": codec.u16(payload, 218) / 1000.0,
            "thermal_output_mode": payload[220],
            "thermal_palette": payload[221],
            "thermal_nuc_active": bool(flags & 1),
            "thermal_auto_range": bool(flags & 2),
            "thermal_range_valid": bool(flags & 4),
            "thermal_over_range": bool(flags & 8),
            "thermal_spot_valid": bool(flags & 16),
            "thermal_nuc_count": payload[223],
            "thermal_reflected_c": cc(224),
            "thermal_range_low_c": cc(226),
            "thermal_range_high_c": cc(228),
        }
    extension = {}
    layout = codec.u32(payload, 0)
    if layout in (2, 3) and payload[232] == layout:
        stored = codec.u16(payload, 254)
        computed = crc16_ccitt(payload[232:254], 0xFFFF)
        extension = {
            "command_block_valid": stored == computed,
            "node_index": payload[233],
            "node_kind": payload[234],
            "bus_owner": payload[235],
            "sensor_selected": payload[236],
            "hrt_enabled": bool(payload[238]),
            "hrt_credits": codec.u16(payload, 244) if layout == 2 else 0,
            "hrt_go_active": bool(codec.u16(payload, 244)) if layout == 3 else False,
            "command_seq": codec.u16(payload, 246) if payload[239] else None,
            "command_response_length": codec.u16(payload, 252),
            "command_crc_errors": codec.u32(payload, 256),
            "thermal_spot_effective": tuple(codec.u16(payload, offset)
                                             for offset in (260, 262, 264, 266)),
        }
        if stored == computed and payload[239]:
            extension["last_command"] = payload[248]
            extension["last_result"] = RESULTS.get(payload[249], payload[249])
        if payload[268] == 1:
            identity_valid = crc16_ccitt(payload[268:324], 0xFFFF) == codec.u16(payload, 324)
            extension.update({
                "identity_block_valid": identity_valid,
                "build_commit": payload[272:292].hex() if identity_valid and payload[269] & 1 else None,
                "build_source_sha256": payload[292:324].hex() if identity_valid and payload[269] & 1 else None,
                "thermal_health_valid": bool(identity_valid and payload[269] & 2),
                "reset_health_valid": bool(identity_valid and payload[269] & 4),
                "fault_health_valid": bool(identity_valid and payload[269] & 8),
                "cpu_health_valid": bool(identity_valid and payload[270] & 1),
                "storage_health_valid": bool(identity_valid and payload[270] & 2),
                "safe_mode_health_valid": bool(identity_valid and payload[270] & 4),
            })
    return {
        **camera,
        **thermal,
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
        **extension,
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
    # This checkout's flight build uses 921600 8N1.
    parser.add_argument("--baud", type=int, default=921600)
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
                        help="send an experiment command; run bus-select-camera first in a separate invocation")
    parser.add_argument("--camera", type=lambda v: int(v, 0),
                        help="index for bus-select-camera; select-camera uses local sensor 0 or 255")
    parser.add_argument("--palette", choices=sorted(PALETTES),
                        help="palette for thermal-set-palette / thermal-set-output")
    parser.add_argument("--output-mode", choices=sorted(OUTPUT_MODES),
                        help="mode for thermal-set-output")
    parser.add_argument("--range", nargs=2, type=float, metavar=("LOW", "HIGH"),
                        help="manual span in degrees Celsius; omit for automatic")
    parser.add_argument("--emissivity", type=float,
                        help="0 to 1, for thermal-set-emissivity")
    parser.add_argument("--reflected", type=float, default=20.0,
                        help="reflected apparent temperature in C, default 20")
    parser.add_argument("--spot", nargs=4, type=int, metavar=("X", "Y", "W", "H"),
                        help="box in sensor pixels for thermal-spot")
    parser.add_argument("--raw", action="store_true", help="dump every packet header")
    parser.add_argument("--no-recover", action="store_true",
                        help="do not ask for a keyframe after a decode failure, "
                             "so the cost of waiting for the scheduled one can "
                             "be measured")
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
        packet = build_command(codec, COMMANDS[args.command], args.target,
                               args=command_arguments(args))
        port.write(packet)
        port.flush()
        print(f"sent command {args.command} (0x{COMMANDS[args.command]:02X}) "
              f"to target 0x{args.target:02X}")

    assembler = FrameAssembler()
    buffer = bytearray()
    counts = {"ack": 0, "lrt": 0, "hrt": 0, "crc_error": 0, "other_target": 0}
    started = time.time()
    last_report = started
    last_build = None

    print(f"listening on {args.port} at {args.baud} baud, sync {sync.hex(' ')}")
    # The experiment is a slave: in flight it transmits only in reply to a
    # request, so something has to play the flight computer. This does, at a
    # cadence matching the vitals rate DICE is expected to use.
    lrt_request = build_request(codec, 0x81, args.target)
    keyframe_request = build_command(codec, COMMANDS["request-keyframe"], args.target)
    keyframes_asked = 0
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
                    if fields.get("identity_block_valid") and fields.get("build_source_sha256") != last_build:
                        last_build = fields["build_source_sha256"]
                        print(f"BUILD commit={fields['build_commit']} source_sha256={last_build} "
                              f"CPU={'valid' if fields['cpu_health_valid'] else 'unavailable'} "
                              f"storage={'valid' if fields['storage_health_valid'] else 'unavailable'} "
                              f"safe_mode={'valid' if fields['safe_mode_health_valid'] else 'unavailable'}")
                    print(
                        f"LRT  up={fields['uptime_ms'] / 1000:8.1f}s  "
                        f"{fields['lepton_state']:<10} gen={fields['frame_generation']:<7} "
                        f"dose={fields['dose_rad']:+.3f} rad  "
                        f"ffc {fields['ffc_elapsed_ms'] / 1000:5.1f}s  "
                        f"{fields['capture_state']:<12} "
                        + (f"scene {fields['scene_min_c']:.1f}..{fields['scene_max_c']:.1f} C"
                           if "scene_min_c" in fields else "no frame")
                        + (f"  seq={fields['command_seq']} "
                           f"cmd=0x{fields['last_command']:02X} "
                           f"result={fields['last_result']}"
                           if fields.get("command_block_valid") and
                              fields.get("command_seq") is not None else "")
                        + ("  INVALID command block CRC"
                           if fields.get("command_block_valid") is False else "")
                    )
                elif size == HRT_DATA_SIZE:
                    counts["hrt"] += 1
                    payload = packet[6:6 + 1280]
                    if args.raw:
                        print(f"HRT  gen={codec.u32(payload,0)} chunk="
                              f"{codec.u16(payload,4)}/{codec.u16(payload,6)}")
                    frame = assembler.push(codec, payload)
                    # A dropped reference blinds us until the next scheduled
                    # keyframe. Ask for one now instead; the camera makes the
                    # next frame self-contained and the gap becomes a round
                    # trip rather than seconds.
                    if assembler.stream.needs_keyframe and not args.no_recover:
                        assembler.stream.needs_keyframe = False
                        keyframes_asked += 1
                        try:
                            port.write(keyframe_request)
                            port.flush()
                        except Exception:  # noqa: BLE001 - the read reports it
                            pass
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
                 if assembler.undecodable else "")
              + (f", {keyframes_asked} keyframes requested" if keyframes_asked else ""))
    if counts["lrt"] == 0 and counts["hrt"] == 0:
        print("Nothing decoded. Check wiring and baud, then try --little-endian "
              "or a different --crc-seed; neither is fixed by the specification.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
