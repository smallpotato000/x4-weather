#pragma once

// Renders a UI that is laid out in one logical frame onto a larger frame by
// scaling every drawing coordinate. It exists so the X4's 480x800 layout can be
// shown on the PaperS3's 540x960 portrait frame without touching a single
// layout constant: the weather pages keep one implementation for both boards.
//
// Glyphs are NOT scaled (the fonts stay baked at their pixel sizes), so a scaled
// frame gets slightly more air around the same type; measurements pass through
// unchanged for the same reason, because the layout math that consumes them
// happens in the layout's own coordinate space.

#include <FreeInkUICore.h>

namespace weather {

class ScaledDrawTarget final : public freeink::ui::DrawTarget {
 public:
  ScaledDrawTarget(freeink::ui::DrawTarget& inner, float scale, int16_t offsetX = 0,
                   int16_t offsetY = 0)
      : inner_(inner), scale_(scale), offsetX_(offsetX), offsetY_(offsetY) {}

  freeink::ui::Rect clipRect() const override {
    const freeink::ui::Rect clip = inner_.clipRect();
    return freeink::ui::Rect{unscale(clip.x), unscale(clip.y), unscale(clip.width), unscale(clip.height)};
  }

  bool setClipRect(freeink::ui::Rect rect) override { return inner_.setClipRect(scaleRect(rect)); }

  // Pass-through: the layout measures text in its own (unscaled) space, and the
  // glyphs really are that size on the panel.
  freeink::ui::Size measureText(freeink::ui::FontId font, const char* text,
                                freeink::ui::TextStyle style) const override {
    return inner_.measureText(font, text, style);
  }

  int16_t lineHeight(freeink::ui::FontId font) const override { return inner_.lineHeight(font); }

  freeink::ui::Rect inkBounds(freeink::ui::FontId font, const char* text,
                              freeink::ui::TextStyle style) const override {
    return inner_.inkBounds(font, text, style);
  }

  void fill(freeink::ui::Rect rect, freeink::ui::Paint paint, uint8_t radius = 0,
            uint8_t corners = freeink::ui::CornersAll) override {
    inner_.fill(scaleRect(rect), paint, scaleLength(radius), corners);
  }

  void stroke(freeink::ui::Rect rect, freeink::ui::Paint paint, uint8_t width, uint8_t radius = 0,
              uint8_t corners = freeink::ui::CornersAll) override {
    inner_.stroke(scaleRect(rect), paint, scaleLength(width), scaleLength(radius), corners);
  }

  void line(freeink::ui::Point from, freeink::ui::Point to, uint8_t width,
            freeink::ui::Paint paint) override {
    inner_.line(scalePoint(from), scalePoint(to), scaleLength(width), paint);
  }

  void triangle(freeink::ui::Point a, freeink::ui::Point b, freeink::ui::Point c,
                freeink::ui::Paint paint) override {
    inner_.triangle(scalePoint(a), scalePoint(b), scalePoint(c), paint);
  }

  void text(freeink::ui::Rect rect, const char* text, freeink::ui::TextStyle style) override {
    inner_.text(scaleRect(rect), text, style);
  }

  void bitmap(freeink::ui::Rect rect, freeink::ui::BitmapRef bitmap, freeink::ui::BitmapMode mode,
              freeink::ui::Paint foreground = freeink::ui::Paint::solid(freeink::ui::Color::Black),
              freeink::ui::Rotation rotation = freeink::ui::Rotation::None) override {
    inner_.bitmap(scaleRect(rect), bitmap, mode, foreground, rotation);
  }

 private:
  int16_t scaleValue(int16_t value) const {
    const float scaled = static_cast<float>(value) * scale_ + (value < 0 ? -0.5f : 0.5f);
    return static_cast<int16_t>(scaled);
  }

  int16_t unscale(int16_t value) const {
    if (scale_ <= 0.0f) return value;
    return static_cast<int16_t>(static_cast<float>(value) / scale_ + 0.5f);
  }

  // A stroke or radius never collapses to nothing.
  uint8_t scaleLength(uint8_t value) const {
    if (value == 0) return 0;
    const int16_t scaled = scaleValue(value);
    return static_cast<uint8_t>(scaled < 1 ? 1 : scaled);
  }

  int16_t scaleX(int16_t x) const { return static_cast<int16_t>(offsetX_ + scaleValue(x)); }
  int16_t scaleY(int16_t y) const { return static_cast<int16_t>(offsetY_ + scaleValue(y)); }

  freeink::ui::Rect scaleRect(freeink::ui::Rect rect) const {
    return freeink::ui::Rect{scaleX(rect.x), scaleY(rect.y), scaleValue(rect.width),
                             scaleValue(rect.height)};
  }

  freeink::ui::Point scalePoint(freeink::ui::Point point) const {
    return freeink::ui::Point{scaleX(point.x), scaleY(point.y)};
  }

  freeink::ui::DrawTarget& inner_;
  float scale_;
  int16_t offsetX_;
  int16_t offsetY_;
};

}  // namespace weather
