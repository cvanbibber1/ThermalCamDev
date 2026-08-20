#!/usr/bin/env python3
"""Diagnose an RS-422 link that carries traffic one way only.

The camera transmits perfectly and receives nothing, which narrows the fault to
the camera's receive pair or its transceiver's receiver-enable. These modes
give you something steady to probe, and a way to prove which end is at fault.

    python tools/rs422_diagnose.py --port COM39 --mode pattern
    python tools/rs422_diagnose.py --port COM39 --mode loopback
    python tools/rs422_diagnose.py --port COM39 --mode listen
"""

from __future__ import annotations

import argparse
import sys
import time

try:
    import serial
except ImportError:  # pragma: no cover
    sys.exit("pyserial is required: pip install pyserial")


def mode_pattern(port: serial.Serial, seconds: float, baud: int) -> int:
    """Transmit continuously so the line can be probed with a meter or scope.

    0x55 alternates every bit, so the differential pair carries a square wave at
    half the baud rate. That is the easiest thing to recognise on a scope, and
    it also gives a meter something to average.
    """
    print(f"Transmitting 0x55 continuously at {baud} baud for {seconds:.0f}s.")
    print("Probe, in this order:")
    print(f"  1. RE on the transceiver. Must be LOW to receive. High means the")
    print(f"     receiver is switched off, which is the most likely fault here.")
    print(f"  2. A and B at the camera. Expect a ~{baud / 2 / 1000:.0f} kHz square wave,")
    print("     a few volts differential. Flat means the pair is not connected.")
    print("  3. RO on the transceiver, the pin feeding the MCU. If A/B swings")
    print("     but RO does not, the receiver is disabled or the part is faulty.")
    block = bytes([0x55]) * 256
    end = time.time() + seconds
    sent = 0
    while time.time() < end:
        port.write(block)
        port.flush()
        sent += len(block)
    print(f"sent {sent} bytes")
    return 0


def mode_loopback(port: serial.Serial, seconds: float) -> int:
    """Prove whether the converter and cable work, independently of the camera.

    Jumper the converter's transmit pair to its own receive pair at the terminal
    block: TX+ to RX+ and TX- to RX-. If the marker comes back the converter and
    its wiring are sound and the fault is on the camera side. If it does not,
    the fault is the converter, its wiring, or the terminal block.
    """
    print("Loopback test. Jumper the converter's TX+ to RX+ and TX- to RX-")
    print("at the terminal block, with the camera disconnected.")
    marker = b"\xA5\x5A" + b"RS422LOOPBACK" + b"\x5A\xA5"
    port.reset_input_buffer()
    port.write(marker)
    port.flush()

    received = bytearray()
    end = time.time() + seconds
    while time.time() < end and marker not in received:
        received.extend(port.read(256))

    if marker in received:
        print(f"PASS: the marker came back. {len(received)} bytes received.")
        print("The converter and its wiring are good; the fault is at the camera.")
        return 0
    print(f"FAIL: the marker did not come back. {len(received)} bytes received.")
    if received:
        print(f"  what did arrive: {bytes(received[:32]).hex(' ')}")
        print("  Bytes but not the marker suggests a baud or polarity problem.")
    else:
        print("  Nothing at all: check the jumper, the terminal block, and that")
        print("  the converter really has a separate transmit pair (4-wire).")
    return 1


def mode_listen(port: serial.Serial, seconds: float) -> int:
    """Show whether anything is arriving from the camera, and how fast."""
    print(f"Listening for {seconds:.0f}s.")
    port.reset_input_buffer()
    total = 0
    sync = 0
    end = time.time() + seconds
    buffer = bytearray()
    while time.time() < end:
        chunk = port.read(4096)
        if not chunk:
            continue
        total += len(chunk)
        buffer.extend(chunk)
        sync += buffer.count(b"\x1a\xcf\xfc\x1d")
        del buffer[:-3]
    rate = total / max(seconds, 1e-6)
    print(f"received {total} bytes ({rate:.0f} bytes/s), {sync} packet markers")
    if total == 0:
        print("  Nothing from the camera. Check power, and that the camera's")
        print("  transmit pair reaches the converter's receive pair.")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True, help="RS-422 converter serial port")
    parser.add_argument("--baud", type=int, default=921600)
    parser.add_argument("--mode", choices=["pattern", "loopback", "listen"],
                        default="listen")
    parser.add_argument("--seconds", type=float, default=10.0)
    args = parser.parse_args()

    try:
        port = serial.Serial(args.port, args.baud, timeout=0.2)
    except serial.SerialException as error:
        import serial.tools.list_ports as list_ports

        available = ", ".join(p.device for p in list_ports.comports()) or "none"
        sys.exit(f"cannot open {args.port}: {error}\nports present: {available}")
    try:
        if args.mode == "pattern":
            return mode_pattern(port, args.seconds, args.baud)
        if args.mode == "loopback":
            return mode_loopback(port, args.seconds)
        return mode_listen(port, args.seconds)
    finally:
        port.close()


if __name__ == "__main__":
    sys.exit(main())
