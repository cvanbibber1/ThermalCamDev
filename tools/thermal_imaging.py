"""Shared imaging helpers for the Lepton 3.1R radiometric stream.

Frames are 160x120 unsigned 16-bit centikelvin as published by the firmware.
Everything here works on that representation so the renderer, the capture
tools, and the application agree on tone mapping and temperature conversion.
"""

from __future__ import annotations

import numpy as np

WIDTH = 160
HEIGHT = 120
PIXELS = WIDTH * HEIGHT

# The firmware configures TLinear with 0.01 K resolution.
CENTIKELVIN_PER_KELVIN = 100.0
KELVIN_AT_ZERO_CELSIUS = 273.15


def counts_to_celsius(counts):
    return counts / CENTIKELVIN_PER_KELVIN - KELVIN_AT_ZERO_CELSIUS


def counts_to_fahrenheit(counts):
    return counts_to_celsius(counts) * 9.0 / 5.0 + 32.0


def celsius_to_counts(celsius):
    return (celsius + KELVIN_AT_ZERO_CELSIUS) * CENTIKELVIN_PER_KELVIN


def format_temperature(counts, unit: str) -> str:
    if unit == "F":
        return f"{counts_to_fahrenheit(counts):.1f} \u00b0F"
    return f"{counts_to_celsius(counts):.1f} \u00b0C"


def load_raw(path: str) -> np.ndarray:
    raw = np.fromfile(path, dtype="<u2")
    if raw.size != PIXELS:
        raise ValueError(f"{path}: expected {PIXELS} pixels, got {raw.size}")
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
    return frame * (reference.mean() / np.maximum(reference, 1.0))


def agc_linear(frame: np.ndarray, low_pct: float, high_pct: float) -> np.ndarray:
    lo, hi = np.percentile(frame, [low_pct, high_pct])
    return agc_manual(frame, lo, hi)


def agc_manual(frame: np.ndarray, low: float, high: float) -> np.ndarray:
    if high <= low:
        high = low + 1.0
    return np.clip((frame - low) / (high - low), 0.0, 1.0)


def agc_equalize(frame: np.ndarray) -> np.ndarray:
    order = frame.ravel().argsort(kind="stable")
    ranks = np.empty(order.size, dtype=np.float64)
    ranks[order] = np.arange(order.size, dtype=np.float64)
    return (ranks / max(order.size - 1, 1)).reshape(frame.shape)


def agc_plateau(frame: np.ndarray, plateau: float) -> np.ndarray:
    """Histogram equalization with a per-bin count limit (FLIR-style plateau AGC).

    Plain equalization spends most of the output range on whatever fills the
    frame, so a large uniform wall flattens everything else. Capping each bin
    keeps small hot objects visible.
    """
    counts, edges = np.histogram(frame, bins=1024)
    limit = max(1.0, plateau * frame.size / counts.size)
    counts = np.minimum(counts, limit)
    cdf = np.cumsum(counts, dtype=np.float64)
    if cdf[-1] <= 0:
        return np.zeros_like(frame)
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


def _ramp(stops):
    positions = np.array([s[0] for s in stops])
    colors = np.array([s[1] for s in stops], dtype=np.float64)
    x = np.linspace(0.0, 1.0, 256)
    return np.stack([np.interp(x, positions, colors[:, c]) for c in range(3)], axis=1)


COLORMAPS = {
    "white hot": _ramp([(0.0, (0, 0, 0)), (1.0, (255, 255, 255))]),
    "black hot": _ramp([(0.0, (255, 255, 255)), (1.0, (0, 0, 0))]),
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
    "lava": _ramp(
        [
            (0.00, (0, 0, 0)),
            (0.35, (120, 20, 0)),
            (0.65, (230, 90, 0)),
            (0.85, (255, 190, 40)),
            (1.00, (255, 255, 220)),
        ]
    ),
    "arctic": _ramp(
        [
            (0.00, (0, 0, 40)),
            (0.35, (0, 90, 160)),
            (0.65, (120, 200, 230)),
            (1.00, (255, 255, 255)),
        ]
    ),
}


def colorize(norm: np.ndarray, name: str) -> np.ndarray:
    lut = COLORMAPS[name]
    idx = np.clip((norm * 255.0).round().astype(np.int32), 0, 255)
    return lut[idx].astype(np.uint8)
