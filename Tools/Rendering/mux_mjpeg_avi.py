#!/usr/bin/env python3
"""Deterministically wraps a numbered JPEG sequence in an MJPEG AVI."""

from __future__ import annotations

import argparse
import glob
import os
import struct
from pathlib import Path


def fourcc(value: str) -> bytes:
    encoded = value.encode("ascii")
    if len(encoded) != 4:
        raise ValueError(value)
    return encoded


class AviWriter:
    def __init__(self, path: Path) -> None:
        self.file = path.open("wb+")

    def u16(self, value: int) -> None:
        self.file.write(struct.pack("<H", value))

    def u32(self, value: int) -> None:
        self.file.write(struct.pack("<I", value))

    def begin(self, tag: str) -> int:
        self.file.write(fourcc(tag))
        offset = self.file.tell()
        self.u32(0)
        return offset

    def end(self, offset: int) -> None:
        end = self.file.tell()
        size = end - offset - 4
        if size & 1:
            self.file.write(b"\0")
        after = self.file.tell()
        self.file.seek(offset)
        self.u32(size)
        self.file.seek(after)


def mux(pattern: str, output: Path, fps: int, width: int, height: int) -> None:
    frames = [Path(p) for p in sorted(glob.glob(pattern))]
    if not frames:
        raise RuntimeError(f"No frames match {pattern}")
    payloads = [frame.read_bytes() for frame in frames]
    if any(data[:2] != b"\xff\xd8" or data[-2:] != b"\xff\xd9" for data in payloads):
        raise RuntimeError("Sequence contains a non-JPEG frame")
    max_bytes = max(map(len, payloads))
    writer = AviWriter(output)
    try:
        riff = writer.begin("RIFF")
        writer.file.write(fourcc("AVI "))
        hdrl = writer.begin("LIST")
        writer.file.write(fourcc("hdrl"))
        avih = writer.begin("avih")
        values = [round(1_000_000 / fps), max_bytes * fps, 0, 0x10,
                  len(frames), 0, 1, max_bytes, width, height, 0, 0, 0, 0]
        for value in values:
            writer.u32(value)
        writer.end(avih)
        strl = writer.begin("LIST")
        writer.file.write(fourcc("strl"))
        strh = writer.begin("strh")
        writer.file.write(fourcc("vids") + fourcc("MJPG"))
        writer.u32(0); writer.u16(0); writer.u16(0); writer.u32(0)
        for value in [1, fps, 0, len(frames), max_bytes, 0xFFFFFFFF, 0]:
            writer.u32(value)
        writer.u16(0); writer.u16(0); writer.u16(width); writer.u16(height)
        writer.end(strh)
        strf = writer.begin("strf")
        for value in [40, width, height]: writer.u32(value)
        writer.u16(1); writer.u16(24); writer.file.write(fourcc("MJPG"))
        for value in [width * height * 3, 0, 0, 0, 0]: writer.u32(value)
        writer.end(strf); writer.end(strl); writer.end(hdrl)
        movi = writer.begin("LIST")
        writer.file.write(fourcc("movi"))
        movie_type_offset = movi + 4
        offsets: list[int] = []
        sizes: list[int] = []
        for data in payloads:
            chunk_start = writer.file.tell()
            chunk = writer.begin("00dc")
            writer.file.write(data)
            writer.end(chunk)
            offsets.append(chunk_start - movie_type_offset)
            sizes.append(len(data))
        writer.end(movi)
        idx = writer.begin("idx1")
        for offset, size in zip(offsets, sizes):
            writer.file.write(fourcc("00dc")); writer.u32(0x10)
            writer.u32(offset); writer.u32(size)
        writer.end(idx); writer.end(riff)
    finally:
        writer.file.close()
    if output.stat().st_size <= 4096:
        raise RuntimeError("AVI is unexpectedly small")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--frames", required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--fps", type=int, required=True)
    parser.add_argument("--width", type=int, required=True)
    parser.add_argument("--height", type=int, required=True)
    args = parser.parse_args()
    mux(args.frames, args.output, args.fps, args.width, args.height)
