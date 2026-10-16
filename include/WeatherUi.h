#pragma once

// Portrait weather screen: draws the whole 480x800 logical frame through a
// FreeInkUI DrawTarget. Freestanding (no Arduino dependency) so the identical
// code runs on the panel and in the host preview renderer
// (tools/preview/render_preview.cpp).
//
// Three pages share one header/footer chrome:
//   Now        — current conditions + the three-day rows (the standing image)
//   Hourly     — the next 12 hours, one row each
//   AirQuality — European AQI + the main pollutants
//   Crypto     — BTC/ETH/XRP/BNB/SOL/TRX/XMR vs USD with the 24 h change

#include <FreeInkUIDisplayTarget.h>

#include "CjkGlyphSource.h"
#include "WeatherData.h"

namespace weather {

// Logical font slots the screen uses; wire them up with
// configureWeatherFonts() before the first render.
static constexpr freeink::ui::FontId kFontBody = 0;   // bundled Noto Sans, 24 px
static constexpr freeink::ui::FontId kFontTitle = 1;  // WeatherTitle, 34 px
static constexpr freeink::ui::FontId kFontBig = 2;    // WeatherDigits, 112 px

// Display order: the crypto board first (it is the page the device wakes up on),
// then the weather pages. References elsewhere are by name, so this order is the
// single place to change it.
enum class Page : uint8_t {
  Crypto = 0,
  Now = 1,
  Hourly = 2,
  AirQuality = 3,
};
static constexpr uint8_t kPageCount = 4;

// Point the slots at the generated fonts and install the CJK fallback.
void configureWeatherFonts(freeink::ui::DisplayTarget& target, CjkGlyphSource& fallback);

struct ScreenState {
  const WeatherReport* report = nullptr;
  const char* place = nullptr;   // header label when the report has no place yet
  const char* status = nullptr;  // footer line (UTF-8), may be nullptr
  Page page = Page::Crypto;
  CryptoSort cryptoSort = CryptoSort::Table;  // row order on the crypto page
  bool batteryKnown = false;     // header battery: read from the board's ADC
  uint8_t batteryPercent = 0;
  uint8_t coinMask = 0x7F;       // bit i = draw coin i (settings-driven)
  const char* hint = nullptr;    // footer hint; defaults to the paging hint
  bool busy = false;             // first fetch in flight -> placeholder screen
  bool interactive = false;      // buttons are live -> show the paging hint
};

// Repaints the full screen. The caller owns the panel refresh.
void renderWeatherScreen(freeink::ui::DrawTarget& target, const ScreenState& state);

// Page cycling helpers for the button handling in main.cpp.
Page nextPage(Page page);
Page previousPage(Page page);

}  // namespace weather
