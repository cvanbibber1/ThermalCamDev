#!/usr/bin/env python3
"""Ground-side decoder for the camera's compressed image stream.

This is the mirror of src/protocol/frame_codec.c. The two must agree exactly,
so read them together if you change either.

The camera codes each frame either against itself (a keyframe, INTRA) or
against the frame before it (INTER). An INTER frame is only decodable if the
previous one arrived intact, which is why the camera sends a keyframe every
APP_CODEC_GOP frames. Every encoded frame carries a checksum of the original
pixels, so a reconstruction is verified rather than assumed.
"""

from __future__ import annotations

import numpy as np

WIDTH = 160
HEIGHT = 120
PIXELS = WIDTH * HEIGHT
FRAME_BYTES = PIXELS * 2

MODE_RAW = 0
MODE_INTRA = 1
MODE_INTER = 2
# Set in the packet's mode byte on the last chunk of a frame. The camera starts
# transmitting before it has finished encoding, so the chunk count is not known
# when the first chunk goes out.
MODE_FINAL = 0x80
MODE_MASK = 0x7F

BLOCK = 32
ESCAPE = 20
CHECKSUM_SEED = 2166136261
MASK32 = 0xFFFFFFFF


class CodecError(Exception):
    """The stream did not decode, so the frame must be discarded."""


def checksum(frame: np.ndarray) -> int:
    """FNV-1a over the frame bytes, matching frame_codec_checksum()."""
    flat = np.asarray(frame, dtype="<u2").reshape(-1)
    low = (flat & 0xFF).astype(np.uint32)
    high = (flat >> 8).astype(np.uint32)
    hash_value = CHECKSUM_SEED
    for lo, hi in zip(low.tolist(), high.tolist()):
        hash_value = ((hash_value ^ lo) * 16777619) & MASK32
        hash_value = ((hash_value ^ hi) * 16777619) & MASK32
    return hash_value


class _BitReader:
    """MSB-first, matching the writer in the firmware."""

    __slots__ = ("data", "position", "limit")

    def __init__(self, data: bytes) -> None:
        self.data = data
        self.position = 0
        self.limit = len(data) * 8

    def bit(self) -> int:
        if self.position >= self.limit:
            raise CodecError("stream ran out")
        byte = self.data[self.position >> 3]
        value = (byte >> (7 - (self.position & 7))) & 1
        self.position += 1
        return value

    def bits(self, count: int) -> int:
        if self.position + count > self.limit:
            raise CodecError("stream ran out")
        value = 0
        for _ in range(count):
            value = (value << 1) | self.bit()
        return value


def _unzigzag(value: int) -> int:
    return (value >> 1) ^ -(value & 1)


def _rice(reader: _BitReader, k: int) -> int:
    quotient = 0
    while quotient < ESCAPE and reader.bit():
        quotient += 1
    if quotient >= ESCAPE:
        return reader.bits(24)
    remainder = reader.bits(k) if k else 0
    return (quotient << k) | remainder


def _predict(a: int, b: int, c: int) -> int:
    """Median edge detection, as in JPEG-LS."""
    high = a if a > b else b
    low = a if a < b else b
    if c >= high:
        return low
    if c <= low:
        return high
    return a + b - c


def decode(payload: bytes, mode: int, reference: np.ndarray | None) -> np.ndarray:
    """Reconstruct one frame as a HEIGHT x WIDTH array of uint16 counts.

    `reference` is the previously decoded frame, required for INTER. Raises
    CodecError if the stream is truncated, malformed, or fails its checksum.
    """
    if mode == MODE_RAW:
        if len(payload) < FRAME_BYTES:
            raise CodecError("raw frame is short")
        return np.frombuffer(payload[:FRAME_BYTES], dtype="<u2").reshape(HEIGHT, WIDTH).copy()

    if mode not in (MODE_INTRA, MODE_INTER):
        raise CodecError(f"unknown codec mode {mode}")
    if mode == MODE_INTER and reference is None:
        raise CodecError("difference frame with no reference")

    if len(payload) < 4:
        raise CodecError("stream is too short to hold a checksum")
    # The checksum is the last four bytes. It cannot go at the front: the
    # camera transmits the opening chunks before the encode has finished, so
    # there is nothing to write there yet.
    body, trailer = payload[:-4], payload[-4:]
    expected = int.from_bytes(trailer, "big")
    reader = _BitReader(body)

    # `plane` is what the predictor works on: pixel values for a keyframe, or
    # the change since the previous frame otherwise.
    plane = [[0] * WIDTH for _ in range(HEIGHT)]
    remaining = 0
    k = 0
    for y in range(HEIGHT):
        row = plane[y]
        above = plane[y - 1] if y else None
        for x in range(WIDTH):
            if remaining == 0:
                k = reader.bits(4)
                remaining = BLOCK
            residual = _unzigzag(_rice(reader, k))
            remaining -= 1
            if x == 0 and y == 0:
                row[0] = residual
                continue
            a = row[x - 1] if x else above[0]
            b = above[x] if y else a
            c = above[x - 1] if (x and y) else b
            row[x] = residual + _predict(a, b, c)

    frame = np.array(plane, dtype=np.int64)
    if mode == MODE_INTER:
        frame = frame + np.asarray(reference, dtype=np.int64)
    frame = (frame & 0xFFFF).astype(np.uint16)

    if checksum(frame) != expected:
        raise CodecError("checksum mismatch: the reconstruction is not the original")
    return frame


class Stream:
    """Tracks the reference frame across a sequence of decoded frames."""

    def __init__(self) -> None:
        self.previous: np.ndarray | None = None
        self.decoded = 0
        self.failed = 0
        # Split by cause: a missing reference means an earlier frame was lost
        # and this one is collateral, whereas a checksum failure means this
        # frame itself arrived wrong.
        self.no_reference = 0
        self.checksum_failed = 0

    def push(self, payload: bytes, mode: int) -> np.ndarray | None:
        """Decode one frame, or return None and count it if that is not possible.

        A failure drops the reference, so every following difference frame is
        refused until the camera's next keyframe. That is deliberate: showing a
        picture rebuilt on a frame we know is wrong would be worse than showing
        none.
        """
        try:
            frame = decode(payload, mode, self.previous)
        except CodecError as error:
            self.failed += 1
            if "no reference" in str(error):
                self.no_reference += 1
            else:
                self.checksum_failed += 1
            self.previous = None
            return None
        self.previous = frame
        self.decoded += 1
        return frame
