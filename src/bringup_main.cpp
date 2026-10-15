// Bring-up / calibration firmware for a touch e-paper board (M5Stack PaperS3).
//
// The SDK's PaperS3 notes list three facts as "pending hardware validation";
// one flash of this build answers all three, plus the questions our port has:
//
//   1. panel rotation  — the calibration screen prints 上/下/左/右 + corner
//      labels, so we can see which way the logical portrait frame lands;
//   2. GT911 axes      — every contact is printed as raw panel coordinates and
//      as logical portrait coordinates (through the DisplayTarget transform) and
//      classified into a quadrant, so tapping 左上/右上/左下/右下 tells us whether
//      swapXY / flipX / flipY in the board profile are right;
//   3. battery divider — mV, percentage and charge/USB state are printed;
// and additionally refresh timings for FULL / HALF / FAST, and whether the
// SDK's display, input and battery classes come up at all on this board.
//
// It never sleeps: the loop polls touch and reprints the battery every 15 s, so
// one monitor session collects everything. Built as [env:papers3_bringup].
#include <Arduino.h>
#include <BatteryMonitor.h>
#include <BoardConfig.h>
#include <EInkDisplay.h>
#include <FreeInkUIDisplayTarget.h>
#include <InputManager.h>
#include <PowerManager.h>

#include <cstdio>
#include <cstring>

#include "CjkGlyphSource.h"
#include "WeatherUi.h"
#include "fonts/WeatherTitleFont.h"

namespace {

using freeink::ui::Color;
using freeink::ui::DisplayTarget;
using freeink::ui::FontId;
using freeink::ui::Paint;
using freeink::ui::Point;
using freeink::ui::Rect;
using freeink::ui::TextAlign;
using freeink::ui::TextStyle;

constexpr FontId kBody = weather::kFontBody;
constexpr FontId kTitle = weather::kFontTitle;

EInkDisplay display(BoardConfig::DEFAULT_DEVICE.display.sclk,
                    BoardConfig::DEFAULT_DEVICE.display.mosi,
                    BoardConfig::DEFAULT_DEVICE.display.cs,
                    BoardConfig::DEFAULT_DEVICE.display.dc,
                    BoardConfig::DEFAULT_DEVICE.display.rst,
                    BoardConfig::DEFAULT_DEVICE.display.busy);
InputManager input;
BatteryMonitor battery;
DisplayTarget* target = nullptr;
weather::CjkGlyphSource cjk;

void text(DisplayTarget& t, FontId slot, const freeink::ui::BitmapFont& font, int16_t x, int16_t baseline,
          const char* s, TextAlign align, int16_t box, Color color) {
  if (s == nullptr || s[0] == '\0') return;
  TextStyle style;
  style.font = slot;
  style.align = align;
  style.color = color;
  style.maxLines = 1;
  t.text(Rect{x, static_cast<int16_t>(baseline - font.ascent), box,
              static_cast<int16_t>(font.yAdvance)},
         s, style);
}

void body(DisplayTarget& t, int16_t x, int16_t baseline, const char* s, TextAlign align = TextAlign::Left,
          int16_t box = 300, Color color = Color::Black) {
  text(t, kBody, freeink::ui::kNotoSansFont, x, baseline, s, align, box, color);
}

void heading(DisplayTarget& t, int16_t x, int16_t baseline, const char* s, TextAlign align = TextAlign::Left,
             int16_t box = 400, Color color = Color::Black) {
  text(t, kTitle, freeink::ui::kWeatherTitleFont, x, baseline, s, align, box, color);
}

// The calibration screen: a clear "this way up" marker, labelled corners and the
// numbers we care about.
void drawCalibrationScreen(const char* extra) {
  if (target == nullptr) return;
  DisplayTarget& t = *target;
  const int16_t w = t.logicalWidth();
  const int16_t h = t.logicalHeight();

  t.fill(Rect{0, 0, w, h}, Paint::solid(Color::White));
  t.stroke(Rect{0, 0, w, h}, Paint::solid(Color::Black), 3);

  // "Up" arrow: a triangle plus a shaft, at the top centre of the logical frame.
  const int16_t cx = static_cast<int16_t>(w / 2);
  t.triangle(Point{cx, 40}, Point{static_cast<int16_t>(cx - 34), 86}, Point{static_cast<int16_t>(cx + 34), 86},
             Paint::solid(Color::Black));
  t.fill(Rect{static_cast<int16_t>(cx - 11), 86, 22, 46}, Paint::solid(Color::Black));
  heading(t, 0, 190, "上", TextAlign::Center, w);
  body(t, 0, 232, "若这一侧朝上，rotation 正确", TextAlign::Center, w);

  heading(t, 24, 320, "左上", TextAlign::Left, 200);
  heading(t, w - 224, 320, "右上", TextAlign::Right, 200);
  heading(t, 24, h - 40, "左下", TextAlign::Left, 200);
  heading(t, w - 224, h - 40, "右下", TextAlign::Right, 200);

  char line[96];
  std::snprintf(line, sizeof(line), "%s  %dx%d", BoardConfig::ACTIVE.name,
                static_cast<int>(BoardConfig::ACTIVE.displayWidth),
                static_cast<int>(BoardConfig::ACTIVE.displayHeight));
  body(t, 0, 400, line, TextAlign::Center, w);
  std::snprintf(line, sizeof(line), "逻辑帧 %dx%d", static_cast<int>(w), static_cast<int>(h));
  body(t, 0, 436, line, TextAlign::Center, w);
  std::snprintf(line, sizeof(line), "触控 %s  多点 %s", input.hasTouch() ? "有" : "无",
                input.supportsMultiTouch() ? "有" : "无");
  body(t, 0, 472, line, TextAlign::Center, w);
  if (extra != nullptr) {
    body(t, 0, 508, extra, TextAlign::Center, w);
  }
  body(t, 0, h - 96, "点亮四个角，看日志里的象限分类", TextAlign::Center, w);
}

void printBoardFacts() {
  const BoardConfig::BoardProfile& profile = BoardConfig::ACTIVE;
  Serial.printf("[cal] board=%s display=%ux%u controller=%d\n", profile.name,
                static_cast<unsigned>(profile.displayWidth),
                static_cast<unsigned>(profile.displayHeight),
                static_cast<int>(profile.displayController));
  Serial.printf("[cal] framebuffer=%dx%d (logical %dx%d)\n", static_cast<int>(display.getDisplayWidth()),
                static_cast<int>(display.getDisplayHeight()),
                static_cast<int>(target != nullptr ? target->logicalWidth() : 0),
                static_cast<int>(target != nullptr ? target->logicalHeight() : 0));
  Serial.printf("[cal] touch controller=%d sda=%d scl=%d irq=%d addr=0x%02X alt=0x%02X swapXY=%d flipX=%d "
                "flipY=%d rawMin=%u,%u rawMax=%u,%u coordsAtByte0=%d irqActiveLow=%d\n",
                static_cast<int>(profile.touch.controller), static_cast<int>(profile.touch.sda),
                static_cast<int>(profile.touch.scl), static_cast<int>(profile.touch.irq),
                static_cast<unsigned>(profile.touch.i2cAddress),
                static_cast<unsigned>(profile.touch.i2cAddressAlt), profile.touch.swapXY ? 1 : 0,
                profile.touch.flipX ? 1 : 0, profile.touch.flipY ? 1 : 0,
                static_cast<unsigned>(profile.touch.rawMinX), static_cast<unsigned>(profile.touch.rawMinY),
                static_cast<unsigned>(profile.touch.rawMaxX), static_cast<unsigned>(profile.touch.rawMaxY),
                profile.touch.gt911CoordsAtByte0 ? 1 : 0, profile.touch.irqActiveLow ? 1 : 0);
  Serial.printf("[cal] battery adc=%d chargeStatus=%d divider=%.2f usbDetect=%d hasTouch=%d\n",
                static_cast<int>(profile.batteryAdc), static_cast<int>(profile.batteryChargeStatus),
                static_cast<double>(profile.batteryDividerMultiplier),
                static_cast<int>(profile.usbDetect), input.hasTouch() ? 1 : 0);
}

void printBattery() {
  const BatteryMonitor::Status status = battery.readStatus();
  bool externalKnown = false;
  const bool external = battery.isExternalPowerPresent(&externalKnown);
  Serial.printf("[cal] battery supported=%d mV=%u %%=%u charging=%d(%d) externalPower=%d(%d)\n",
                status.supported ? 1 : 0, static_cast<unsigned>(status.millivolts),
                static_cast<unsigned>(status.percentage), status.charging ? 1 : 0,
                status.chargingKnown ? 1 : 0, external ? 1 : 0, externalKnown ? 1 : 0);
  Serial.printf("[cal]   -> percentageFromMillivolts(%u) = %u%%\n",
                static_cast<unsigned>(battery.readMillivolts()),
                static_cast<unsigned>(BatteryMonitor::percentageFromMillivolts(battery.readMillivolts())));
}

// Which quadrant of the logical portrait frame a logical point falls in.
const char* quadrantOf(int16_t x, int16_t y) {
  if (target == nullptr) return "?";
  const int16_t w = target->logicalWidth();
  const int16_t h = target->logicalHeight();
  const bool left = x < w / 2;
  const bool top = y < h / 2;
  if (top) return left ? "左上" : "右上";
  return left ? "左下" : "右下";
}

// Marks the logical quadrant a mapped touch landed in, so the corner test can be
// judged by eye as well as from the log.
void markQuadrant(int16_t x, int16_t y) {
  if (target == nullptr) return;
  DisplayTarget& t = *target;
  const int16_t w = t.logicalWidth();
  const int16_t h = t.logicalHeight();
  const int16_t size = 72;
  const Rect box{x < w / 2 ? static_cast<int16_t>(8) : static_cast<int16_t>(w - size - 8),
                 y < h / 2 ? static_cast<int16_t>(8) : static_cast<int16_t>(h - size - 8), size, size};
  t.fill(box, Paint::solid(Color::Black));
}

void reportTouch(const InputManager::TouchPoint& point, const char* what) {
  if (target == nullptr) return;
  const BoardConfig::BoardProfile& profile = BoardConfig::ACTIVE;
  // touchToLogical() takes PANEL-NATIVE NORMALISED coordinates (0..1), which is
  // also what InputManager's tap endpoints use.
  const float nx = static_cast<float>(point.x) / static_cast<float>(profile.displayWidth);
  const float ny = static_cast<float>(point.y) / static_cast<float>(profile.displayHeight);
  const freeink::ui::Point logical = freeink::ui::touchToLogical(target->deviceContext(), nx, ny);
  Serial.printf("[cal] %6u ms %s panel=(%u,%u) normalized=(%.3f,%.3f) logical=(%d,%d) -> %s\n",
                static_cast<unsigned>(millis()), what, static_cast<unsigned>(point.x),
                static_cast<unsigned>(point.y), static_cast<double>(nx), static_cast<double>(ny),
                static_cast<int>(logical.x), static_cast<int>(logical.y),
                quadrantOf(logical.x, logical.y));
  if (std::strcmp(what, "抬起") == 0) {
    markQuadrant(logical.x, logical.y);
    display.displayBuffer(EInkDisplay::FAST_REFRESH);
  }
}

void timeRefresh(EInkDisplay::RefreshMode mode, const char* name) {
  // Repaint the same content twice so a partial/differential path is measurable.
  drawCalibrationScreen(name);
  const uint32_t start = millis();
  display.displayBuffer(mode);
  Serial.printf("[cal] refresh %s: %u ms\n", name, static_cast<unsigned>(millis() - start));
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(2500);  // native USB CDC: give the host time to attach before the log
  Serial.println();
  Serial.println("[cal] PaperS3 bring-up — FreeInk SDK calibration build");

  display.begin();
  target = new DisplayTarget(display.getFrameBuffer(), display.getDisplayWidth(), display.getDisplayHeight(),
                             display.getDisplayWidth() / 8);
  weather::configureWeatherFonts(*target, cjk);

  input.begin();
  printBoardFacts();
  printBattery();

  drawCalibrationScreen("绘制中");
  const uint32_t drawDone = millis();
  display.displayBuffer(EInkDisplay::FULL_REFRESH);
  Serial.printf("[cal] first FULL refresh: %u ms (draw+refresh from boot %u ms)\n",
                static_cast<unsigned>(millis() - drawDone), static_cast<unsigned>(millis()));

  // Refresh timings for all three modes. The content changes between them, so a
  // partial/differential path has something to push.
  drawCalibrationScreen("FULL");
  uint32_t start = millis();
  display.displayBuffer(EInkDisplay::FULL_REFRESH);
  Serial.printf("[cal] refresh FULL: %u ms\n", static_cast<unsigned>(millis() - start));

  drawCalibrationScreen("HALF");
  start = millis();
  display.displayBuffer(EInkDisplay::HALF_REFRESH);
  Serial.printf("[cal] refresh HALF: %u ms\n", static_cast<unsigned>(millis() - start));

  drawCalibrationScreen("FAST");
  start = millis();
  display.displayBuffer(EInkDisplay::FAST_REFRESH);
  Serial.printf("[cal] refresh FAST: %u ms\n", static_cast<unsigned>(millis() - start));

  // Same content again: this is what a page flip in the weather UI costs.
  drawCalibrationScreen("FAST (unchanged)");
  start = millis();
  display.displayBuffer(EInkDisplay::FAST_REFRESH);
  Serial.printf("[cal] refresh FAST unchanged: %u ms\n", static_cast<unsigned>(millis() - start));

  Serial.println("[cal] 请依次点击四个角：左上 → 右上 → 左下 → 右下");
}

void loop() {
  input.update();

  if (input.wasTouchPressed()) {
    reportTouch(input.getTouchPoint(), "按下");
  }
  if (input.wasTouchReleased()) {
    reportTouch(input.getTouchPoint(), "抬起");
  }

  static uint32_t lastBattery = 0;
  if (millis() - lastBattery > 15000) {
    lastBattery = millis();
    printBattery();
  }
  delay(10);
}
