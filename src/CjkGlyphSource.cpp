#include "CjkGlyphSource.h"

namespace weather {

const CjkSizeFont* CjkGlyphSource::fontFor(uint16_t pixelSize) {
  const CjkSizeFont* best = kCjkPairings[0].font;
  uint16_t bestDistance = 0xFFFF;
  for (uint8_t i = 0; i < kCjkPairingCount; ++i) {
    const CjkPairing& pairing = kCjkPairings[i];
    const uint16_t distance =
        pairing.matchYAdvance > pixelSize ? pairing.matchYAdvance - pixelSize : pixelSize - pairing.matchYAdvance;
    if (distance < bestDistance) {
      bestDistance = distance;
      best = pairing.font;
    }
    if (distance == 0) break;
  }
  return best;
}

const CjkGlyph* CjkGlyphSource::find(const CjkSizeFont& font, uint32_t codepoint) {
  // Sorted by codepoint in the generated table; the charset is small, so a
  // branchless binary search is not worth the code size here.
  for (uint16_t i = 0; i < font.count; ++i) {
    if (font.glyphs[i].codepoint == codepoint) return &font.glyphs[i];
  }
  return nullptr;
}

bool CjkGlyphSource::hasGlyph(uint32_t codepoint, uint16_t pixelSize) const {
  const CjkSizeFont* font = fontFor(pixelSize);
  const CjkGlyph* glyph = find(*font, codepoint);
  return glyph != nullptr && glyph->width > 0 && glyph->height > 0;
}

uint16_t CjkGlyphSource::glyphCount(uint16_t pixelSize) const { return fontFor(pixelSize)->count; }

bool CjkGlyphSource::glyph(uint32_t codepoint, uint16_t pixelSize, Glyph* out) {
  if (out == nullptr) return false;
  const CjkSizeFont* font = fontFor(pixelSize);
  const CjkGlyph* glyph = find(*font, codepoint);
  if (glyph == nullptr || glyph->width == 0 || glyph->height == 0) return false;
  if (glyph->width > kMaxGlyphSide || glyph->height > kMaxGlyphSide) return false;

  const uint32_t pixels = static_cast<uint32_t>(glyph->width) * glyph->height;
  const uint8_t* src = font->bitmap + glyph->bitmapOffset;
  for (uint32_t i = 0; i < pixels; ++i) {
    // 1 bpp, MSB-first, row-major (BitmapFont convention) -> 0/255 coverage.
    coverage_[i] = (src[i >> 3] & (0x80u >> (i & 7))) ? 255 : 0;
  }

  out->coverage = coverage_;
  out->width = glyph->width;
  out->height = glyph->height;
  out->xoff = glyph->xOffset;
  out->yoff = glyph->yOffset;
  out->advance = glyph->xAdvance;
  return true;
}

int16_t CjkGlyphSource::advance(uint32_t codepoint, uint16_t pixelSize) {
  const CjkSizeFont* font = fontFor(pixelSize);
  const CjkGlyph* glyph = find(*font, codepoint);
  return glyph != nullptr ? glyph->xAdvance : 0;
}

}  // namespace weather
