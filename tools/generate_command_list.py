#!/usr/bin/env python3
"""Generate COMMANDS.md from the live protocol definitions.

The hex strings are produced by the same code that talks to the camera, so the
list cannot drift from the implementation. Re-run after changing an opcode or
the Target ID.
"""

from __future__ import annotations

import importlib.util
import struct
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent


def load(name: str):
    spec = importlib.util.spec_from_file_location(name, str(HERE / f"{name}.py"))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


cli = load("thermalcam_cli")
stp = load("stp_monitor")

TARGET = 0xC7
codec = stp.Codec(True, 0xFFFF)

# Descriptions live here rather than in the tools, which only need the codes.
CAMERA_COMMANDS = {
    "info": "Firmware version, unique chip ID and capability flags",
    "health": "All error and activity counters since power on",
    "discover": "Broadcast form of info, so an unknown camera answers",
    "lepton-status": "Imaging sensor state, frame count and start-up results",
    "cci-get": "Read a sensor setting (needs command id and word count)",
    "cci-set": "Write a sensor setting (needs command id and values)",
    "ffc": "Run a flat-field correction now",
    "cci-run": "Run a sensor operation that carries no data",
    "reg-read": "Read one sensor hardware register",
    "reg-write": "Write one sensor hardware register",
    "ffc-status": "Shutter policy, time since last correction, trigger thresholds",
    "stream-status": "Current image number and its size in bytes",
    "frame": "Download the current image as raw data, in chunks",
    "dosimeter": "Full dosimeter reading: voltages, intercept, dose and flags",
    "dosimeter-zero": "Measure and store this unit's zero intercept",
    "dosimeter-set-zero": "Set the intercept directly; 0 restores the nominal value",
    "bus-status": "RS-422 link speed, Target ID, stream state and packet counters",
    "assign": "Assign a Target ID, matched by the camera's unique chip ID",
}

# DICE -> Experiment packets. Time fields are zero so each string is constant.
DICE_REQUESTS = [
    ("LRT_REQUEST", stp.TYPE_LRT, "Ask for vitals; the camera replies with an LRT data packet"),
    ("HRT_GO", stp.TYPE_HRT_GO, "Open the selected node's HRT tap until STOP"),
    ("HRT_STOP", stp.TYPE_HRT_STOP, "Stop capture and close the HRT tap"),
    ("HRT_STOP_WITH_LOSS", stp.TYPE_HRT_STOP_WITH_LOSS, "Stop capture, loss expected"),
]


EXPERIMENT_COMMANDS = {
    "PING": "Payload liveness. Acknowledged and nothing else; the correction is THERMAL_NUC",
    "TAKE_IMAGE": "Correct, then send exactly one complete frame and stop. Use this when the link is too slow to stream",
    "START_RECORD": "Correct, then stream continuously until stopped",
    "STOP_RECORD": "Stop streaming",
    "STREAM_ON": "Stream without correcting first, when the image is already settled",
    "STREAM_OFF": "Stop streaming",
    "DOSIMETER_ZERO": "Measure and store this unit's dosimeter zero",
    "REQUEST_KEYFRAME": "Make the next image self-contained. Send this when a frame fails to decode, rather than waiting for the scheduled keyframe",
}

# V3 command header carries sequence, length and inner CRC. Arguments are
# little endian. Example values below are syntactically valid for each opcode.
CHAIN_COMMANDS = {
    "SELECT_CAMERA": ("Select the fixed local Lepton sensor (0) or disable it (0xFF); does not grant bus ownership. On the current visual node, only send while it owns the bus", 0x6D),
    "CAMERA_LIST": ("Node identity is in LRT; a shared bus table is unsupported", 0x6E),
    "CAMERA_INFO": ("Report this camera's index, kind and selection state", 0x61),
    "BUS_SELECT_CAMERA": ("Hand the bus to one camera; 0xFF to nobody. Separate from SELECT_CAMERA, which the visual payload also consumes to switch its own sensor", 0x6F),
    "THERMAL_SET_OUTPUT": ("mode u8, palette u8, depth u8. Only mode 0, depth 16 emits HRT; palette modes return UNSUPPORTED", 0x74),
    "THERMAL_SET_RANGE": ("mode u8, low i16, high i16 in centi-degrees C. Mode 0 automatic, 1 manual", 0x75),
    "THERMAL_SET_EMISSIVITY": ("emissivity u16 in thousandths, reflected i16 in centi-degrees C", 0x76),
    "THERMAL_NUC": ("Run a non-uniformity correction now; no image for about a second", 0x7C),
    "THERMAL_SPOT": ("x u16, y u16, w u16, h u16. Returns min, max and mean over the box in telemetry", 0x7D),
    "THERMAL_SET_PALETTE": ("palette u8. 0 white-hot, 1 black-hot, 2 ironbow, 3 rainbow, 4 arctic", 0x7E),
    "CAPTURE_IMAGE": ("Ephemeral corrected thermal HRT frame; no slot is created", 0x30),
    "STREAM_START": ("Start live thermal HRT stream", 0x78),
    "STREAM_STOP": ("Stop live thermal HRT stream", 0x79),
    "SLOT_CAPTURE_IMAGE": ("Unsupported on thermal: no persistent storage", 0x66),
}

EXAMPLE_ARGS = {
    "SELECT_CAMERA": b"\x00", "BUS_SELECT_CAMERA": b"\x00",
    "CAMERA_LIST": b"\x00\x01", "THERMAL_SET_OUTPUT": b"\x00\x02\x10",
    "THERMAL_SET_RANGE": b"\x00\x00\x00\x00\x00",
    "THERMAL_SET_EMISSIVITY": b"\xe8\x03\x00\x00",
    "THERMAL_SPOT": b"\x00\x00\x00\x00\x01\x00\x01\x00",
    "THERMAL_SET_PALETTE": b"\x02", "SLOT_CAPTURE_IMAGE": b"\x00",
}


def experiment_command(command_id: int, args: bytes = b"") -> bytes:
    """A 120-byte command packet carrying one experiment command.

    Sequence zero and no arguments, so the published bytes are reproducible.
    A real command carries an incrementing sequence, which is why these are a
    template to adapt rather than bytes to replay verbatim.
    """
    return stp.build_command(codec, command_id, TARGET, args=args,
                             sequence=0, coarse_time=0)


def request_packet(packet_type: int) -> bytes:
    """A 14-byte request with zero timestamps, so the bytes never change."""
    packet = bytearray(stp.REQUEST_SIZE)
    packet[0:4] = codec.sync_bytes()
    struct.pack_into(">I", packet, 4, 0)
    struct.pack_into(">H", packet, 8, 0)
    packet[10] = packet_type
    packet[11] = TARGET
    struct.pack_into(">H", packet, 12, codec.crc(bytes(packet[4:12])))
    return bytes(packet)


def command_packet() -> bytes:
    packet = bytearray(stp.COMMAND_SIZE if hasattr(stp, "COMMAND_SIZE") else 120)
    packet[0:4] = codec.sync_bytes()
    struct.pack_into(">I", packet, 4, 0)
    struct.pack_into(">H", packet, 8, 0)
    packet[10] = stp.TYPE_COMMAND
    packet[11] = TARGET
    struct.pack_into(">H", packet, 118, codec.crc(bytes(packet[4:118])))
    return bytes(packet)


def hexs(data: bytes) -> str:
    return data.hex().upper()


def main() -> int:
    out: list[str] = []
    out.append("# Command List")
    out.append("")
    out.append("Machine-readable command list in `CMD_Name,HexString` form.")
    out.append("Generated by `tools/generate_command_list.py`; do not edit by hand.")
    out.append("")
    out.append(f"Target ID `0x{TARGET:02X}`. Big endian. "
               "CRC-16/CCITT-FALSE, seed `0xFFFF`, in the last two bytes,")
    out.append("covering everything after the four sync bytes.")
    out.append("")

    out.append("## RS-422 packets to send to the camera")
    out.append("")
    out.append("Request packets are ready to transmit. Coarse and fine time are zero,")
    out.append("which keeps each string constant. **If you fill in the time fields you")
    out.append("must recompute the CRC**, because it covers them. COMMAND_EMPTY is")
    out.append("an invalid diagnostic vector, not a command to send.")
    out.append("")
    out.append("```")
    for name, packet_type, _ in DICE_REQUESTS:
        out.append(f"{name},{hexs(request_packet(packet_type))}")
    out.append(f"COMMAND_EMPTY,{hexs(command_packet())}")
    out.append("```")
    out.append("")
    out.append("| CMD_Name | Bytes | Type | Meaning |")
    out.append("|---|---:|---|---|")
    for name, packet_type, description in DICE_REQUESTS:
        out.append(f"| `{name}` | {len(request_packet(packet_type))} | "
                   f"`0x{packet_type:02X}` | {description} |")
    out.append(f"| `COMMAND_EMPTY` | {len(command_packet())} | `0x10` | "
               "Invalid diagnostic packet; the camera must not acknowledge it |")
    out.append("")

    out.append("## Experiment commands over RS-422")
    out.append("")
    out.append("V3 examples with sequence zero. Set a fresh sequence before each live")
    out.append("command and recompute both CRCs. BUS_SELECT_CAMERA must be sent first.")
    out.append("On the connected visual firmware, SELECT_CAMERA 0 while thermal owns")
    out.append("the bus caused overlapping replies; select visual ownership first.")
    out.append("Complete 120-byte command packets. The command id sits in the first")
    out.append("payload byte, at offset 12. These are the discrete actions: one image,")
    out.append("start and stop a recording, stream, or correct the image.")
    out.append("")
    out.append("```")
    for name, (_, opcode) in CHAIN_COMMANDS.items():
        out.append(f"CMD_{name},{hexs(experiment_command(opcode, EXAMPLE_ARGS.get(name, b'')))}")
    for name in EXPERIMENT_COMMANDS:
        key = name.lower().replace("_", "-")
        out.append(f"CMD_{name},{hexs(experiment_command(stp.COMMANDS[key]))}")
    out.append("```")
    out.append("")
    out.append("| CMD_Name | Id | Corrects first | Meaning |")
    out.append("|---|---|---|---|")
    for name, (description, opcode) in CHAIN_COMMANDS.items():
        out.append(f"| `CMD_{name}` | `{opcode:02X}` | - | {description} |")
    for name, description in EXPERIMENT_COMMANDS.items():
        key = name.lower().replace("_", "-")
        corrects = "yes" if name in ("TAKE_IMAGE", "START_RECORD") else "-"
        out.append(f"| `CMD_{name}` | `{stp.COMMANDS[key]:02X}` | {corrects} | {description} |")
    out.append("")

    out.append("## Camera command opcodes")
    out.append("")
    out.append("Two-byte opcodes for the camera's own command protocol, carried over")
    out.append("USB serial. Same codes as the command line tool.")
    out.append("")
    out.append("```")
    for name in CAMERA_COMMANDS:
        out.append(f"{name.upper().replace('-', '_')},{cli.OPCODES[name]:04X}")
    out.append("```")
    out.append("")
    out.append("| CMD_Name | Hex | Command line name | Meaning |")
    out.append("|---|---|---|---|")
    for name, description in CAMERA_COMMANDS.items():
        out.append(f"| `{name.upper().replace('-', '_')}` | `{cli.OPCODES[name]:04X}` | "
                   f"`{name}` | {description} |")
    out.append("")

    out.append("## Packet types")
    out.append("")
    out.append("```")
    for name, value in (("TYPE_COMMAND", stp.TYPE_COMMAND), ("TYPE_LRT", stp.TYPE_LRT),
                        ("TYPE_HRT_STOP", stp.TYPE_HRT_STOP),
                        ("TYPE_HRT_STOP_WITH_LOSS", stp.TYPE_HRT_STOP_WITH_LOSS),
                        ("TYPE_HRT_GO", stp.TYPE_HRT_GO)):
        out.append(f"{name},{value:02X}")
    out.append(f"SYNC_WORD,{stp.SYNC_WORD:08X}")
    out.append(f"TARGET_ID,{TARGET:02X}")
    out.append("```")
    out.append("")
    out.append("| CMD_Name | Hex | Meaning |")
    out.append("|---|---|---|")
    out.append("| `TYPE_COMMAND` | `10` | Command from DICE, 120 bytes; acknowledgement back, 8 bytes |")
    out.append("| `TYPE_LRT` | `81` | Vitals request in, 14 bytes; vitals data back, 1256 bytes |")
    out.append("| `TYPE_HRT_STOP` | `85` | Stop the image stream, 14 bytes |")
    out.append("| `TYPE_HRT_STOP_WITH_LOSS` | `86` | Stop the image stream with loss, 14 bytes |")
    out.append("| `TYPE_HRT_GO` | `87` | Start the stream in, 14 bytes; image data back, 1288 bytes |")
    out.append("| `SYNC_WORD` | `1ACFFC1D` | Start of every packet, sent most significant byte first |")
    out.append("| `TARGET_ID` | `C7` | This camera |")
    out.append("")

    Path("COMMANDS.md").write_text("\n".join(out) + "\n", encoding="utf-8")
    print(f"wrote COMMANDS.md ({len(out)} lines)")

    # Every generated packet must pass the same check the camera applies.
    for name, packet_type, _ in DICE_REQUESTS:
        packet = request_packet(packet_type)
        assert codec.u16(packet, 12) == codec.crc(packet[4:12]), name
    packet = command_packet()
    assert codec.u16(packet, 118) == codec.crc(packet[4:118])
    for name in EXPERIMENT_COMMANDS:
        key = name.lower().replace("_", "-")
        packet = experiment_command(stp.COMMANDS[key])
        assert codec.u16(packet, 118) == codec.crc(packet[4:118]), name
        assert packet[12] == stp.COMMANDS[key], name
    print("all generated packets pass their own CRC check")
    return 0


if __name__ == "__main__":
    sys.exit(main())
