#!/usr/bin/env python3
"""Generate every bitmap font the X4 weather UI renders with.

The FreeInkUI `DisplayTarget` bundles one ASCII font (Noto Sans, 24 px). This
project adds:

  * fonts/WeatherTitleFont.h  — ASCII 34 px (header band, section headings)
  * fonts/WeatherDigitsFont.h — ASCII 112 px (the big current temperature)
  * fonts/WeatherCjkFont.h    — sparse CJK/°C glyphs at 24 px and 34 px, exposed
                                through the FreeInkUI `RuntimeGlyphSource`
                                fallback (see src/CjkGlyphSource.*)

The dense ASCII fonts are produced by the SDK's own tools/gen_font.py so the
byte layout is exactly what `DisplayTarget` expects. The sparse CJK font needs a
non-contiguous codepoint set, which gen_font.py cannot express, so this script
rasterizes it with the same conventions and emits a lookup table instead of a
dense range.

The character set is collected by scanning the project sources for non-ASCII
characters (so every label the UI prints is covered automatically) plus an
optional extra-charset file — put city names or extra wording there.

Usage:
    python3 tools/gen_fonts.py \
        --ttf tools/fonts/NotoSansSC-VF.ttf \
        --outdir include/fonts \
        --scan src include \
        --extra tools/charset_extra.txt \
        --sdk ../freeink-sdk
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

SOURCE_EXTS = {".h", ".hpp", ".c", ".cc", ".cpp"}
# Skip characters we would never want baked (emoji, box drawing, ...).
SKIP_RANGES = ((0x1F000, 0x1FAFF), (0x2500, 0x25FF), (0xE000, 0xF8FF))


def c_ident(name: str) -> str:
    ident = "".join(ch for ch in name if ch.isalnum())
    return ident or "Custom"


def string_literals(text: str) -> set[str]:
    """Non-ASCII characters that appear inside C/C++ string literals.

    Comments are skipped on purpose: a Chinese comment should not cost flash in
    the baked glyph table. Everything the firmware can actually print lives in a
    string literal (or comes from --extra, e.g. a WEATHER_PLACE macro value)."""
    chars: set[str] = set()
    index = 0
    length = len(text)
    in_line_comment = in_block_comment = in_string = False
    while index < length:
        ch = text[index]
        if in_line_comment:
            in_line_comment = ch != "\n"
            index += 1
        elif in_block_comment:
            if ch == "*" and index + 1 < length and text[index + 1] == "/":
                in_block_comment = False
                index += 2
            else:
                index += 1
        elif in_string:
            if ch == "\\":
                index += 2
                continue
            if ch == '"':
                in_string = False
                index += 1
                continue
            if ord(ch) > 0x7F:
                chars.add(ch)
            index += 1
        elif ch == "/" and text.startswith("//", index):
            in_line_comment = True
            index += 2
        elif ch == "/" and text.startswith("/*", index):
            in_block_comment = True
            index += 2
        elif ch == '"':
            in_string = True
            index += 1
        else:
            index += 1
    return chars


def collect_chars(paths: list[Path], extra: Path | None) -> list[str]:
    chars: set[str] = {"°"}
    for root in paths:
        files = [root] if root.is_file() else sorted(p for p in root.rglob("*") if p.suffix in SOURCE_EXTS)
        for path in files:
            text = path.read_text(encoding="utf-8", errors="replace")
            chars |= string_literals(text)
    if extra is not None and extra.exists():
        for ch in extra.read_text(encoding="utf-8"):
            if ord(ch) > 0x7F:
                chars.add(ch)
    kept = []
    for ch in chars:
        cp = ord(ch)
        if any(lo <= cp <= hi for lo, hi in SKIP_RANGES):
            continue
        if ch.isspace():
            continue
        kept.append(ch)
    return sorted(kept, key=ord)


def generate_dense(sdk: Path, ttf: Path, size: int, name: str, first: int, last: int, out: Path) -> None:
    tool = sdk / "libs" / "ui" / "FreeInkUI" / "tools" / "gen_font.py"
    if not tool.exists():
        sys.exit(f"error: {tool} not found (pass --sdk pointing at the FreeInk checkout)")
    cmd = [
        sys.executable, str(tool),
        "--ttf", str(ttf), "--size", str(size), "--name", name,
        "--first", hex(first), "--last", hex(last), "--out", str(out),
    ]
    print(f"  dense  U+{first:04X}..U+{last:04X} @ {size}px -> {out.name}")
    subprocess.run(cmd, check=True)


def rasterize(font: ImageFont.FreeTypeFont, ch: str, ascent: int):
    """Rasterize one glyph to MSB-first 1-bit rows (gen_font.py conventions)."""
    bbox = font.getbbox(ch)
    advance = round(font.getlength(ch))
    if bbox is None or bbox[2] - bbox[0] <= 0 or bbox[3] - bbox[1] <= 0:
        return b"", 0, 0, advance, 0, 0
    left, top, right, bottom = bbox
    img = Image.new("1", (right + 2, bottom + 2), 0)
    ImageDraw.Draw(img).text((0, 0), ch, font=font, fill=1)
    crop = img.crop((left, top, right, bottom))
    width, height = crop.size
    pixels = crop.load()
    bitmap = bytearray()
    acc = nbits = 0
    for y in range(height):
        for x in range(width):
            acc = (acc << 1) | (1 if pixels[x, y] else 0)
            nbits += 1
            if nbits == 8:
                bitmap.append(acc)
                acc = nbits = 0
    if nbits:
        bitmap.append(acc << (8 - nbits))
    return bytes(bitmap), width, height, advance, left, top - ascent


def generate_sparse(ttf: Path, sizes: list[tuple[int, int]], chars: list[str], name: str, out: Path) -> list[tuple[int, int]]:
    """Emit a sparse (codepoint -> glyph) font per size.

    `sizes` is a list of (nominal_px, match_y_advance) pairs: match_y_advance is
    the yAdvance of the dense font the glyphs are mixed with, i.e. the
    `pixelSize` DisplayTarget passes to the RuntimeGlyphSource.
    """
    sym = c_ident(name)
    bodies: list[str] = []
    pairings: list[tuple[int, int]] = []
    empty: list[tuple[int, str]] = []
    for index, (size, match) in enumerate(sizes):
        font = ImageFont.truetype(str(ttf), size)
        ascent, descent = font.getmetrics()
        y_advance = ascent + descent
        pairings.append((match, index))
        bitmap = bytearray()
        glyphs = []
        for ch in chars:
            data, w, h, advance, xoff, yoff = rasterize(font, ch, ascent)
            if w and h:
                offset = len(bitmap)
                if offset > 0xFFFF:
                    sys.exit(f"error: {sym}{size} bitmap offset overflows uint16 — split the charset")
                bitmap.extend(data)
            else:
                offset = 0
                empty.append((size, ch))
            glyphs.append((ch, offset, w, h, advance, xoff, yoff))
        print(f"  sparse {len(chars)} glyphs @ {size}px -> {sym}{size} ({len(bitmap)} B)")
        lines = [f"inline constexpr uint8_t {sym}{size}Bitmap[] = {{"]
        for i in range(0, len(bitmap), 16):
            lines.append("    " + ", ".join(f"0x{b:02X}" for b in bitmap[i:i + 16]) + ",")
        lines.append("};")
        lines.append("")
        lines.append(f"inline constexpr CjkGlyph {sym}{size}Glyphs[] = {{")
        for (ch, offset, w, h, advance, xoff, yoff) in glyphs:
            lines.append(
                f"    {{0x{ord(ch):04X}, {offset}, {w}, {h}, {advance}, {xoff}, {yoff}}},  // {ch}"
            )
        lines.append("};")
        lines.append("")
        lines.append(
            f"inline constexpr CjkSizeFont {sym}{size} = {{{size}, {ascent}, {y_advance}, {len(glyphs)}, "
            f"{sym}{size}Bitmap, {sym}{size}Glyphs}};"
        )
        bodies.append("\n".join(lines))

    pairing_lines = []
    for match, index in pairings:
        size = sizes[index][0]
        pairing_lines.append(f"    {{{match}, &{sym}{size}}},  // mixed with a font whose yAdvance is {match}")

    header = f"""#pragma once

// AUTO-GENERATED by tools/gen_fonts.py — do not edit by hand.
//
// Sparse CJK / unit glyphs for the weather UI. Each glyph is 1bpp, MSB-first,
// row-major, exactly like freeink::ui::BitmapFont; unlike a BitmapFont the
// codepoints are sparse, so lookups go through the sorted table per size.
// Consumed by weather::CjkGlyphSource, the DisplayTarget RuntimeGlyphSource
// fallback (src/CjkGlyphSource.cpp).
//
// Source: {ttf.name} (SIL Open Font License 1.1). Rasterized bitmaps only.

#include <stdint.h>

namespace weather {{

struct CjkGlyph {{
  uint32_t codepoint;
  uint16_t bitmapOffset;  // byte offset into the size font's bitmap blob
  uint8_t width;
  uint8_t height;
  uint8_t xAdvance;
  int8_t xOffset;  // upper-left corner relative to the pen origin
  int8_t yOffset;  // relative to the baseline (negative = above)
}};

struct CjkSizeFont {{
  uint8_t nominalSize;  // px the glyphs were rasterized at
  uint8_t ascent;
  uint8_t yAdvance;
  uint16_t count;
  const uint8_t* bitmap;
  const CjkGlyph* glyphs;
}};

// Which sparse size backs which dense font, keyed by the dense font's yAdvance
// (the `pixelSize` DisplayTarget hands to RuntimeGlyphSource).
struct CjkPairing {{
  uint16_t matchYAdvance;
  const CjkSizeFont* font;
}};

{chr(10).join(bodies)}

inline constexpr CjkPairing kCjkPairings[] = {{
{chr(10).join(pairing_lines)}
}};
inline constexpr uint8_t kCjkPairingCount = {len(pairings)};

}}  // namespace weather
"""
    out.write_text(header, encoding="utf-8")
    print(f"  sparse -> {out.name}")
    if empty:
        # An empty glyph draws a "tofu" box on the panel: fail loudly instead.
        for size, ch in empty:
            print(f"error: {ch!r} (U+{ord(ch):04X}) has no glyph at {size}px in {ttf.name}")
        sys.exit("error: the source font does not cover every character in the charset "
                 "(remove it from the sources/charset_extra.txt, or use a font that has it)")
    return pairings


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--ttf", required=True, type=Path)
    parser.add_argument("--outdir", required=True, type=Path)
    parser.add_argument("--sdk", type=Path, default=Path("../freeink-sdk"))
    parser.add_argument("--scan", nargs="*", default=["src", "include", "tools/preview"], type=Path)
    parser.add_argument("--extra", type=Path, default=None, help="file with extra characters (city names, ...)")
    parser.add_argument("--title-size", type=int, default=34)
    parser.add_argument("--digit-size", type=int, default=112)
    args = parser.parse_args()

    if not args.ttf.exists():
        sys.exit(f"error: font {args.ttf} not found")
    args.outdir.mkdir(parents=True, exist_ok=True)

    print(f"source font: {args.ttf}")
    print("dense fonts:")
    generate_dense(args.sdk, args.ttf, args.title_size, "WeatherTitle", 0x20, 0x7E,
                   args.outdir / "WeatherTitleFont.h")
    generate_dense(args.sdk, args.ttf, args.digit_size, "WeatherDigits", 0x20, 0x3A,
                   args.outdir / "WeatherDigitsFont.h")

    chars = collect_chars(args.scan, args.extra)
    print(f"sparse charset: {len(chars)} non-ASCII glyphs")
    print("  " + " ".join(chars))

    # The title font's yAdvance is what pairs with the 34 px CJK glyphs. Read it
    # back from the generated header instead of guessing.
    title_header = (args.outdir / "WeatherTitleFont.h").read_text(encoding="utf-8")
    match = re.search(r"kWeatherTitleFont = \{(.*?)\};", title_header, re.DOTALL)
    if match is None:
        sys.exit("error: could not read the generated title font metrics")
    fields = [f.strip() for f in match.group(1).split(",")]
    y_advance_title = int(fields[4])
    y_advance_bundled = 34  # freeink::ui::kNotoSansFont (FreeInkUI, 24 px design)

    print("sparse fonts:")
    generate_sparse(
        args.ttf,
        [(24, y_advance_bundled), (args.title_size, y_advance_title)],
        chars,
        "WeatherCjk",
        args.outdir / "WeatherCjkFont.h",
    )
    print(f"paired: yAdvance {y_advance_bundled} -> 24 px, yAdvance {y_advance_title} -> {args.title_size} px")


if __name__ == "__main__":
    main()
