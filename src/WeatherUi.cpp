#include "WeatherUi.h"

#include <stdio.h>
#include <string.h>

#include "fonts/WeatherDigitsFont.h"
#include "fonts/WeatherIcons.h"
#include "fonts/WeatherTitleFont.h"

namespace weather {
namespace {

using freeink::ui::BitmapFormat;
using freeink::ui::BitmapMode;
using freeink::ui::BitmapRef;
using freeink::ui::Color;
using freeink::ui::DisplayTarget;
using freeink::ui::FontId;
using freeink::ui::Paint;
using freeink::ui::Rect;
using freeink::ui::TextAlign;
using freeink::ui::TextStyle;

// Logical portrait frame: the panel is 800x480 landscape and DisplayTarget
// rotates 90 deg CW into it (the default orientation for landscape panels).
constexpr int16_t kWidth = 480;
constexpr int16_t kHeight = 800;

constexpr int16_t kMargin = 24;
constexpr int16_t kHeaderBottom = 100;
constexpr int16_t kFooterBaseline = 782;

constexpr int16_t kRowHeight = 80;
constexpr int16_t kRowsTop = 520;

constexpr int16_t kHourRowsTop = 182;
constexpr int16_t kHourRowHeight = 46;

constexpr int16_t kPollutantRowsTop = 470;
constexpr int16_t kPollutantRowHeight = 62;

constexpr int16_t kCoinRowsTop = 178;
constexpr int16_t kCoinRowHeight = 82;

static_assert(kIconCount == static_cast<uint8_t>(Icon::Thunder) + 1,
              "WeatherIcons.h order must match weather::Icon");
static_assert(kForecastDays == 3, "the Now page reserves exactly three forecast rows");

// One line of text placed by its baseline. layoutText() vertically centers the
// line block in the rect, so passing a rect exactly one line tall pins the
// baseline to rect.y + font.ascent.
void drawLine(DisplayTarget& target, const freeink::ui::BitmapFont& font, FontId slot, int16_t x,
              int16_t baseline, const char* text, TextAlign align, int16_t boxWidth, Color color) {
  if (text == nullptr || text[0] == '\0') return;
  TextStyle style;
  style.font = slot;
  style.align = align;
  style.color = color;
  style.maxLines = 1;
  const Rect rect{x, static_cast<int16_t>(baseline - font.ascent), boxWidth,
                  static_cast<int16_t>(font.yAdvance)};
  target.text(rect, text, style);
}

int16_t textWidth(DisplayTarget& target, FontId slot, const char* text) {
  TextStyle style;
  style.font = slot;
  return target.measureText(slot, text, style).width;
}

void drawIcon(DisplayTarget& target, Icon icon, const Rect& rect, bool small) {
  const IconBitmap& chosen = small ? kIcons[static_cast<uint8_t>(icon)].small : kIcons[static_cast<uint8_t>(icon)].big;
  const BitmapRef ref{chosen.data, chosen.width, chosen.height, BitmapFormat::BW1, false};
  target.bitmap(rect, ref, BitmapMode::Contain, Paint::solid(Color::Black));
}

// A dashed separator: cheaper on the eye (and on the panel) than a solid rule.
void drawDashedRule(DisplayTarget& target, int16_t y, int16_t x0, int16_t x1) {
  const Paint ink = Paint::solid(Color::Black);
  for (int16_t x = x0; x < x1; x += 12) {
    target.line(freeink::ui::Point{x, y}, freeink::ui::Point{static_cast<int16_t>(x + 5), y}, 2, ink);
  }
}

void copyIsoDate(const char* iso, char* out, size_t outLen) {
  out[0] = '\0';
  if (iso == nullptr || strlen(iso) < 10 || outLen < 11) return;
  memcpy(out, iso, 10);
  out[10] = '\0';
}

// "体感 19.7°"
void formatFeels(float celsius, char* out, size_t outLen) {
  char value[16];
  formatTemp1(celsius, value, sizeof(value));
  snprintf(out, outLen, "体感 %s°", value);
}

// "15.9° / 22.6°"
void formatRange(float tMin, float tMax, char* out, size_t outLen) {
  char low[16];
  char high[16];
  formatTemp1(tMin, low, sizeof(low));
  formatTemp1(tMax, high, sizeof(high));
  snprintf(out, outLen, "%s° / %s°", low, high);
}

const char* pageName(Page page) {
  switch (page) {
    case Page::Hourly: return "未来 12 小时";
    case Page::AirQuality: return "空气质量";
    case Page::Crypto: return "数字货币";
    default: return "";
  }
}

// Small direction marker for a 24 h change: filled pointing up when the price
// rose, down when it fell, a dash when it barely moved.
void drawTrendMarker(DisplayTarget& target, int16_t centerX, int16_t centerY, float changePercent) {
  const Paint ink = Paint::solid(Color::Black);
  if (changePercent > 0.05f) {
    target.triangle(freeink::ui::Point{centerX, static_cast<int16_t>(centerY - 6)},
                    freeink::ui::Point{static_cast<int16_t>(centerX - 7), static_cast<int16_t>(centerY + 5)},
                    freeink::ui::Point{static_cast<int16_t>(centerX + 7), static_cast<int16_t>(centerY + 5)}, ink);
  } else if (changePercent < -0.05f) {
    target.triangle(freeink::ui::Point{centerX, static_cast<int16_t>(centerY + 6)},
                    freeink::ui::Point{static_cast<int16_t>(centerX - 7), static_cast<int16_t>(centerY - 5)},
                    freeink::ui::Point{static_cast<int16_t>(centerX + 7), static_cast<int16_t>(centerY - 5)}, ink);
  } else {
    target.line(freeink::ui::Point{static_cast<int16_t>(centerX - 6), centerY},
                freeink::ui::Point{static_cast<int16_t>(centerX + 6), centerY}, 2, ink);
  }
}

// --- shared chrome ----------------------------------------------------------

void drawHeader(DisplayTarget& target, const WeatherReport* report, const char* place) {
  target.fill(Rect{0, 0, kWidth, kHeaderBottom}, Paint::solid(Color::Black));

  const char* label = nullptr;
  if (report != nullptr && report->place[0] != '\0') {
    label = report->place;
  } else if (place != nullptr && place[0] != '\0') {
    label = place;
  } else {
    label = "天气";
  }
  drawLine(target, freeink::ui::kWeatherTitleFont, kFontTitle, kMargin, 62, label, TextAlign::Left, 260,
           Color::White);

  const char* observed = nullptr;
  if (report != nullptr && report->current.valid && report->current.time[0] != '\0') {
    observed = report->current.time;
  } else if (report != nullptr && report->air.valid && report->air.time[0] != '\0') {
    observed = report->air.time;
  }
  if (observed != nullptr) {
    char date[11];
    char shortDate[8];
    char dateLabel[32];
    copyIsoDate(observed, date, sizeof(date));
    formatShortDate(date, shortDate, sizeof(shortDate));
    snprintf(dateLabel, sizeof(dateLabel), "%s %s", shortDate, weekdayName(weekdayFromIsoDate(date)));
    drawLine(target, freeink::ui::kNotoSansFont, kFontBody, kWidth - kMargin - 170, 62, dateLabel, TextAlign::Right,
             170, Color::White);
  }
}

void drawFooter(DisplayTarget& target, const ScreenState& state) {
  if (state.status != nullptr && state.status[0] != '\0') {
    drawLine(target, freeink::ui::kNotoSansFont, kFontBody, kMargin, kFooterBaseline, state.status,
             TextAlign::Left, 240, Color::Black);
  }
  char indicator[48];
  if (state.interactive) {
    snprintf(indicator, sizeof(indicator), "%u/%u %s", static_cast<unsigned>(state.page) + 1,
             static_cast<unsigned>(kPageCount), state.hint != nullptr ? state.hint : "左右切页");
  } else {
    snprintf(indicator, sizeof(indicator), "%u/%u", static_cast<unsigned>(state.page) + 1,
             static_cast<unsigned>(kPageCount));
  }
  drawLine(target, freeink::ui::kNotoSansFont, kFontBody, kWidth - kMargin - 240, kFooterBaseline, indicator,
           TextAlign::Right, 240, Color::Black);
}

// A big figure (digits font) centred on its own, with an optional unit drawn in
// the title font right after it, sharing the baseline.
void drawFigure(DisplayTarget& target, const char* figure, const char* unit, int16_t baseline) {
  const int16_t figureWidth = textWidth(target, kFontBig, figure);
  const int16_t unitWidth = unit != nullptr ? textWidth(target, kFontTitle, unit) : 0;
  const int16_t gap = 8;
  const int16_t total = static_cast<int16_t>(figureWidth + (unit != nullptr ? gap + unitWidth : 0));
  const int16_t x0 = static_cast<int16_t>((kWidth - total) / 2);
  drawLine(target, freeink::ui::kWeatherDigitsFont, kFontBig, x0, baseline, figure, TextAlign::Left, figureWidth,
           Color::Black);
  if (unit != nullptr) {
    drawLine(target, freeink::ui::kWeatherTitleFont, kFontTitle, static_cast<int16_t>(x0 + figureWidth + gap),
             baseline, unit, TextAlign::Left, unitWidth, Color::Black);
  }
}

void drawMetricsRow(DisplayTarget& target, const CurrentWeather& current, int16_t baseline) {
  char feels[32];
  char humidity[24];
  char wind[32];
  char value[16];

  formatFeels(current.apparentC, feels, sizeof(feels));
  snprintf(humidity, sizeof(humidity), "湿度 %u%%", static_cast<unsigned>(current.humidity));
  snprintf(wind, sizeof(wind), "风速 %skm/h", formatWhole(current.windKph, value, sizeof(value)));

  struct Column {
    const char* text;
    int16_t x;
    int16_t width;
  };
  const Column columns[3] = {
      {feels, 8, 155},
      {humidity, 163, 155},
      {wind, 318, 154},
  };
  for (const Column& column : columns) {
    drawLine(target, freeink::ui::kNotoSansFont, kFontBody, column.x, baseline, column.text, TextAlign::Center,
             column.width, Color::Black);
  }
}

// --- pages ------------------------------------------------------------------

void drawNowPage(DisplayTarget& target, const WeatherReport& report) {
  const CurrentWeather& current = report.current;

  drawIcon(target, conditionIcon(current.code, current.isDay), Rect{176, 128, 128, 128}, false);

  char number[16];
  formatTemp1(current.temperatureC, number, sizeof(number));
  drawFigure(target, number, "°C", 350);

  drawLine(target, freeink::ui::kWeatherTitleFont, kFontTitle, 0, 424, conditionText(current.code),
           TextAlign::Center, kWidth, Color::Black);

  drawMetricsRow(target, current, 478);

  for (uint8_t i = 0; i < kForecastDays; ++i) {
    const int16_t rowTop = static_cast<int16_t>(kRowsTop + i * kRowHeight);
    if (i > 0) drawDashedRule(target, static_cast<int16_t>(rowTop - 6), kMargin, kWidth - kMargin);

    const DailyForecast& day = report.days[i];
    drawLine(target, freeink::ui::kWeatherTitleFont, kFontTitle, kMargin, static_cast<int16_t>(rowTop + 30),
             dayLabel(i), TextAlign::Left, 110, Color::Black);

    if (!day.valid) {
      drawLine(target, freeink::ui::kNotoSansFont, kFontBody, 150, static_cast<int16_t>(rowTop + 48), "无数据",
               TextAlign::Left, 200, Color::Black);
      continue;
    }

    char date[11];
    char shortDate[8];
    char subLabel[32];
    copyIsoDate(day.date, date, sizeof(date));
    formatShortDate(date, shortDate, sizeof(shortDate));
    snprintf(subLabel, sizeof(subLabel), "%s %s", shortDate, weekdayName(weekdayFromIsoDate(date)));
    drawLine(target, freeink::ui::kNotoSansFont, kFontBody, kMargin, static_cast<int16_t>(rowTop + 66), subLabel,
             TextAlign::Left, 120, Color::Black);

    drawIcon(target, conditionIcon(day.code, true), Rect{168, static_cast<int16_t>(rowTop + 12), 56, 56}, true);

    drawLine(target, freeink::ui::kNotoSansFont, kFontBody, 232, static_cast<int16_t>(rowTop + 44),
             conditionText(day.code), TextAlign::Left, 90, Color::Black);

    char range[32];
    formatRange(day.tMin, day.tMax, range, sizeof(range));
    drawLine(target, freeink::ui::kNotoSansFont, kFontBody, kWidth - kMargin - 170, static_cast<int16_t>(rowTop + 44),
             range, TextAlign::Right, 170, Color::Black);
  }
}

void drawHourlyPage(DisplayTarget& target, const WeatherReport& report) {
  drawLine(target, freeink::ui::kWeatherTitleFont, kFontTitle, kMargin, 150, pageName(Page::Hourly),
           TextAlign::Left, 300, Color::Black);

  if (report.hourCount == 0) {
    drawLine(target, freeink::ui::kNotoSansFont, kFontBody, 0, 400, "暂无逐小时数据", TextAlign::Center, kWidth,
             Color::Black);
    return;
  }

  for (uint8_t i = 0; i < report.hourCount && i < kForecastHours; ++i) {
    const HourlyForecast& hour = report.hours[i];
    const int16_t baseline = static_cast<int16_t>(kHourRowsTop + i * kHourRowHeight + 34);
    if (i > 0 && i % 3 == 0) {
      drawDashedRule(target, static_cast<int16_t>(kHourRowsTop + i * kHourRowHeight - 4), kMargin, kWidth - kMargin);
    }

    char label[8];
    formatHourLabel(hour.time, label, sizeof(label));
    drawLine(target, freeink::ui::kNotoSansFont, kFontBody, kMargin, baseline, label, TextAlign::Left, 76,
             Color::Black);

    drawIcon(target, conditionIcon(hour.code, true), Rect{112, static_cast<int16_t>(baseline - 30), 36, 36}, true);

    drawLine(target, freeink::ui::kNotoSansFont, kFontBody, 160, baseline, conditionText(hour.code),
             TextAlign::Left, 100, Color::Black);

    if (hour.precipitationChance > 0) {
      char rain[16];
      snprintf(rain, sizeof(rain), "%u%%", static_cast<unsigned>(hour.precipitationChance));
      drawLine(target, freeink::ui::kNotoSansFont, kFontBody, 268, baseline, rain, TextAlign::Right, 64,
               Color::Black);
    }

    char temperature[16];
    char value[20];
    snprintf(value, sizeof(value), "%s°", formatTemp1(hour.temperatureC, temperature, sizeof(temperature)));
    drawLine(target, freeink::ui::kNotoSansFont, kFontBody, kWidth - kMargin - 110, baseline, value,
             TextAlign::Right, 110, Color::Black);
  }
}

void drawAirQualityScale(DisplayTarget& target, int europeanAqi, int16_t y) {
  const uint8_t level = aqiLevelIndex(europeanAqi);
  constexpr int16_t kSegments = 6;
  constexpr int16_t kGap = 6;
  const int16_t segmentWidth = static_cast<int16_t>((kWidth - 2 * kMargin - (kSegments - 1) * kGap) / kSegments);
  for (int16_t i = 0; i < kSegments; ++i) {
    const int16_t x = static_cast<int16_t>(kMargin + i * (segmentWidth + kGap));
    const Rect segment{x, y, segmentWidth, 22};
    if (i <= static_cast<int16_t>(level)) {
      target.fill(segment, Paint::solid(Color::Black), 4);
    } else {
      target.stroke(segment, Paint::solid(Color::Black), 2, 4);
    }
  }
}

void drawPollutantRow(DisplayTarget& target, int16_t y, const char* label, float value) {
  drawLine(target, freeink::ui::kNotoSansFont, kFontBody, kMargin, static_cast<int16_t>(y + 34), label,
           TextAlign::Left, 160, Color::Black);
  char number[16];
  char text[32];
  snprintf(text, sizeof(text), "%s µg/m³", formatTemp1(value, number, sizeof(number)));
  drawLine(target, freeink::ui::kNotoSansFont, kFontBody, kWidth - kMargin - 240, static_cast<int16_t>(y + 34), text,
           TextAlign::Right, 240, Color::Black);
}

void drawAirQualityPage(DisplayTarget& target, const WeatherReport& report) {
  drawLine(target, freeink::ui::kWeatherTitleFont, kFontTitle, kMargin, 150, pageName(Page::AirQuality),
           TextAlign::Left, 300, Color::Black);

  if (!report.air.valid) {
    drawLine(target, freeink::ui::kNotoSansFont, kFontBody, 0, 400, "空气质量数据获取中…", TextAlign::Center,
             kWidth, Color::Black);
    return;
  }

  const AirQuality& air = report.air;
  char figure[16];
  snprintf(figure, sizeof(figure), "%d", air.europeanAqi);

  const int16_t figureWidth = textWidth(target, kFontBig, figure);
  const int16_t levelWidth = textWidth(target, kFontTitle, aqiLevelText(air.europeanAqi));
  const int16_t total = static_cast<int16_t>(figureWidth + 16 + levelWidth);
  const int16_t x0 = static_cast<int16_t>((kWidth - total) / 2);
  constexpr int16_t kAqiBaseline = 330;
  drawLine(target, freeink::ui::kWeatherDigitsFont, kFontBig, x0, kAqiBaseline, figure, TextAlign::Left,
           figureWidth, Color::Black);
  drawLine(target, freeink::ui::kWeatherTitleFont, kFontTitle, static_cast<int16_t>(x0 + figureWidth + 16),
           kAqiBaseline, aqiLevelText(air.europeanAqi), TextAlign::Left, levelWidth, Color::Black);

  drawLine(target, freeink::ui::kNotoSansFont, kFontBody, 0, 386, "空气质量指数 EAQI", TextAlign::Center, kWidth,
           Color::Black);
  drawAirQualityScale(target, air.europeanAqi, 404);

  drawPollutantRow(target, kPollutantRowsTop + 0 * kPollutantRowHeight, "PM2.5", air.pm25);
  drawPollutantRow(target, kPollutantRowsTop + 1 * kPollutantRowHeight, "PM10", air.pm10);
  drawPollutantRow(target, kPollutantRowsTop + 2 * kPollutantRowHeight, "臭氧", air.ozone);
  drawPollutantRow(target, kPollutantRowsTop + 3 * kPollutantRowHeight, "二氧化氮", air.no2);
}

void drawSparkline(DisplayTarget& target, const CoinQuote& quote, const Rect& box);

void drawCryptoPage(DisplayTarget& target, const ScreenState& state) {
  const WeatherReport& report = *state.report;
  drawLine(target, freeink::ui::kWeatherTitleFont, kFontTitle, kMargin, 150, pageName(Page::Crypto),
           TextAlign::Left, 300, Color::Black);
  drawLine(target, freeink::ui::kNotoSansFont, kFontBody, kWidth - kMargin - 150, 150, "兑美元 USD",
           TextAlign::Right, 150, Color::Black);

  if (!report.crypto.ok) {
    drawLine(target, freeink::ui::kNotoSansFont, kFontBody, 0, 400, "行情获取失败", TextAlign::Center, kWidth,
             Color::Black);
    return;
  }

  // Row order: table, gainers first or losers first. Coins without a quote keep
  // their table position at the end.
  uint8_t order[kCoinCount];
  for (uint8_t i = 0; i < kCoinCount; ++i) order[i] = i;
  if (state.cryptoSort != CryptoSort::Table) {
    const bool descending = state.cryptoSort == CryptoSort::Gainers;
    for (uint8_t i = 1; i < kCoinCount; ++i) {
      const uint8_t current = order[i];
      const CoinQuote& quote = report.crypto.coins[current];
      int8_t j = static_cast<int8_t>(i - 1);
      while (j >= 0) {
        const CoinQuote& other = report.crypto.coins[order[j]];
        float a = quote.valid ? quote.change24h : (descending ? -10000.0f : 10000.0f);
        float b = other.valid ? other.change24h : (descending ? -10000.0f : 10000.0f);
        const bool outOfOrder = descending ? (a > b) : (a < b);
        if (!outOfOrder) break;
        order[j + 1] = order[j];
        --j;
      }
      order[j + 1] = current;
    }
  }

  for (uint8_t row = 0; row < kCoinCount; ++row) {
    const uint8_t i = order[row];
    const CoinInfo& info = coinInfo(i);
    const CoinQuote& quote = report.crypto.coins[i];
    const int16_t rowTop = static_cast<int16_t>(kCoinRowsTop + row * kCoinRowHeight);
    const int16_t symbolBaseline = static_cast<int16_t>(rowTop + 62);
    if (row > 0) drawDashedRule(target, static_cast<int16_t>(rowTop - 10), kMargin, kWidth - kMargin);

    drawLine(target, freeink::ui::kWeatherTitleFont, kFontTitle, kMargin, symbolBaseline, info.symbol,
             TextAlign::Left, 80, Color::Black);
    drawLine(target, freeink::ui::kNotoSansFont, kFontBody, 108, symbolBaseline, info.nameZh, TextAlign::Left, 84,
             Color::Black);

    const Rect sparkBox{194, static_cast<int16_t>(rowTop + 22), 104, 34};
    drawSparkline(target, quote, sparkBox);

    if (!quote.valid) {
      drawLine(target, freeink::ui::kNotoSansFont, kFontBody, kWidth - kMargin - 150,
               static_cast<int16_t>(rowTop + 40), "--", TextAlign::Right, 150, Color::Black);
      continue;
    }

    char price[24];
    formatUsd(quote.usd, price, sizeof(price));
    drawLine(target, freeink::ui::kNotoSansFont, kFontBody, kWidth - kMargin - 150,
             static_cast<int16_t>(rowTop + 36), price, TextAlign::Right, 150, Color::Black);

    char change[16];
    formatPercent(quote.change24h, change, sizeof(change));
    drawTrendMarker(target, 388, static_cast<int16_t>(rowTop + 60), quote.change24h);
    drawLine(target, freeink::ui::kNotoSansFont, kFontBody, kWidth - kMargin - 62,
             static_cast<int16_t>(rowTop + 68), change, TextAlign::Right, 62, Color::Black);
  }
}

// Seven-day trend: a 48-point polyline in a small box, with a dashed line at
// the week's opening price so "up" and "down" read at a glance.
void drawSparkline(DisplayTarget& target, const CoinQuote& quote, const Rect& box) {
  const Paint ink = Paint::solid(Color::Black);
  target.stroke(box, ink, 1);
  if (quote.sparkCount < 2) return;

  int16_t low = quote.spark[0];
  int16_t high = quote.spark[0];
  for (uint8_t i = 1; i < quote.sparkCount; ++i) {
    if (quote.spark[i] < low) low = quote.spark[i];
    if (quote.spark[i] > high) high = quote.spark[i];
  }
  if (high == low) high = static_cast<int16_t>(low + 1);

  const int16_t innerTop = static_cast<int16_t>(box.y + 2);
  const int16_t innerHeight = static_cast<int16_t>(box.height - 4);
  const auto yFor = [&](int16_t value) {
    const int32_t scaled = static_cast<int32_t>(value - low) * innerHeight / (high - low);
    return static_cast<int16_t>(innerTop + innerHeight - scaled);
  };
  const auto xFor = [&](uint8_t index) {
    return static_cast<int16_t>(box.x + 2 + static_cast<int32_t>(index) * (box.width - 5) /
                                                 (quote.sparkCount - 1));
  };

  if (low <= 0 && high >= 0) {  // the opening price is inside the range
    const int16_t y = yFor(0);
    for (int16_t x = static_cast<int16_t>(box.x + 2); x < box.x + box.width - 3; x += 6) {
      target.line(freeink::ui::Point{x, y}, freeink::ui::Point{static_cast<int16_t>(x + 2), y}, 1,
                  Paint::solid(Color::DarkGray));
    }
  }
  for (uint8_t i = 1; i < quote.sparkCount; ++i) {
    target.line(freeink::ui::Point{xFor(static_cast<uint8_t>(i - 1)), yFor(quote.spark[i - 1])},
                freeink::ui::Point{xFor(i), yFor(quote.spark[i])}, 2, ink);
  }
}

void drawPlaceholder(DisplayTarget& target, const ScreenState& state) {
  drawIcon(target, Icon::Unknown, Rect{176, 200, 128, 128}, false);
  drawLine(target, freeink::ui::kWeatherTitleFont, kFontTitle, 0, 420,
           state.busy ? "正在获取天气…" : "暂无数据", TextAlign::Center, kWidth, Color::Black);
  drawLine(target, freeink::ui::kNotoSansFont, kFontBody, 0, 480,
           state.busy ? "连接 WiFi 中" : "请检查网络与位置设置", TextAlign::Center, kWidth, Color::Black);
}

}  // namespace

Page nextPage(Page page) {
  const uint8_t next = static_cast<uint8_t>((static_cast<uint8_t>(page) + 1) % kPageCount);
  return static_cast<Page>(next);
}

Page previousPage(Page page) {
  const uint8_t previous = static_cast<uint8_t>((static_cast<uint8_t>(page) + kPageCount - 1) % kPageCount);
  return static_cast<Page>(previous);
}

void configureWeatherFonts(DisplayTarget& target, CjkGlyphSource& fallback) {
  target.setFont(kFontBody, freeink::ui::kNotoSansFont);
  target.setFont(kFontTitle, freeink::ui::kWeatherTitleFont);
  target.setFont(kFontBig, freeink::ui::kWeatherDigitsFont);
  target.setGlyphFallback(&fallback);
}

void renderWeatherScreen(DisplayTarget& target, const ScreenState& state) {
  target.fill(Rect{0, 0, kWidth, kHeight}, Paint::solid(Color::White));

  const WeatherReport* report = state.report;
  const bool hasData = report != nullptr && report->ok && report->current.valid;

  drawHeader(target, report, state.place);
  drawFooter(target, state);

  if (report == nullptr || (!hasData && state.page == Page::Now)) {
    drawPlaceholder(target, state);
    return;
  }

  switch (state.page) {
    case Page::Hourly:
      drawHourlyPage(target, *report);
      break;
    case Page::AirQuality:
      drawAirQualityPage(target, *report);
      break;
    case Page::Crypto:
      drawCryptoPage(target, state);
      break;
    case Page::Now:
    default:
      drawNowPage(target, *report);
      break;
  }
}

}  // namespace weather
