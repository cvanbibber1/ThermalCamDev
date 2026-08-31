#!/usr/bin/env python3
"""Find out what is stalling the compressed image stream.

Logs every frame's arrival and every gap, and samples the camera's own
counters across each gap, so a stall can be attributed rather than guessed at.

    python stall_probe.py COM34 2000000 90

A stall has exactly one of these signatures, and this prints which:

  camera side   the camera stopped producing. lepton_state leaves 'streaming',
                or vospi_resyncs / camera_stalls climb across the gap.
  link side     packets were corrupted or lost in transit. host CRC errors
                climb across the gap.
  reference     nothing was lost or corrupted, but frames are refused because
                an earlier one was, and no keyframe has arrived since.
  host side     none of the above moved. The receiver could not keep up and
                the driver's buffer overflowed.
"""

import sys
import time

sys.path.insert(0, "tools")
import serial
import stp_monitor as stp
import frame_codec

COUNTERS = """reset_cause fatal_code clock_failures camera_boot_failures cci_errors
ffc_forced_runs vospi_discard_packets vospi_crc_errors vospi_sequence_errors
vospi_resyncs vospi_start_retries vospi_link_stalls vospi_start_failures
vospi_spi_errors vospi_chunks vospi_segments vospi_segments_ignored
frames_complete frames_dropped usb_rx_overruns usb_tx_busy adc_overruns
rs485_rx_overruns rs485_crc_errors rs485_tx_busy codec_raw_fallback
codec_keyframes codec_encode_us_max codec_chunk_starved camera_stalls
previous_fatal_code""".split()

STATES = {0: "power-off", 1: "reset-hold", 2: "booting", 3: "configuring",
          4: "wait-vsync", 5: "streaming", 6: "resync", 7: "retry"}

PORT = sys.argv[1]
BAUD = int(sys.argv[2]) if len(sys.argv) > 2 else 2000000
SECONDS = float(sys.argv[3]) if len(sys.argv) > 3 else 90.0
GAP = 1.0  # a silence longer than this counts as a stall

port = serial.Serial(PORT, BAUD, timeout=0.05)
try:
    port.set_buffer_size(rx_size=1 << 22, tx_size=1 << 16)
except Exception:
    pass

codec = stp.Codec(True, 0xFFFF)
sync = codec.sync_bytes()
assembler = stp.FrameAssembler()
lrt_request = stp.build_request(codec, 0x81, 0xC7)

port.write(stp.build_command(codec, stp.COMMANDS["stream-on"], 0xC7))
port.flush()

buffer = bytearray()
started = time.time()
last_frame = started
last_poll = 0.0
vitals = None
gap_open = None
gap_start_vitals = None
stalls = []
frames = 0
crc_errors = 0
modes = {}

print(f"listening on {PORT} at {BAUD}, {SECONDS:.0f}s, gap threshold {GAP}s\n")

while time.time() - started < SECONDS:
    now = time.time()
    if now - last_poll >= 1.0:
        last_poll = now
        try:
            port.write(lrt_request)
            port.flush()
        except Exception:
            pass

    chunk = port.read(8192)
    if chunk:
        buffer.extend(chunk)

    while True:
        start = buffer.find(sync)
        if start < 0:
            del buffer[: max(0, len(buffer) - 3)]
            break
        if start:
            del buffer[:start]
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
            crc_errors += 1
            continue

        if size == stp.LRT_DATA_SIZE:
            payload = packet[6:6 + 1248]
            vitals = {n: codec.u32(payload, 64 + 4 * k)
                      for k, n in enumerate(COUNTERS)}
            vitals["_state"] = payload[14]
            vitals["_capture"] = payload[61]
            vitals["_uptime"] = codec.u32(payload, 4)
            vitals["_gen"] = codec.u32(payload, 16)
            vitals["_host_crc"] = crc_errors

        elif size == stp.HRT_DATA_SIZE:
            frame = assembler.push(codec, packet[6:6 + 1280])
            if frame is None:
                continue
            frames += 1
            mode = assembler.mode
            modes[mode] = modes.get(mode, 0) + 1
            arrived = time.time()
            if gap_open is not None:
                stalls.append((gap_open, arrived - gap_open, gap_start_vitals, vitals))
                print(f"  [{gap_open - started:6.1f}s] stall ended after "
                      f"{arrived - gap_open:5.2f}s")
                gap_open = None
            last_frame = arrived

    if gap_open is None and (time.time() - last_frame) > GAP:
        gap_open = last_frame
        gap_start_vitals = vitals

port.close()

elapsed = time.time() - started
print(f"\n{elapsed:.0f}s: {frames} frames ({frames / elapsed:.2f}/s), "
      f"{crc_errors} host CRC errors, {assembler.discarded} incomplete, "
      f"{assembler.undecodable} undecodable "
      f"({assembler.stream.checksum_failed} corrupt, "
      f"{assembler.stream.no_reference} without a reference)")
print(f"frame modes: {modes}  (1 = keyframe, 2 = difference)")

if not stalls:
    print("\nNo stall longer than the threshold. The stream was continuous.")
    sys.exit(0)

total = sum(d for _, d, _, _ in stalls)
print(f"\n{len(stalls)} stalls, {total:.1f}s lost of {elapsed:.0f}s "
      f"({total / elapsed * 100:.0f}%), longest {max(d for _, d, _, _ in stalls):.1f}s")
print("\nwhat the camera did across each stall:")
watch = ("vospi_resyncs", "camera_stalls", "vospi_sequence_errors",
         "vospi_crc_errors", "frames_dropped", "codec_raw_fallback",
         "codec_chunk_starved", "codec_keyframes", "rs485_crc_errors")

for when, length, before, after in stalls:
    print(f"\n  stall at {when - started:6.1f}s for {length:5.2f}s")
    if not before or not after:
        print("    (no vitals either side)")
        continue
    print(f"    state {STATES.get(before['_state'], '?')} -> "
          f"{STATES.get(after['_state'], '?')}   "
          f"uptime {before['_uptime']/1000:.1f} -> {after['_uptime']/1000:.1f}s   "
          f"sensor frames {after['_gen'] - before['_gen']}")
    moved = [(n, after[n] - before[n]) for n in watch if after[n] != before[n]]
    host = after["_host_crc"] - before["_host_crc"]
    if host:
        moved.append(("host_crc_errors", host))
    if moved:
        for name, delta in moved:
            print(f"    {name:<24} +{delta}")
    else:
        print("    nothing moved on the camera or the link")

    # Attribute it.
    if after["_state"] != 5 or after["camera_stalls"] != before["camera_stalls"]:
        verdict = "CAMERA SIDE - the sensor stopped or was being recovered"
    elif after["vospi_resyncs"] != before["vospi_resyncs"]:
        verdict = "CAMERA SIDE - VoSPI reacquired during the gap"
    elif host:
        verdict = "LINK OR HOST - packets arrived corrupt"
    elif after["_gen"] - before["_gen"] > 5:
        verdict = ("HOST SIDE - the camera kept producing frames, so the loss "
                   "is in the receiver")
    else:
        verdict = "UNATTRIBUTED - see the counters above"
    print(f"    -> {verdict}")
