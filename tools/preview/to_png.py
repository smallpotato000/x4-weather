#!/usr/bin/env python3
"""Convert the host preview PBM into PNGs.

  <input.pbm> <native.png> <portrait.png>

`native.png` is the raw panel framebuffer (800x480 landscape, as the SSD1677
scans it). `portrait.png` is the same frame rotated so it reads upright when the
X4 is held tall — the orientation the UI is laid out for (DisplayTarget
Orientation::Portrait = 90 deg CW into the panel).
"""

from __future__ import annotations

import sys
from pathlib import Path

from PIL import Image


def main() -> None:
    if len(sys.argv) != 4:
        raise SystemExit(__doc__)
    src, native_out, portrait_out = (Path(p) for p in sys.argv[1:])

    with src.open("rb") as handle:
        magic = handle.readline().strip()
        if magic != b"P4":
            raise SystemExit(f"error: {src} is not a P4 PBM")
        line = handle.readline()
        while line.startswith(b"#"):
            line = handle.readline()
        width, height = (int(v) for v in line.split())
        raster = handle.read((width + 7) // 8 * height)

    # FreeInkDisplay framebuffer convention: MSB-first, a SET bit is WHITE
    # paper and a CLEAR bit is BLACK ink (clearScreen(0xFF) is a white page).
    native = Image.new("L", (width, height), 0)
    pixels = native.load()
    stride = (width + 7) // 8
    for y in range(height):
        for x in range(width):
            if raster[y * stride + (x >> 3)] & (0x80 >> (x & 7)):
                pixels[x, y] = 255
    native.save(native_out)

    # DisplayTarget Portrait maps logical (x,y) -> panel (y, 479-x), so the
    # logical portrait frame is the panel image rotated 90 deg clockwise (PIL's
    # ROTATE_270) — that is the orientation the user sees holding the X4 tall.
    portrait = native.transpose(Image.ROTATE_270)
    portrait.save(portrait_out)
    print(f"wrote {native_out} and {portrait_out} ({width}x{height} panel)")


if __name__ == "__main__":
    main()
