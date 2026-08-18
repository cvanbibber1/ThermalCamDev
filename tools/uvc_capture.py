#!/usr/bin/env python3
"""Capture Y16 frames from the camera's USB video interface on Windows.

The device presents a 160x120 16-bit Y16 UVC pin. Most media tooling cannot
decode Y16 and will either refuse the pin or silently convert it to 8-bit RGB,
which destroys the radiometric data. DirectShow with colour conversion disabled
returns the raw 16-bit centikelvin values.
"""

from __future__ import annotations

import argparse
import sys
import time
from pathlib import Path

import numpy as np

try:
    import cv2
except ImportError:  # pragma: no cover - dependency reported at runtime
    sys.exit("opencv-python is required: pip install opencv-python")

WIDTH = 160
HEIGHT = 120


def open_camera(index: int) -> "cv2.VideoCapture":
    capture = cv2.VideoCapture(index, cv2.CAP_DSHOW)
    if not capture.isOpened():
        raise SystemExit(f"could not open DirectShow device {index}")
    # Must be cleared before the first read or OpenCV converts the frame to BGR.
    capture.set(cv2.CAP_PROP_CONVERT_RGB, 0)
    capture.set(cv2.CAP_PROP_FRAME_WIDTH, WIDTH)
    capture.set(cv2.CAP_PROP_FRAME_HEIGHT, HEIGHT)
    return capture


def find_camera() -> int:
    """Return the first DirectShow index that yields 16-bit 160x120 frames."""
    for index in range(8):
        capture = cv2.VideoCapture(index, cv2.CAP_DSHOW)
        if not capture.isOpened():
            capture.release()
            continue
        capture.set(cv2.CAP_PROP_CONVERT_RGB, 0)
        capture.set(cv2.CAP_PROP_FRAME_WIDTH, WIDTH)
        capture.set(cv2.CAP_PROP_FRAME_HEIGHT, HEIGHT)
        ok, frame = capture.read()
        capture.release()
        if ok and frame is not None and frame.dtype == np.uint16 and frame.shape == (HEIGHT, WIDTH):
            return index
    raise SystemExit("no Y16 160x120 DirectShow device found")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--index", type=int, help="DirectShow device index (default: probe)")
    parser.add_argument("--seconds", type=float, default=10.0, help="capture duration")
    parser.add_argument("--output", type=Path, help="write the last frame as raw Y16")
    parser.add_argument("--save-all", type=Path, help="directory for every unique frame")
    args = parser.parse_args(argv)

    index = args.index if args.index is not None else find_camera()
    capture = open_camera(index)
    if args.save_all:
        args.save_all.mkdir(parents=True, exist_ok=True)

    stamps: list[float] = []
    previous: np.ndarray | None = None
    total = unique = malformed = torn = 0
    last: np.ndarray | None = None
    start = time.time()

    try:
        while time.time() - start < args.seconds:
            ok, frame = capture.read()
            if not ok or frame is None:
                malformed += 1
                continue
            if frame.dtype != np.uint16 or frame.shape != (HEIGHT, WIDTH):
                malformed += 1
                continue
            total += 1
            stamps.append(time.time())
            if previous is None or not np.array_equal(frame, previous):
                unique += 1
                # A frame swapped mid-transfer shows a discontinuity at the
                # boundary, so compare each row seam against the frame's own
                # typical row-to-row change.
                rows = np.abs(np.diff(frame.astype(np.int32), axis=0)).mean(axis=1)
                if rows.max() > 6.0 * rows.mean():
                    torn += 1
                if args.save_all:
                    frame.astype("<u2").tofile(args.save_all / f"frame_{unique:05d}.raw")
            previous = frame.copy()
            last = frame
    finally:
        capture.release()

    if total < 2:
        raise SystemExit("captured no usable frames")
    elapsed = stamps[-1] - stamps[0]
    gaps = np.diff(stamps) * 1000.0
    print(f"device index      {index}")
    print(f"frames delivered  {total} in {elapsed:.1f}s ({(total - 1) / elapsed:.2f} fps)")
    print(f"unique frames     {unique} ({unique / elapsed:.2f} fps)")
    print(f"repeated frames   {total - unique}")
    print(f"malformed reads   {malformed}")
    print(f"torn frames       {torn}")
    print(f"interval ms       mean {gaps.mean():.1f}  min {gaps.min():.1f}  max {gaps.max():.1f}")
    if last is not None:
        kelvin = last / 100.0
        print(
            f"last frame        {last.min()}-{last.max()} counts, "
            f"{kelvin.min() - 273.15:.2f} to {kelvin.max() - 273.15:.2f} C"
        )
        if args.output:
            last.astype("<u2").tofile(args.output)
            print(f"wrote             {args.output}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
