#include "WeatherUi.h"

#include "ArtBits.h"

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
using freeink::ui::DrawTarget;
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
void drawLine(DrawTarget& target, const freeink::ui::BitmapFont& font, FontId slot, int16_t x,
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

int16_t textWidth(DrawTarget& target, FontId slot, const char* text) {
  TextStyle style;
  style.font = slot;
  return target.measureText(slot, text, style).width;
}

void drawIcon(DrawTarget& target, Icon icon, const Rect& rect, bool small) {
  const IconBitmap& chosen = small ? kIcons[static_cast<uint8_t>(icon)].small : kIcons[static_cast<uint8_t>(icon)].big;
  const BitmapRef ref{chosen.data, chosen.width, chosen.height, BitmapFormat::BW1, false};
  target.bitmap(rect, ref, BitmapMode::Contain, Paint::solid(Color::Black));
}

// A dashed separator: cheaper on the eye (and on the panel) than a solid rule.
void drawDashedRule(DrawTarget& target, int16_t y, int16_t x0, int16_t x1) {
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
#if FREEINK_DEVICE_PAPERS3
    case Page::Home: return "";
#endif
    case Page::Hourly: return "未来 12 小时";
    case Page::AirQuality: return "空气质量";
    case Page::Crypto: return "数字货币";
    default: return "";
  }
}

// Small direction marker for a 24 h change: filled pointing up when the price
// rose, down when it fell, a dash when it barely moved.
void drawTrendMarker(DrawTarget& target, int16_t centerX, int16_t centerY, float changePercent) {
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

// Outlined battery with a proportional fill, drawn in the header's ink.
void drawBatteryGlyph(DrawTarget& target, int16_t x, int16_t y, uint8_t percent) {
  const Paint ink = Paint::solid(Color::White);
  constexpr int16_t kBodyWidth = 34;
  constexpr int16_t kBodyHeight = 17;
  target.stroke(Rect{x, y, kBodyWidth, kBodyHeight}, ink, 2);
  target.fill(Rect{static_cast<int16_t>(x + kBodyWidth + 1), static_cast<int16_t>(y + 5), 3, 7}, ink);

  const uint8_t level = percent > 100 ? 100 : percent;
  const int16_t inner = static_cast<int16_t>((kBodyWidth - 8) * level / 100);
  if (inner > 0) {
    target.fill(Rect{static_cast<int16_t>(x + 4), static_cast<int16_t>(y + 4), inner,
                     static_cast<int16_t>(kBodyHeight - 8)},
                ink);
  }
}

void drawHeader(DrawTarget& target, const ScreenState& state) {
  const WeatherReport* report = state.report;
  const char* place = state.place;
  target.fill(Rect{0, 0, kWidth, kHeaderBottom}, Paint::solid(Color::Black));

  const char* label = nullptr;
  if (report != nullptr && report->place[0] != '\0') {
    label = report->place;
  } else if (place != nullptr && place[0] != '\0') {
    label = place;
  } else {
    label = "天气";
  }
  // The right column carries the date and the battery, so the place gets the rest.
  drawLine(target, freeink::ui::kWeatherTitleFont, kFontTitle, kMargin, 62, label, TextAlign::Left, 240,
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
#if FREEINK_DEVICE_PAPERS3
    // Right edge stops short of the gear (the last 52 px of the strip).
    drawLine(target, freeink::ui::kNotoSansFont, kFontBody, kWidth - kMargin - 52 - 170, 44, dateLabel,
             TextAlign::Right, 170, Color::White);
#else
    drawLine(target, freeink::ui::kNotoSansFont, kFontBody, kWidth - kMargin - 170, 44, dateLabel, TextAlign::Right,
             170, Color::White);
#endif
  }

  if (state.batteryKnown) {
    char battery[8];
    snprintf(battery, sizeof(battery), "%u%%", static_cast<unsigned>(state.batteryPercent));
#if FREEINK_DEVICE_PAPERS3
    // Leave the last 52 px for the settings gear: the whole strip opens settings,
    // the icon is what tells a touch user it is tappable.
    const int16_t batteryRight = static_cast<int16_t>(kWidth - kMargin - 52);
    drawLine(target, freeink::ui::kNotoSansFont, kFontBody, batteryRight, 84, battery, TextAlign::Right, 62,
             Color::White);
    drawBatteryGlyph(target, static_cast<int16_t>(batteryRight - 62 - 6 - 34), 76, state.batteryPercent);
#else
    drawLine(target, freeink::ui::kNotoSansFont, kFontBody, kWidth - kMargin - 62, 84, battery,
             TextAlign::Right, 62, Color::White);
    drawBatteryGlyph(target, static_cast<int16_t>(kWidth - kMargin - 62 - 6 - 34), 76,
                     state.batteryPercent);
#endif
  }
#if FREEINK_DEVICE_PAPERS3
  {
    const BitmapRef gear{art::kGearBits, art::kGearWidth, art::kGearHeight, BitmapFormat::BW1, true};
    target.bitmap(Rect{static_cast<int16_t>(kWidth - kMargin - 40), 34, art::kGearWidth, art::kGearHeight},
                  gear, BitmapMode::Contain, Paint::solid(Color::White));
  }
#endif
}

void drawFooter(DrawTarget& target, const ScreenState& state) {
  if (state.status != nullptr && state.status[0] != '\0') {
    drawLine(target, freeink::ui::kNotoSansFont, kFontBody, kMargin, kFooterBaseline, state.status,
             TextAlign::Left, 240, Color::Black);
  }
  char indicator[48];
  if (state.interactive) {
    snprintf(indicator, sizeof(indicator), "%u/%u %s", static_cast<unsigned>(state.page) + 1,
             static_cast<unsigned>(kPageCount),
             state.hint != nullptr ? state.hint
#if FREEINK_DEVICE_PAPERS3
                                   : "左右滑动翻页"
#else
                                   : "左右切页"
#endif
    );
  } else {
    snprintf(indicator, sizeof(indicator), "%u/%u", static_cast<unsigned>(state.page) + 1,
             static_cast<unsigned>(kPageCount));
  }
  drawLine(target, freeink::ui::kNotoSansFont, kFontBody, kWidth - kMargin - 240, kFooterBaseline, indicator,
           TextAlign::Right, 240, Color::Black);
}

// A big figure (digits font) centred on its own, with an optional unit drawn in
// the title font right after it, sharing the baseline.
void drawFigure(DrawTarget& target, const char* figure, const char* unit, int16_t baseline) {
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

void drawMetricsRow(DrawTarget& target, const CurrentWeather& current, int16_t baseline) {
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

#if FREEINK_DEVICE_PAPERS3
// Home: the clock and the weather up top, a sleeping cat below. Drawn only on the
// touch board — the X4 keeps its shipped page set.
void drawHomePage(DrawTarget& target, const ScreenState& state) {
  const WeatherReport& report = *state.report;
  const CurrentWeather& current = report.current;

  // Clock: "2026-10-17T09:32" -> "09:32", plus the date underneath.
  char clock[8] = "--:--";
  char date[11] = {0};
  char shortDate[8] = {0};
  char dayLine[40] = {0};
  if (current.time[0] != '\0' && strlen(current.time) >= 16) {
    snprintf(clock, sizeof(clock), "%.5s", current.time + 11);
    copyIsoDate(current.time, date, sizeof(date));
    formatShortDate(date, shortDate, sizeof(shortDate));
    snprintf(dayLine, sizeof(dayLine), "%s %s", shortDate, weekdayName(weekdayFromIsoDate(date)));
  }
  drawLine(target, freeink::ui::kWeatherDigitsFont, kFontBig, 0, 196, clock, TextAlign::Center, kWidth,
           Color::Black);

  if (dayLine[0] != '\0') {
    drawLine(target, freeink::ui::kNotoSansFont, kFontBody, 0, 244, dayLine, TextAlign::Center, kWidth,
             Color::Black);
  }

  drawDashedRule(target, 286, kMargin, kWidth - kMargin);

  // City, then the summary line the user asked for.
  const char* city = report.place[0] != '\0' ? report.place : (state.place != nullptr ? state.place : "天气");
  drawLine(target, freeink::ui::kWeatherTitleFont, kFontTitle, 0, 348, city, TextAlign::Center, kWidth,
           Color::Black);

  char summary[48] = {0};
  if (current.valid) {
    char temp[16];
    formatTemp1(current.temperatureC, temp, sizeof(temp));
    snprintf(summary, sizeof(summary), "%s°C  %s", temp, conditionText(current.code));
  } else {
    snprintf(summary, sizeof(summary), "暂无观测");
  }
  drawLine(target, freeink::ui::kWeatherTitleFont, kFontTitle, 0, 408, summary, TextAlign::Center, kWidth,
           Color::Black);

  char detail[64] = {0};
  if (report.days[0].valid) {
    char range[32];
    formatRange(report.days[0].tMin, report.days[0].tMax, range, sizeof(range));
    snprintf(detail, sizeof(detail), "%s  湿度 %u%%", range, static_cast<unsigned>(current.humidity));
  }
  drawLine(target, freeink::ui::kNotoSansFont, kFontBody, 0, 456, detail, TextAlign::Center, kWidth,
           Color::Black);

  drawDashedRule(target, 506, kMargin, kWidth - kMargin);

  // The sleeping cat: lower half, centred, ending clear of the footer. The art is
  // trimmed to its ink, so the placement numbers are the whole gap budget.
  constexpr int16_t catHeight = static_cast<int16_t>(art::kCatHeight);
  constexpr int16_t catY = static_cast<int16_t>(780 - 44 - catHeight);
  constexpr int16_t catX = (kWidth - static_cast<int16_t>(art::kCatWidth)) / 2;
  const BitmapRef cat{art::kCatBits, art::kCatWidth, art::kCatHeight, BitmapFormat::BW1, true};
  target.bitmap(Rect{catX, catY, art::kCatWidth, art::kCatHeight}, cat, BitmapMode::Contain,
                Paint::solid(Color::Black));
  // Zzz in the empty band above the cat's back, clear of the weather lines.
  drawLine(target, freeink::ui::kWeatherTitleFont, kFontTitle, static_cast<int16_t>(catX + 250),
           static_cast<int16_t>(catY + 26), "Z z z", TextAlign::Left, 130, Color::Black);
}
#endif  // FREEINK_DEVICE_PAPERS3

void drawNowPage(DrawTarget& target, const WeatherReport& report) {
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

void drawHourlyPage(DrawTarget& target, const WeatherReport& report) {
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

void drawAirQualityScale(DrawTarget& target, int europeanAqi, int16_t y) {
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

void drawPollutantRow(DrawTarget& target, int16_t y, const char* label, float value) {
  drawLine(target, freeink::ui::kNotoSansFont, kFontBody, kMargin, static_cast<int16_t>(y + 34), label,
           TextAlign::Left, 160, Color::Black);
  char number[16];
  char text[32];
  snprintf(text, sizeof(text), "%s µg/m³", formatTemp1(value, number, sizeof(number)));
  drawLine(target, freeink::ui::kNotoSansFont, kFontBody, kWidth - kMargin - 240, static_cast<int16_t>(y + 34), text,
           TextAlign::Right, 240, Color::Black);
}

void drawAirQualityPage(DrawTarget& target, const WeatherReport& report) {
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

void drawSparkline(DrawTarget& target, const CoinQuote& quote, const Rect& box);

void drawCryptoPage(DrawTarget& target, const ScreenState& state) {
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

  // Only the coins the user kept visible take part in ordering and drawing.
  uint8_t order[kCoinCount];
  uint8_t visible = 0;
  for (uint8_t i = 0; i < kCoinCount; ++i) {
    if ((state.coinMask & (1u << i)) != 0) order[visible++] = i;
  }
  if (visible == 0) {  // never leave the page blank
    for (uint8_t i = 0; i < kCoinCount; ++i) order[i] = i;
    visible = kCoinCount;
  }
  if (state.cryptoSort != CryptoSort::Table) {
    const bool descending = state.cryptoSort == CryptoSort::Gainers;
    for (uint8_t i = 1; i < visible; ++i) {
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

  for (uint8_t row = 0; row < visible; ++row) {
    const uint8_t i = order[row];
    const CoinInfo& info = coinInfo(i);
    const CoinQuote& quote = report.crypto.coins[i];
    const int16_t rowTop = static_cast<int16_t>(kCoinRowsTop + row * kCoinRowHeight);
    const int16_t symbolBaseline = static_cast<int16_t>(rowTop + 58);
    if (row > 0) drawDashedRule(target, static_cast<int16_t>(rowTop - 10), kMargin, kWidth - kMargin);

    drawLine(target, freeink::ui::kWeatherTitleFont, kFontTitle, kMargin, symbolBaseline, info.symbol,
             TextAlign::Left, 80, Color::Black);
    drawLine(target, freeink::ui::kNotoSansFont, kFontBody, 106, symbolBaseline, info.nameZh, TextAlign::Left, 86,
             Color::Black);

    const Rect sparkBox{196, static_cast<int16_t>(rowTop + 20), 98, 34};
    drawSparkline(target, quote, sparkBox);
    if (quote.sparkCount < 2) {
      // No reachable OHLC source for this coin: say so instead of an empty box.
      drawLine(target, freeink::ui::kNotoSansFont, kFontBody, sparkBox.x + 4,
               static_cast<int16_t>(sparkBox.y + 26), "无走势", TextAlign::Center, sparkBox.width - 8,
               Color::Black);
    }

    if (!quote.valid) {
      drawLine(target, freeink::ui::kNotoSansFont, kFontBody, 300, static_cast<int16_t>(rowTop + 40),
               "--", TextAlign::Right, 156, Color::Black);
      continue;
    }

    char price[24];
    formatUsd(quote.usd, price, sizeof(price));
    // Price on the first line, 24 h change under it: both get the full width to
    // the right of the sparkline, so no value is ever clipped.
    drawLine(target, freeink::ui::kNotoSansFont, kFontBody, 300, static_cast<int16_t>(rowTop + 32),
             price, TextAlign::Right, 156, Color::Black);

    char change[16];
    formatPercent(quote.change24h, change, sizeof(change));
    drawTrendMarker(target, 372, static_cast<int16_t>(rowTop + 58), quote.change24h);
    drawLine(target, freeink::ui::kNotoSansFont, kFontBody, 382, static_cast<int16_t>(rowTop + 66),
             change, TextAlign::Right, 74, Color::Black);
  }
}

// Seven-day trend: a 48-point polyline in a small box, with a dashed line at
// the week's opening price so "up" and "down" read at a glance.
void drawSparkline(DrawTarget& target, const CoinQuote& quote, const Rect& box) {
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

void drawPlaceholder(DrawTarget& target, const ScreenState& state) {
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

void renderWeatherScreen(DrawTarget& target, const ScreenState& state) {
  target.fill(Rect{0, 0, kWidth, kHeight}, Paint::solid(Color::White));

  const WeatherReport* report = state.report;
  const bool hasData = report != nullptr && report->ok && report->current.valid;

  drawHeader(target, state);
  drawFooter(target, state);

  if (report == nullptr || (!hasData && (state.page == Page::Now
#if FREEINK_DEVICE_PAPERS3
                                        || state.page == Page::Home
#endif
                                            ))) {
    drawPlaceholder(target, state);
    return;
  }

  switch (state.page) {
#if FREEINK_DEVICE_PAPERS3
    case Page::Home:
      drawHomePage(target, state);
      break;
#endif
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
