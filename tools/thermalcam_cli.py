#!/usr/bin/env python3
"""ThermalCamDev CDC/RS-485 diagnostic and camera-control client."""

from __future__ import annotations

import argparse
import struct
import sys
import time
from pathlib import Path

import serial

MAGIC = 0x4354
VERSION = 1
KIND_REQUEST = 1
FLAG_ERROR = 1
HOST_ADDRESS = 0xF0
MAX_PAYLOAD = 2048

OPCODES = {
    "info": 0x0001,
    "health": 0x0002,
    "discover": 0x0003,
    "lepton-status": 0x0100,
    "cci-get": 0x0101,
    "cci-set": 0x0102,
    "ffc": 0x0103,
    "ffc-status": 0x0107,
    "cci-run": 0x0104,
    "reg-read": 0x0105,
    "reg-write": 0x0106,
    "stream-status": 0x0200,
    "frame": 0x0201,
    "dosimeter": 0x0300,
    "dosimeter-zero": 0x0301,
    "dosimeter-set-zero": 0x0302,
    "bus-status": 0x0400,
    "assign": 0x0401,
}


# Field order must match health_counters_t in include/health.h.
HEALTH_FIELDS = (
    "reset_cause",
    "fatal_code",
    "clock_failures",
    "camera_boot_failures",
    "cci_errors",
    "ffc_forced_runs",
    "vospi_discard_packets",
    "vospi_crc_errors",
    "vospi_sequence_errors",
    "vospi_resyncs",
    "vospi_start_retries",
    "vospi_link_stalls",
    "vospi_start_failures",
    "vospi_spi_errors",
    "vospi_chunks",
    "vospi_segments",
    "vospi_segments_ignored",
    "frames_complete",
    "frames_dropped",
    "usb_rx_overruns",
    "usb_tx_busy",
    "adc_overruns",
    "rs485_rx_overruns",
    "rs485_crc_errors",
    "rs485_tx_busy",
)


def decode_health(data: bytes) -> dict[str, int]:
    count = min(len(data) // 4, len(HEALTH_FIELDS))
    values = struct.unpack("<" + "I" * count, data[: count * 4])
    return dict(zip(HEALTH_FIELDS, values))


DOSIMETER_FLAGS = {
    0x01: "nominal-calibration",
    0x02: "saturated",
    0x04: "stale",
    0x08: "zeroing",
}

# Dosimeter transfer function at PA4, in volts:
#     DOSI = 0.1575 + 0.0025 * D_rad
INTERCEPT_UV = 157500
UV_PER_RAD = 2500


def rad_from_microvolts(microvolts: float, intercept_uv: float = INTERCEPT_UV) -> float:
    return (microvolts - intercept_uv) / UV_PER_RAD


def describe_dosimeter_flags(flags: int) -> str:
    names = [name for bit, name in DOSIMETER_FLAGS.items() if flags & bit]
    return ",".join(names) if names else "none"


def decode_dosimeter(body: bytes) -> dict:
    timestamp, = struct.unpack("<I", body[:4])
    mean, minimum, maximum, stddev = struct.unpack("<HHHH", body[4:12])
    vdda, voltage, filtered = struct.unpack("<III", body[12:24])
    zero, dose_microrad, flags = struct.unpack("<iiI", body[24:36])
    settings_status, save_count = struct.unpack("<iI", body[36:44])
    return {
        "timestamp_ms": timestamp,
        "raw_mean": mean,
        "raw_min": minimum,
        "raw_max": maximum,
        "raw_stddev": stddev,
        "vdda_mv": vdda,
        "voltage_uv": voltage,
        "filtered_voltage_uv": filtered,
        "zero_uv": zero,
        "dose_microrad": dose_microrad,
        "rad": dose_microrad / 1e6,
        "flags": flags,
        "settings_status": settings_status,
        "save_count": save_count,
    }


def crc32c(data: bytes) -> int:
    crc = 0xFFFFFFFF
    for value in data:
        crc ^= value
        for _ in range(8):
            crc = (crc >> 1) ^ (0x82F63B78 if crc & 1 else 0)
    return (~crc) & 0xFFFFFFFF


def cobs_encode(data: bytes) -> bytes:
    output = bytearray(b"\x00")
    code_index = 0
    code = 1
    for value in data:
        if value == 0:
            output[code_index] = code
            code_index = len(output)
            output.append(0)
            code = 1
        else:
            output.append(value)
            code += 1
            if code == 0xFF:
                output[code_index] = code
                code_index = len(output)
                output.append(0)
                code = 1
    output[code_index] = code
    return bytes(output)


def cobs_decode(data: bytes) -> bytes:
    output = bytearray()
    index = 0
    while index < len(data):
        code = data[index]
        if code == 0:
            raise ValueError("zero byte inside COBS frame")
        index += 1
        end = index + code - 1
        if end > len(data):
            raise ValueError("truncated COBS frame")
        output.extend(data[index:end])
        index = end
        if code != 0xFF and index < len(data):
            output.append(0)
    return bytes(output)


def encode_request(destination: int, sequence: int, opcode: int, payload: bytes) -> bytes:
    if len(payload) > MAX_PAYLOAD:
        raise ValueError("payload too large")
    header = struct.pack(
        "<HBBBBBBIHH",
        MAGIC,
        VERSION,
        KIND_REQUEST,
        0,
        HOST_ADDRESS,
        destination,
        0,
        sequence,
        opcode,
        len(payload),
    )
    raw = header + payload
    raw += struct.pack("<I", crc32c(raw))
    return cobs_encode(raw) + b"\x00"


def decode_message(encoded: bytes) -> dict:
    raw = cobs_decode(encoded)
    if len(raw) < 20:
        raise ValueError("short response")
    header = struct.unpack("<HBBBBBBIHH", raw[:16])
    magic, version, kind, flags, source, destination, _, sequence, opcode, length = header
    if magic != MAGIC or version != VERSION or len(raw) != 16 + length + 4:
        raise ValueError("invalid response header or length")
    if crc32c(raw[:-4]) != struct.unpack("<I", raw[-4:])[0]:
        raise ValueError("response CRC-32C mismatch")
    return {
        "kind": kind,
        "flags": flags,
        "source": source,
        "destination": destination,
        "sequence": sequence,
        "opcode": opcode,
        "payload": raw[16:-4],
    }


class CameraLink:
    def __init__(self, port: str, baud: int, destination: int, timeout: float):
        self.serial = serial.Serial(port, baudrate=baud, timeout=timeout)
        self.destination = destination
        self.sequence = int(time.time() * 1000) & 0xFFFFFFFF

    def close(self) -> None:
        self.serial.close()

    def request(self, opcode: int, payload: bytes = b"") -> bytes:
        self.sequence = (self.sequence + 1) & 0xFFFFFFFF
        self.serial.reset_input_buffer()
        self.serial.write(encode_request(self.destination, self.sequence, opcode, payload))
        encoded = self.serial.read_until(b"\x00")
        if not encoded.endswith(b"\x00"):
            raise TimeoutError("camera response timed out")
        response = decode_message(encoded[:-1])
        if response["sequence"] != self.sequence or response["opcode"] != opcode:
            raise ValueError("response does not match request")
        body = response["payload"]
        if len(body) < 2:
            raise ValueError("response has no result code")
        result = struct.unpack("<h", body[:2])[0]
        if response["flags"] & FLAG_ERROR or result != 0:
            raise RuntimeError(f"camera returned error {result}")
        return body[2:]


def print_words(label: str, data: bytes) -> None:
    values = struct.unpack("<" + "I" * (len(data) // 4), data[: len(data) // 4 * 4])
    for index, value in enumerate(values):
        print(f"{label}[{index}]={value}")


def save_frame(link: CameraLink, output: Path) -> None:
    generation = 0
    offset = 0
    frame = bytearray()
    total = 38400
    while offset < total:
        request = struct.pack("<IIH", generation, offset, 1900)
        body = link.request(OPCODES["frame"], request)
        response_generation, total, response_offset = struct.unpack("<III", body[:12])
        if response_offset != offset:
            raise ValueError("out-of-order frame chunk")
        if generation == 0:
            generation = response_generation
        elif generation != response_generation:
            raise RuntimeError("frame was replaced during transfer")
        frame.extend(body[12:])
        offset += len(body) - 12
    output.write_bytes(frame[:total])
    print(f"saved generation {generation}, {total} bytes to {output}")


def run(args: argparse.Namespace) -> None:
    destination = 0xFF if args.command in {"discover", "assign"} else args.address
    link = CameraLink(args.port, args.baud, destination, args.timeout)
    try:
        opcode = OPCODES[args.command]
        if args.command == "cci-get":
            body = link.request(opcode, struct.pack("<HH", args.command_id, args.words))
            count = struct.unpack("<H", body[:2])[0]
            values = struct.unpack("<" + "H" * count, body[2 : 2 + count * 2])
            print(" ".join(f"0x{value:04X}" for value in values))
        elif args.command == "cci-set":
            values = [int(value, 0) for value in args.values]
            payload = struct.pack("<HH", args.command_id, len(values))
            payload += struct.pack("<" + "H" * len(values), *values)
            link.request(opcode, payload)
            print("ok")
        elif args.command == "cci-run":
            link.request(opcode, struct.pack("<H", args.command_id))
            print("ok")
        elif args.command == "reg-read":
            body = link.request(opcode, struct.pack("<H", args.register))
            print(f"0x{struct.unpack('<H', body)[0]:04X}")
        elif args.command == "reg-write":
            link.request(opcode, struct.pack("<HH", args.register, args.value))
            print("ok")
        elif args.command == "dosimeter-set-zero":
            link.request(opcode, struct.pack("<i", args.microvolts))
            print("ok")
        elif args.command == "assign":
            uid = bytes.fromhex(args.uid)
            if len(uid) != 12:
                raise ValueError("UID must contain exactly 12 bytes (24 hex digits)")
            link.request(opcode, uid + bytes([args.new_address]))
            print(f"assigned runtime address {args.new_address}")
        elif args.command == "frame":
            save_frame(link, args.output)
        else:
            body = link.request(opcode)
            if args.command in {"info", "discover"}:
                version = ".".join(str(value) for value in body[:3])
                uid = body[4:16].hex().upper()
                capabilities = struct.unpack("<I", body[16:20])[0]
                print(f"firmware={version} uid={uid} capabilities=0x{capabilities:08X}")
            elif args.command == "health":
                for name, value in decode_health(body).items():
                    print(f"{name}={value}")
            elif args.command == "bus-status":
                baud, target, hrt = struct.unpack("<IBB", body[:6])
                (commands, lrt_req, lrt_sent, hrt_sent, other,
                 crc_err, type_err, coarse) = struct.unpack("<IIIIIIII", body[6:38])
                print(f"baud={baud} target_id={target} hrt={'on' if hrt else 'off'} coarse_time={coarse}")
                print(f"rx: commands={commands} lrt_requests={lrt_req} other_target={other} "
                      f"crc_errors={crc_err} type_errors={type_err}")
                print(f"tx: lrt={lrt_sent} hrt={hrt_sent}")
            elif args.command == "lepton-status":
                state, _, cci, ffc, _, generation, vsync = struct.unpack("<BBhhHII", body[:16])
                states = {
                    0: "power-off", 1: "reset-hold", 2: "booting", 3: "configuring",
                    4: "wait-vsync", 5: "streaming", 6: "resync", 7: "retry",
                }
                print(
                    f"state={states.get(state, state)} cci_result={cci} ffc_result={ffc} "
                    f"generation={generation} last_vsync={vsync}ms"
                )
            elif args.command == "ffc-status":
                mode, lockout, elapsed, period, delta, state = struct.unpack("<IIIIIi", body[:24])
                modes = {0: "manual", 1: "auto", 2: "external"}
                print(f"shutter_mode={modes.get(mode, mode)} lockout={lockout} ffc_state={state}")
                print(
                    f"elapsed={elapsed / 1000:.1f}s period={period / 1000:.0f}s "
                    f"temp_delta={delta / 100:.2f}C"
                )
            elif args.command == "dosimeter":
                sample = decode_dosimeter(body)
                print(
                    f"t={sample['timestamp_ms']}ms adc={sample['raw_mean']} "
                    f"min={sample['raw_min']} max={sample['raw_max']} sd={sample['raw_stddev']}"
                )
                print(
                    f"vdda={sample['vdda_mv']}mV voltage={sample['voltage_uv']}uV "
                    f"filtered={sample['filtered_voltage_uv']}uV zero={sample['zero_uv']}uV"
                )
                print(
                    f"dose={sample['rad']:.4f} rad  flags={describe_dosimeter_flags(sample['flags'])}  "
                    f"settings={sample['settings_status']} saves={sample['save_count']}"
                )
            elif args.command == "dosimeter-zero":
                samples, = struct.unpack("<I", body[:4])
                print(f"zero capture started, averaging {samples} samples (about {samples / 15.6:.1f}s)")
            elif args.command == "dosimeter-set-zero":
                print("ok")
            else:
                print_words(args.command, body)
    finally:
        link.close()


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True, help="CDC or RS-485 serial port, for example COM12")
    parser.add_argument("--baud", type=int, default=921_600, help="ignored by USB CDC hosts; field-bus default is 921600")
    parser.add_argument("--address", type=lambda value: int(value, 0), default=1)
    parser.add_argument("--timeout", type=float, default=2.0)
    subparsers = parser.add_subparsers(dest="command", required=True)
    for command in ("info", "health", "discover", "lepton-status", "ffc", "ffc-status", "stream-status", "dosimeter", "dosimeter-zero", "bus-status"):
        subparsers.add_parser(command)
    get_parser = subparsers.add_parser("cci-get")
    get_parser.add_argument("command_id", type=lambda value: int(value, 0))
    get_parser.add_argument("words", type=int)
    set_parser = subparsers.add_parser("cci-set")
    set_parser.add_argument("command_id", type=lambda value: int(value, 0))
    set_parser.add_argument("values", nargs="+")
    run_parser = subparsers.add_parser("cci-run")
    run_parser.add_argument("command_id", type=lambda value: int(value, 0))
    read_parser = subparsers.add_parser("reg-read")
    read_parser.add_argument("register", type=lambda value: int(value, 0))
    write_parser = subparsers.add_parser("reg-write")
    write_parser.add_argument("register", type=lambda value: int(value, 0))
    write_parser.add_argument("value", type=lambda value: int(value, 0))
    set_zero_parser = subparsers.add_parser("dosimeter-set-zero")
    set_zero_parser.add_argument("microvolts", type=int, help="zero reference in uV")
    assign_parser = subparsers.add_parser("assign")
    assign_parser.add_argument("uid", help="12-byte MCU UID as 24 hex digits")
    assign_parser.add_argument("new_address", type=lambda value: int(value, 0))
    frame_parser = subparsers.add_parser("frame")
    frame_parser.add_argument("--output", type=Path, default=Path("frame-y16.raw"))
    return parser.parse_args()


if __name__ == "__main__":
    try:
        run(parse_args())
    except (OSError, ValueError, RuntimeError, TimeoutError) as error:
        print(f"error: {error}", file=sys.stderr)
        raise SystemExit(1)
