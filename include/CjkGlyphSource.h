#pragma once

// RuntimeGlyphSource that renders the CJK / unit glyphs FreeInkUI's bundled
// Noto Sans bitmap font does not contain (it covers U+0020..U+007E only).
//
// The glyphs come from the generated sparse font (include/fonts/WeatherCjkFont.h,
// produced by tools/gen_fonts.py): one baked bitmap per codepoint per size, plus
// a pairing table that maps the *dense* font a slot uses onto the right baked
// size. DisplayTarget calls glyph() with `pixelSize` = the slot font's yAdvance,
// so the pairing is exact and CJK text matches the Latin it sits next to.

#include <FreeInkUIDisplayTarget.h>

#include "fonts/WeatherCjkFont.h"

namespace weather {

class CjkGlyphSource final : public freeink::ui::RuntimeGlyphSource {
 public:
  bool glyph(uint32_t codepoint, uint16_t pixelSize, Glyph* out) override;
  int16_t advance(uint32_t codepoint, uint16_t pixelSize) override;

  // Number of baked glyphs available for the size DisplayTarget asks for.
  uint16_t glyphCount(uint16_t pixelSize) const;
  // True when `codepoint` can be rendered at that size (used by the host tests
  // and to warn about a missing label instead of silently drawing a box).
  bool hasGlyph(uint32_t codepoint, uint16_t pixelSize) const;

 private:
  static const CjkSizeFont* fontFor(uint16_t pixelSize);
  static const CjkGlyph* find(const CjkSizeFont& font, uint32_t codepoint);

  // 1 bpp -> 8-bit coverage scratch for the glyph currently being drawn.
  // DisplayTarget consumes the returned pointer before asking for the next one.
  static constexpr uint16_t kMaxGlyphSide = 48;
  uint8_t coverage_[kMaxGlyphSide * kMaxGlyphSide] = {0};
};

}  // namespace weather
