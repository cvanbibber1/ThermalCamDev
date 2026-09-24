"""Send individual thermal HRT controls and capture commands over RS-422."""

from __future__ import annotations

import argparse
import json
import sys
import time
from pathlib import Path

import serial

sys.path.insert(0, str(Path(__file__).resolve().parent))
import stp_monitor as stp


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="COM56")
    parser.add_argument("--output", type=Path, default=Path("validation/persistent_hrt_normal_com56.json"))
    args = parser.parse_args()
    codec = stp.Codec(True, 0xFFFF)
    sync = codec.sync_bytes()
    buffer = bytearray()
    stats = {"bad_outer_crc": 0, "bad_packets": [], "go_requests": 0,
             "hrt_packets": 0, "lrt_packets": 0}
    wire_steps: list[dict] = []
    phase = "initial"
    seq = 22000
    port = serial.Serial(args.port, 921600, timeout=0.01)
    try:
        port.set_buffer_size(rx_size=1 << 20, tx_size=1 << 16)
    except (AttributeError, NotImplementedError):
        pass

    def request(kind: int) -> None:
        if kind == stp.TYPE_HRT_GO:
            stats["go_requests"] += 1
        packet = stp.build_request(codec, kind, 0xC7)
        wire_steps.append({"phase": phase, "tx": "request", "type": f"0x{kind:02X}",
                           "wire_hex": packet.hex()})
        port.write(packet)
        port.flush()

    def command(opcode: int, payload: bytes = b"") -> int:
        nonlocal seq
        seq += 1
        packet = stp.build_command(codec, opcode, 0xC7, args=payload, sequence=seq)
        wire_steps.append({"phase": phase, "tx": "command", "opcode": f"0x{opcode:02X}",
                           "seq": seq, "args_hex": payload.hex(),
                           "dice_command_payload_hex": packet[12:117].hex(),
                           "wire_hex": packet.hex()})
        port.write(packet)
        port.flush()
        return seq

    def receive(duration: float) -> list[bytes]:
        packets: list[bytes] = []
        deadline = time.monotonic() + duration
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
                    stats["bad_outer_crc"] += 1
                    stats["bad_packets"].append({"phase": phase, "type": packet[4],
                                                 "prefix": packet[:24].hex(), "length": size})
                    continue
                if len(packet) == stp.HRT_DATA_SIZE:
                    stats["hrt_packets"] += 1
                elif len(packet) == stp.LRT_DATA_SIZE:
                    stats["lrt_packets"] += 1
                packets.append(packet)
        return packets

    def result(command_seq: int, opcode: int) -> dict:
        request(stp.TYPE_LRT)
        for packet in receive(0.55):
            if len(packet) == stp.LRT_DATA_SIZE:
                fields = stp.decode_lrt(codec, packet[6:1254])
                if fields.get("command_seq") == command_seq and fields.get("last_command") == opcode:
                    wire_steps.append({"phase": phase, "rx": "matching_lrt",
                                       "opcode": f"0x{opcode:02X}", "seq": command_seq,
                                       "result": fields.get("last_result"),
                                       "gate_open": fields.get("hrt_go_active")})
                    return fields
        raise RuntimeError(f"no matching LRT result for 0x{opcode:02X} seq {command_seq}")

    def frames_from(packets: list[bytes]) -> list[int]:
        assembler = stp.FrameAssembler()
        frames = []
        for packet in packets:
            if len(packet) == stp.HRT_DATA_SIZE:
                frame = assembler.push(codec, packet[6:1286])
                if frame is not None:
                    frames.append(len(frame))
        return frames

    report = {"port": args.port, "checks": {}, "stats": stats,
              "wire_steps": wire_steps}
    try:
        phase = "select"
        selected = result(command(0x6F, b"\x01"), 0x6F)
        report["identity"] = {k: selected.get(k) for k in
                              ("layout", "node_index", "bus_owner", "build_commit", "build_source_sha256")}
        report["checks"]["layout3_thermal_selected"] = (
            selected.get("layout") == 3 and selected.get("node_index") == 1
            and selected.get("bus_owner") == 1 and selected.get("last_result") == "ok")
        report["checks"]["gate_closed_from_boot"] = not selected.get("hrt_go_active")

        phase = "closed_capture"
        closed_still = result(command(0x30), 0x30)
        report["checks"]["capture_refused_while_stopped"] = (
            closed_still.get("last_result") == "hrt stopped")
        report["checks"]["no_hrt_while_stopped"] = not any(
            len(p) == stp.HRT_DATA_SIZE for p in receive(0.7))

        phase = "still"
        request(stp.TYPE_HRT_GO)
        receive(0.1)
        still = result(command(0x30), 0x30)
        still_packets = receive(3.8)
        still_frames = frames_from(still_packets)
        report["still"] = {"command_result": still.get("last_result"),
                           "hrt_packets": sum(len(p) == stp.HRT_DATA_SIZE for p in still_packets),
                           "frame_lengths": still_frames}
        report["checks"]["go_one_complete_still"] = (
            still.get("last_result") == "ok" and still_frames == [38400])
        request(stp.TYPE_HRT_STOP)
        receive(0.2)
        phase = "stopped_commands"
        stopped_still = result(command(0x30), 0x30)
        stopped_stream = result(command(0x78), 0x78)
        report["checks"]["capture_refused_after_stop"] = (
            stopped_still.get("last_result") == "hrt stopped")
        report["checks"]["stream_refused_after_stop"] = (
            stopped_stream.get("last_result") == "hrt stopped")
        report["checks"]["stop_latches_closed"] = (
            not stopped_stream.get("hrt_go_active")
            and not any(len(p) == stp.HRT_DATA_SIZE for p in receive(0.7)))

        phase = "stream"
        request(stp.TYPE_HRT_GO)
        receive(0.1)
        started = result(command(0x78), 0x78)
        stream_packets = receive(5.0)
        stream_frames = frames_from(stream_packets)
        report["stream"] = {"command_result": started.get("last_result"),
                            "hrt_packets": sum(len(p) == stp.HRT_DATA_SIZE for p in stream_packets),
                            "frame_lengths": stream_frames}
        report["checks"]["go_multiple_complete_frames"] = (
            started.get("last_result") == "ok" and len(stream_frames) >= 2
            and all(length == 38400 for length in stream_frames))
        phase = "stop"
        request(stp.TYPE_HRT_STOP)
        receive(0.3)  # an already-started DMA packet may finish
        report["checks"]["no_hrt_reassertion_after_stop"] = not any(
            len(p) == stp.HRT_DATA_SIZE for p in receive(0.7))
        stopped = result(command(0x79), 0x79)
        report["checks"]["stop_result_ok"] = stopped.get("last_result") == "ok"
        request(stp.TYPE_LRT)
        final_lrt = [stp.decode_lrt(codec, p[6:1254]) for p in receive(0.3)
                     if len(p) == stp.LRT_DATA_SIZE]
        report["checks"]["gate_closed_after_stop"] = bool(final_lrt) and not final_lrt[-1].get("hrt_go_active")
        phase = "recovered_still"
        request(stp.TYPE_HRT_GO)
        receive(0.1)
        recovered = result(command(0x30), 0x30)
        recovered_frames = frames_from(receive(3.8))
        report["checks"]["capture_recovers_after_go"] = (
            recovered.get("last_result") == "ok"
            and recovered_frames == [38400])
        report["checks"]["exactly_three_go_requests"] = stats["go_requests"] == 3
        report["checks"]["zero_outer_crc"] = stats["bad_outer_crc"] == 0
        request(stp.TYPE_HRT_STOP)
        receive(0.2)
        command(0x6F, b"\x00")  # leave the shared bus with the visual node
        receive(0.2)
    finally:
        port.close()
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps({"checks": report["checks"], "stats": stats,
                      "still": report.get("still"), "stream_frames": len(report.get("stream", {}).get("frame_lengths", []))}))
    return 0 if report["checks"] and all(report["checks"].values()) else 1


if __name__ == "__main__":
    raise SystemExit(main())
