"""Repeat thermal protocol checks with visual attached; DE needs an analyzer."""
import argparse
import json
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import serial
import stp_monitor as stp

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--port", default="COM56", help="shared RS-422 adapter port")
parser.add_argument("--output", type=Path,
                    default=Path("tmp/verify_flashed_thermal.json"))
cli_args = parser.parse_args()
codec = stp.Codec(True, 0xFFFF)
sync = codec.sync_bytes()
port = serial.Serial(cli_args.port, 921600, timeout=0.01)
try:
    port.set_buffer_size(rx_size=1 << 20, tx_size=1 << 16)
except (AttributeError, NotImplementedError):
    pass
buffer = bytearray()
results = {"crc_errors": 0, "phases": []}
seq = 0


def command(opcode, args=b""):
    global seq
    seq += 1
    port.write(stp.build_command(codec, opcode, 0xC7, args=args, sequence=seq))
    port.flush()
    return seq


def request(packet_type):
    port.write(stp.build_request(codec, packet_type, 0xC7))
    port.flush()


def receive(seconds):
    deadline = time.monotonic() + seconds
    packets = []
    while time.monotonic() < deadline:
        buffer.extend(port.read(4096))
        while True:
            pos = buffer.find(sync)
            if pos < 0:
                del buffer[:max(0, len(buffer) - 3)]
                break
            if pos:
                del buffer[:pos]
            if len(buffer) < 6:
                break
            size = stp.transmitted_size(buffer[4])
            if size is None:
                del buffer[:4]
                continue
            if len(buffer) < size:
                break
            packet = bytes(buffer[:size])
            del buffer[:size]
            if codec.u16(packet, size - 2) != codec.crc(packet[4:size - 2]):
                results["crc_errors"] += 1
                continue
            packets.append(packet)
    return packets


def summarize(name, packets):
    summary = {"name": name, "ack": 0, "thermal_lrt": [],
               "other_lrt": 0, "hrt": 0}
    for packet in packets:
        if len(packet) == stp.ACK_SIZE:
            summary["ack"] += 1
        elif len(packet) == stp.LRT_DATA_SIZE:
            fields = stp.decode_lrt(codec, packet[6:1254])
            if fields.get("identity_block_valid") and fields.get("node_index") == 1:
                summary["thermal_lrt"].append({
                    "owner": fields["bus_owner"],
                    "seq": fields["command_seq"],
                    "opcode": fields.get("last_command"),
                    "result": fields.get("last_result"),
                    "build_commit": fields["build_commit"],
                    "build_source_sha256": fields["build_source_sha256"],
                    "cpu_health_valid": fields["cpu_health_valid"],
                    "storage_health_valid": fields["storage_health_valid"],
                    "safe_mode_health_valid": fields["safe_mode_health_valid"],
                })
            else:
                summary["other_lrt"] += 1
        elif len(packet) == stp.HRT_DATA_SIZE:
            summary["hrt"] += 1
    results["phases"].append(summary)
    print(json.dumps(summary))
    return summary


try:
    # Explicitly own the bus before asking for thermal telemetry.
    command(0x6F, b"\x01")
    request(stp.TYPE_LRT)
    summarize("select_thermal", receive(0.5))

    for name, opcode, args in [
        ("common_record_start", 0x31, b""),
        ("common_record_stop", 0x32, b""),
        ("slot_list", 0x64, b"\x00\x00"),
        ("slot_record_start", 0x67, b"\x00\x00\x00"),
    ]:
        command(opcode, args)
        request(stp.TYPE_LRT)
        summarize(name, receive(0.45))

    # A fresh boot is stopped. Open the gate before starting capture.
    request(stp.TYPE_HRT_GO)
    receive(0.1)
    command(0x30)
    request(stp.TYPE_LRT)
    summarize("capture_start", receive(0.5))
    time.sleep(1.7)
    packets = receive(2.0)
    assembler = stp.FrameAssembler()
    frames = []
    for packet in packets:
        if len(packet) == stp.HRT_DATA_SIZE:
            frame = assembler.push(codec, packet[6:1286])
            if frame is not None:
                frames.append(frame)
    summary = summarize("capture_image", packets)
    summary["complete_frames"] = len(frames)
    summary["frame_lengths"] = [len(frame) for frame in frames]
    print(json.dumps({"capture_frames": summary["complete_frames"],
                      "lengths": summary["frame_lengths"]}))

    request(stp.TYPE_HRT_STOP)
    receive(0.1)

    # Stream, then issue a transfer while an HRT packet is arriving.
    command(0x78)
    request(stp.TYPE_LRT)
    summarize("stream_start", receive(0.4))
    request(stp.TYPE_HRT_GO)
    deadline = time.monotonic() + 2.0
    saw_hrt_start = False
    while time.monotonic() < deadline and not saw_hrt_start:
        buffer.extend(port.read(16))
        pos = buffer.find(sync)
        if pos >= 0 and len(buffer) >= pos + 5 and buffer[pos + 4] == stp.TYPE_HRT_GO:
            saw_hrt_start = True
    results["active_hrt_start_seen"] = saw_hrt_start
    if saw_hrt_start:
        command(0x01)  # queue ACK
        request(stp.TYPE_LRT)
        command(0x6F, b"\x00")  # visual takes the bus
    else:
        command(0x6F, b"\x00")
    request(stp.TYPE_LRT)
    summarize("active_hrt_handoff", receive(1.2))
    # Do not send a new GO while the visual bus owner may have an active tap.
    request(stp.TYPE_LRT)
    summarize("visual_owns_bus", receive(0.8))

    command(0x6D, b"\x00")
    request(stp.TYPE_LRT)
    summarize("visual_local_sensor_select", receive(0.5))

    command(0x6F, b"\xff")
    request(stp.TYPE_LRT)
    summarize("owner_none", receive(0.6))

    command(0x6F, b"\x01")
    request(stp.TYPE_LRT)
    summarize("reselect_thermal", receive(0.6))
finally:
    port.close()
    phases = {phase["name"]: phase for phase in results["phases"]}
    checks = {
        "packet_crc": results["crc_errors"] == 0,
        "thermal_identity": bool(phases.get("select_thermal", {}).get("thermal_lrt")),
        "unsupported_storage": all(
            phase.get("thermal_lrt") and
            phase["thermal_lrt"][-1]["result"] == "unsupported"
            for name in ("common_record_start", "common_record_stop",
                         "slot_list", "slot_record_start")
            for phase in [phases.get(name, {})]
        ),
        "one_radiometric_frame": phases.get("capture_image", {}).get("frame_lengths") == [38400],
        "active_hrt_handoff": results.get("active_hrt_start_seen", False) and
                              phases.get("active_hrt_handoff", {}).get("hrt", 0) >= 1,
        "visual_ownership_silences_thermal": not phases.get("visual_owns_bus", {}).get("thermal_lrt") and
                                            phases.get("visual_owns_bus", {}).get("hrt") == 0,
        "owner_none_silent": not any(
            phases.get("owner_none", {}).get(key)
            for key in ("ack", "thermal_lrt", "other_lrt", "hrt")
        ),
        "thermal_reselected": bool(phases.get("reselect_thermal", {}).get("thermal_lrt")),
    }
    results["checks"] = checks
    cli_args.output.parent.mkdir(parents=True, exist_ok=True)
    cli_args.output.write_text(
        json.dumps(results, indent=2), encoding="utf-8")
    print(json.dumps({"checks": checks, "electrical_de_measured": False}))
    if not all(checks.values()):
        sys.exit(1)
