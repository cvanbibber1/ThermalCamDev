#!/usr/bin/env python3
"""Render a 160x120 Lepton Y16 raw frame to a viewable image.

The firmware publishes TLinear centikelvin values (0.01 K per count). This tool
applies a chosen automatic gain control (AGC) and colormap so the low-contrast
radiometric range becomes visible.
"""

from __future__ import annotations

import argparse
import sys

import numpy as np
from PIL import Image

WIDTH = 160
HEIGHT = 120


def load_frame(path: str) -> np.ndarray:
    raw = np.fromfile(path, dtype="<u2")
    if raw.size != WIDTH * HEIGHT:
        raise SystemExit(f"{path}: expected {WIDTH * HEIGHT} pixels, got {raw.size}")
    return raw.reshape(HEIGHT, WIDTH).astype(np.float64)


def destripe(frame: np.ndarray) -> np.ndarray:
    """Remove Lepton column fixed-pattern noise.

    Each column carries a constant offset from its readout channel. Scene
    content varies between neighbouring columns far less than that offset does,
    so subtracting each column's deviation from a locally smoothed profile
    removes the stripes without flattening real structure.
    """
    profile = frame.mean(axis=0)
    padded = np.pad(profile, 2, mode="edge")
    smooth = np.stack([padded[i : i + profile.size] for i in range(5)]).mean(axis=0)
    return frame - (profile - smooth)


def flat_field(frame: np.ndarray, reference: np.ndarray) -> np.ndarray:
    """Divide out lens/housing shading measured from a uniform-scene frame."""
    gain = reference.mean() / np.maximum(reference, 1.0)
    return frame * gain


def agc_linear(frame: np.ndarray, low_pct: float, high_pct: float) -> np.ndarray:
    lo, hi = np.percentile(frame, [low_pct, high_pct])
    if hi <= lo:
        hi = lo + 1.0
    return np.clip((frame - lo) / (hi - lo), 0.0, 1.0)


def agc_equalize(frame: np.ndarray) -> np.ndarray:
    order = frame.ravel().argsort(kind="stable")
    ranks = np.empty(order.size, dtype=np.float64)
    ranks[order] = np.arange(order.size, dtype=np.float64)
    return (ranks / max(order.size - 1, 1)).reshape(frame.shape)


def agc_plateau(frame: np.ndarray, plateau: float) -> np.ndarray:
    """Histogram equalization with a per-bin count limit (FLIR-style plateau AGC)."""
    counts, edges = np.histogram(frame, bins=1024)
    limit = max(1.0, plateau * frame.size / counts.size)
    counts = np.minimum(counts, limit)
    cdf = np.cumsum(counts, dtype=np.float64)
    cdf /= cdf[-1]
    idx = np.clip(np.digitize(frame, edges[1:-1]), 0, counts.size - 1)
    return cdf[idx]


def unsharp(norm: np.ndarray, amount: float) -> np.ndarray:
    """Sharpen with a 3x3 box blur; keeps the dependency set to numpy only."""
    if amount <= 0.0:
        return norm
    padded = np.pad(norm, 1, mode="edge")
    blur = sum(
        padded[dy : dy + norm.shape[0], dx : dx + norm.shape[1]]
        for dy in range(3)
        for dx in range(3)
    ) / 9.0
    return np.clip(norm + amount * (norm - blur), 0.0, 1.0)


def _ramp(stops: list[tuple[float, tuple[int, int, int]]]) -> np.ndarray:
    positions = np.array([s[0] for s in stops])
    colors = np.array([s[1] for s in stops], dtype=np.float64)
    x = np.linspace(0.0, 1.0, 256)
    return np.stack([np.interp(x, positions, colors[:, c]) for c in range(3)], axis=1)


COLORMAPS = {
    "gray": _ramp([(0.0, (0, 0, 0)), (1.0, (255, 255, 255))]),
    "ironbow": _ramp(
        [
            (0.00, (0, 0, 0)),
            (0.20, (37, 0, 96)),
            (0.40, (122, 15, 118)),
            (0.60, (207, 66, 72)),
            (0.80, (253, 158, 24)),
            (0.92, (255, 227, 106)),
            (1.00, (255, 255, 255)),
        ]
    ),
    "rainbow": _ramp(
        [
            (0.00, (0, 0, 60)),
            (0.20, (0, 60, 190)),
            (0.40, (0, 190, 190)),
            (0.60, (30, 200, 60)),
            (0.80, (240, 220, 40)),
            (1.00, (255, 40, 40)),
        ]
    ),
    "whitehot": _ramp([(0.0, (0, 0, 0)), (1.0, (255, 255, 255))]),
    "blackhot": _ramp([(0.0, (255, 255, 255)), (1.0, (0, 0, 0))]),
}


def colorize(norm: np.ndarray, name: str) -> np.ndarray:
    lut = COLORMAPS[name]
    idx = np.clip((norm * 255.0).round().astype(np.int32), 0, 255)
    return lut[idx].astype(np.uint8)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", help="raw Y16 frame, 160x120 little-endian")
    parser.add_argument("-o", "--output", required=True, help="output PNG path")
    parser.add_argument(
        "--agc",
        default="plateau",
        choices=["linear", "equalize", "plateau"],
        help="contrast mapping (default: plateau)",
    )
    parser.add_argument("--low", type=float, default=0.5, help="linear AGC low percentile")
    parser.add_argument("--high", type=float, default=99.5, help="linear AGC high percentile")
    parser.add_argument("--plateau", type=float, default=0.015, help="plateau AGC bin limit fraction")
    parser.add_argument("--colormap", default="ironbow", choices=sorted(COLORMAPS))
    parser.add_argument("--sharpen", type=float, default=0.6, help="unsharp amount, 0 disables")
    parser.add_argument("--scale", type=int, default=4, help="integer upscale factor")
    parser.add_argument("--flip-v", action="store_true", help="flip vertically")
    parser.add_argument("--flip-h", action="store_true", help="flip horizontally")
    parser.add_argument("--stats", action="store_true", help="print frame statistics")
    parser.add_argument(
        "--destripe", action="store_true", help="remove column fixed-pattern noise"
    )
    parser.add_argument(
        "--flat-field",
        metavar="REF",
        help="raw frame of a uniform scene used to divide out lens shading",
    )
    args = parser.parse_args(argv)

    frame = load_frame(args.input)
    if args.flip_v:
        frame = frame[::-1, :]
    if args.flip_h:
        frame = frame[:, ::-1]

    if args.flat_field:
        frame = flat_field(frame, load_frame(args.flat_field))
    if args.destripe:
        frame = destripe(frame)

    if args.stats:
        kelvin = frame / 100.0
        print(
            f"counts min={frame.min():.0f} max={frame.max():.0f} "
            f"mean={frame.mean():.1f} std={frame.std():.1f} unique={np.unique(frame).size}"
        )
        print(
            f"kelvin min={kelvin.min():.2f} max={kelvin.max():.2f} "
            f"mean={kelvin.mean():.2f} | celsius min={kelvin.min() - 273.15:.2f} "
            f"max={kelvin.max() - 273.15:.2f}"
        )

    if args.agc == "linear":
        norm = agc_linear(frame, args.low, args.high)
    elif args.agc == "equalize":
        norm = agc_equalize(frame)
    else:
        norm = agc_plateau(frame, args.plateau)

    norm = unsharp(norm, args.sharpen)
    rgb = colorize(norm, args.colormap)

    image = Image.fromarray(rgb)
    if args.scale > 1:
        image = image.resize(
            (WIDTH * args.scale, HEIGHT * args.scale), resample=Image.LANCZOS
        )
    image.save(args.output)
    print(f"wrote {args.output}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
