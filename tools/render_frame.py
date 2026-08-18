#!/usr/bin/env python3
"""Render a 160x120 Lepton Y16 raw frame to a viewable image.

The firmware publishes TLinear centikelvin values, so the radiometric range is
narrow; an automatic gain control and a colormap make it visible. The imaging
itself lives in thermal_imaging so this and the application agree.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

import numpy as np
from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))

from thermal_imaging import (  # noqa: E402
    COLORMAPS,
    HEIGHT,
    WIDTH,
    agc_equalize,
    agc_linear,
    agc_plateau,
    colorize,
    counts_to_celsius,
    destripe,
    flat_field,
    load_raw,
    unsharp,
)


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

    frame = load_raw(args.input)
    if args.flip_v:
        frame = frame[::-1, :]
    if args.flip_h:
        frame = frame[:, ::-1]

    if args.flat_field:
        frame = flat_field(frame, load_raw(args.flat_field))
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
