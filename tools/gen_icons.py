#!/usr/bin/env python3
"""Generate the 1-bit weather icon bitmaps used by the X4 weather UI.

Icons are drawn with Pillow primitives at 4x, downsampled (LANCZOS) and
thresholded, which gives clean anti-aliased-then-binarized shapes that survive
the panel's 1-bit rendering far better than hand-authored pixel art.

Two sizes are emitted per icon — one for the current-conditions block and one
for the three forecast rows — as MSB-first, row-major, set-bit-is-ink bitmaps
(freeink::ui::BitmapFormat::BW1).

The emitted array order matches weather::Icon (include/WeatherData.h); the
firmware static_asserts that the two agree.

Usage:
    python3 tools/gen_icons.py --out include/fonts/WeatherIcons.h
"""

from __future__ import annotations

import argparse
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

# Set from --ttf; only used for the "unknown" icon's question mark.
TTF_PATH = Path("tools/fonts/NotoSansSC-VF.ttf")

# Order must match weather::Icon in include/WeatherData.h.
ICONS = [
    "unknown",
    "clear_day",
    "clear_night",
    "partly_day",
    "partly_night",
    "cloudy",
    "fog",
    "drizzle",
    "rain",
    "snow",
    "thunder",
]
BIG = 128
SMALL = 56
SUPERSAMPLE = 4
INK = 255


def _disk(d: ImageDraw.ImageDraw, cx: float, cy: float, r: float, fill: int = INK) -> None:
    d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=fill)


def _rays(d: ImageDraw.ImageDraw, cx: float, cy: float, r0: float, r1: float, width: float,
          count: int = 8, start: float = 0.0, sweep: float = 360.0) -> None:
    import math
    for i in range(count):
        angle = math.radians(start + sweep * i / count)
        dx, dy = math.cos(angle), math.sin(angle)
        d.line([cx + dx * r0, cy + dy * r0, cx + dx * r1, cy + dy * r1],
               fill=INK, width=max(1, round(width)))


def _cloud(d: ImageDraw.ImageDraw, x: float, y: float, w: float, h: float, fill: int = INK) -> None:
    base_h = h * 0.44
    base_y = y + h - base_h
    d.rounded_rectangle([x, base_y, x + w, y + h], radius=base_h / 2, fill=fill)
    d.ellipse([x + w * 0.01, y + h * 0.30, x + w * 0.50, y + h * 0.95], fill=fill)
    d.ellipse([x + w * 0.24, y, x + w * 0.78, y + h * 0.74], fill=fill)
    d.ellipse([x + w * 0.50, y + h * 0.24, x + w * 0.99, y + h * 0.92], fill=fill)


def _outline_cloud(d: ImageDraw.ImageDraw, x: float, y: float, w: float, h: float, t: float) -> None:
    _cloud(d, x, y, w, h, INK)
    _cloud(d, x + t, y + t, w - 2 * t, h - 2 * t, 0)


def _drop(d: ImageDraw.ImageDraw, cx: float, cy: float, r: float, fill: int = INK) -> None:
    # Teardrop: a disk with a tapered top.
    _disk(d, cx, cy, r, fill)
    d.polygon([(cx - r * 0.92, cy), (cx, cy - r * 3.0), (cx + r * 0.92, cy)], fill=fill)


def _flake(d: ImageDraw.ImageDraw, cx: float, cy: float, r: float, width: float) -> None:
    import math
    for i in range(3):
        angle = math.radians(60 * i + 30)
        dx, dy = math.cos(angle) * r, math.sin(angle) * r
        d.line([cx - dx, cy - dy, cx + dx, cy + dy], fill=INK, width=max(1, round(width)))


def _bolt(d: ImageDraw.ImageDraw, x: float, y: float, w: float, h: float) -> None:
    d.polygon([
        (x + w * 0.56, y),
        (x + w * 0.16, y + h * 0.58),
        (x + w * 0.44, y + h * 0.58),
        (x + w * 0.30, y + h * 1.00),
        (x + w * 0.84, y + h * 0.40),
        (x + w * 0.52, y + h * 0.40),
    ], fill=INK)


_QUESTION_FONT: dict[int, ImageFont.FreeTypeFont] = {}


def _question_font(scale: int) -> ImageFont.FreeTypeFont:
    """Question-mark face for the 'unknown' icon, at supersampled size."""
    if scale not in _QUESTION_FONT:
        size = max(8, round(scale * 0.62))
        _QUESTION_FONT[scale] = ImageFont.truetype(str(TTF_PATH), size)
    return _QUESTION_FONT[scale]


def draw(name: str, d: ImageDraw.ImageDraw, s: int) -> None:
    """Draw `name` into a `s`x`s` supersampled canvas (0..s coordinates)."""
    unit = s / 100.0
    stroke = 6.5 * unit

    if name == "unknown":
        ring = [s * 0.10, s * 0.10, s * 0.90, s * 0.90]
        d.ellipse(ring, outline=INK, width=round(stroke))
        font = _question_font(s)
        d.text((s * 0.50, s * 0.52), "?", font=font, fill=INK, anchor="mm")

    elif name in ("clear_day",):
        _disk(d, s * 0.50, s * 0.50, s * 0.27)
        _rays(d, s * 0.50, s * 0.50, s * 0.34, s * 0.46, stroke * 1.15)

    elif name == "clear_night":
        _disk(d, s * 0.52, s * 0.50, s * 0.32)
        _disk(d, s * 0.68, s * 0.40, s * 0.28, fill=0)

    elif name in ("partly_day", "partly_night"):
        if name == "partly_day":
            _disk(d, s * 0.31, s * 0.26, s * 0.145)
            _rays(d, s * 0.31, s * 0.26, s * 0.195, s * 0.28, stroke * 0.8, count=8, start=22.5)
        else:
            _disk(d, s * 0.33, s * 0.27, s * 0.165)
            _disk(d, s * 0.43, s * 0.21, s * 0.145, fill=0)
        _outline_cloud(d, s * 0.17, s * 0.46, s * 0.79, s * 0.40, stroke)

    elif name == "cloudy":
        _outline_cloud(d, s * 0.06, s * 0.18, s * 0.58, s * 0.34, stroke * 0.85)
        _outline_cloud(d, s * 0.16, s * 0.42, s * 0.80, s * 0.42, stroke)

    elif name == "fog":
        _outline_cloud(d, s * 0.18, s * 0.04, s * 0.72, s * 0.36, stroke)
        for i in range(3):
            y = s * (0.58 + 0.14 * i)
            x0 = s * (0.16 if i % 2 == 0 else 0.26)
            d.line([x0, y, s - x0, y], fill=INK, width=round(stroke))

    elif name in ("drizzle", "rain", "snow", "thunder"):
        _outline_cloud(d, s * 0.14, s * 0.06, s * 0.76, s * 0.40, stroke)
        if name == "drizzle":
            for i in range(3):
                x = s * (0.30 + 0.20 * i)
                _drop(d, x, s * 0.72, s * 0.035)
                _disk(d, x, s * 0.66, s * 0.035)
        elif name == "rain":
            for i in range(3):
                x = s * (0.28 + 0.22 * i)
                _drop(d, x, s * 0.74, s * 0.055)
        elif name == "snow":
            for i in range(3):
                x = s * (0.28 + 0.22 * i)
                _flake(d, x, s * 0.75, s * 0.115, stroke)
        else:  # thunder
            _bolt(d, s * 0.30, s * 0.50, s * 0.44, s * 0.46)


RENDERERS = {
    name: (lambda d, s, name=name: draw(name, d, s)) for name in ICONS
}


def render(name: str, size: int) -> tuple[bytes, int, int]:
    s = size * SUPERSAMPLE
    img = Image.new("L", (s, s), 0)
    d = ImageDraw.Draw(img)
    RENDERERS[name](d, s)
    img = img.resize((size, size), Image.LANCZOS)
    pixels = img.load()
    rows = bytearray()
    for y in range(size):
        acc = nbits = 0
        for x in range(size):
            acc = (acc << 1) | (1 if pixels[x, y] >= 128 else 0)
            nbits += 1
            if nbits == 8:
                rows.append(acc)
                acc = nbits = 0
        if nbits:
            rows.append(acc << (8 - nbits))
    return bytes(rows), size, size


def camel(name: str) -> str:
    return "".join(part.capitalize() for part in name.split("_"))


def main() -> None:
    global TTF_PATH
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, default=Path("include/fonts/WeatherIcons.h"))
    parser.add_argument("--ttf", type=Path, default=TTF_PATH)
    parser.add_argument("--big", type=int, default=BIG)
    parser.add_argument("--small", type=int, default=SMALL)
    parser.add_argument("--preview-dir", type=Path, default=None,
                        help="optionally write PNG previews for eyeballing")
    args = parser.parse_args()

    args.out.parent.mkdir(parents=True, exist_ok=True)
    if args.preview_dir:
        args.preview_dir.mkdir(parents=True, exist_ok=True)
    TTF_PATH = args.ttf
    if not TTF_PATH.exists():
        raise SystemExit(f"error: font {TTF_PATH} not found (needed for the unknown icon)")

    body: list[str] = []
    for name in ICONS:
        for label, size in (("Big", args.big), ("Small", args.small)):
            data, w, h = render(name, size)
            body.append(f"inline constexpr uint8_t kIcon{camel(name)}{label}[] = {{")
            for i in range(0, len(data), 16):
                body.append("    " + ", ".join(f"0x{b:02X}" for b in data[i:i + 16]) + ",")
            body.append("};")
            body.append("")
            print(f"  {name:<14} {label.lower():<5} {w}x{h}  {len(data)} B")
        if args.preview_dir:
            for label, size in (("big", args.big), ("small", args.small)):
                data, w, h = render(name, size)
                img = Image.new("1", (w, h), 1)
                px = img.load()
                for y in range(h):
                    for x in range(w):
                        if data[y * ((w + 7) // 8) + (x >> 3)] & (0x80 >> (x & 7)):
                            px[x, y] = 0
                img.save(args.preview_dir / f"{name}_{label}.png")

    sets = []
    for name in ICONS:
        cn = camel(name)
        sets.append(f'    {{{{kIcon{cn}Big, {args.big}, {args.big}}}, '
                    f'{{kIcon{cn}Small, {args.small}, {args.small}}}}},  // {name}')

    header = f"""#pragma once

// AUTO-GENERATED by tools/gen_icons.py — do not edit by hand.
//
// Weather icons, 1 bpp, MSB-first, row-major, set bit = ink
// (freeink::ui::BitmapFormat::BW1). Two sizes per icon: {args.big} px for the current block
// and {args.small} px for the three forecast rows.
//
// The array order matches weather::Icon in include/WeatherData.h; the firmware
// static_asserts that the two stay in sync.

#include <stdint.h>

namespace weather {{

struct IconBitmap {{
  const uint8_t* data;
  uint16_t width;
  uint16_t height;
}};

struct IconSet {{
  IconBitmap big;
  IconBitmap small;
}};

{chr(10).join(body)}
inline constexpr IconSet kIcons[] = {{
{chr(10).join(sets)}
}};
inline constexpr uint8_t kIconCount = {len(ICONS)};

}}  // namespace weather
"""
    args.out.write_text(header, encoding="utf-8")
    print(f"wrote {args.out}: {len(ICONS)} icons x 2 sizes")


if __name__ == "__main__":
    main()
