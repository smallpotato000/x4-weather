#include "SettingsUi.h"

#include <cstdio>
#include <cstring>

#include "CityList.h"
#include "WeatherUi.h"  // font slots + configure-time font names
#include "fonts/WeatherTitleFont.h"

namespace weather {
namespace {

using freeink::ui::BitmapFont;
using freeink::ui::Color;
using freeink::ui::DrawTarget;
using freeink::ui::FontId;
using freeink::ui::Paint;
using freeink::ui::Point;
using freeink::ui::Rect;
using freeink::ui::TextAlign;
using freeink::ui::TextStyle;

using L = SettingsLayout;

constexpr FontId kBody = kFontBody;    // Noto Sans, 24 px
constexpr FontId kTitle = kFontTitle;  // WeatherTitle, 34 px

const Paint kInk = Paint::solid(Color::Black);
const Paint kPaper = Paint::solid(Color::White);

// --- text -------------------------------------------------------------------

void text(DrawTarget& t, FontId slot, const BitmapFont& font, int16_t x, int16_t baseline, const char* s,
          TextAlign align, int16_t box, Color color) {
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

void body(DrawTarget& t, int16_t x, int16_t baseline, const char* s, TextAlign align = TextAlign::Left,
          int16_t box = 300, Color color = Color::Black) {
  text(t, kBody, freeink::ui::kNotoSansFont, x, baseline, s, align, box, color);
}

void heading(DrawTarget& t, int16_t x, int16_t baseline, const char* s,
             TextAlign align = TextAlign::Left, int16_t box = 400, Color color = Color::Black) {
  text(t, kTitle, freeink::ui::kWeatherTitleFont, x, baseline, s, align, box, color);
}

// --- small glyphs -----------------------------------------------------------

void chevron(DrawTarget& t, int16_t x, int16_t centerY) {
  t.line(Point{x, static_cast<int16_t>(centerY - 10)}, Point{static_cast<int16_t>(x + 9), centerY}, 3, kInk);
  t.line(Point{static_cast<int16_t>(x + 9), centerY}, Point{x, static_cast<int16_t>(centerY + 10)}, 3, kInk);
}

void backChevron(DrawTarget& t, int16_t x, int16_t centerY, Color color) {
  const Paint ink = Paint::solid(color);
  t.line(Point{static_cast<int16_t>(x + 10), static_cast<int16_t>(centerY - 16)}, Point{x, centerY}, 3, ink);
  t.line(Point{x, centerY}, Point{static_cast<int16_t>(x + 10), static_cast<int16_t>(centerY + 16)}, 3, ink);
}

void dashRule(DrawTarget& t, int16_t y, int16_t x0 = L::margin, int16_t x1 = L::rightEdge) {
  for (int16_t x = x0; x < x1; x += 9) {
    const int16_t end = static_cast<int16_t>(x + 5 < x1 ? x + 5 : x1);
    t.line(Point{x, y}, Point{end, y}, 1, kInk);
  }
}

void signalBars(DrawTarget& t, int16_t x, int16_t baseline, uint8_t level, Color color = Color::Black) {
  const Paint ink = Paint::solid(color);
  for (uint8_t i = 0; i < 4; ++i) {
    const int16_t height = static_cast<int16_t>(6 + i * 5);
    const Rect bar{static_cast<int16_t>(x + i * 9), static_cast<int16_t>(baseline - height), 6, height};
    if (i < level) {
      t.fill(bar, ink);
    } else {
      t.stroke(bar, ink, 1);
    }
  }
}

void lockGlyph(DrawTarget& t, int16_t x, int16_t baseline, Color color = Color::Black) {
  const Paint ink = Paint::solid(color);
  const int16_t bodyTop = static_cast<int16_t>(baseline - 13);
  t.fill(Rect{x, bodyTop, 15, 13}, ink);
  t.line(Point{static_cast<int16_t>(x + 3), bodyTop},
         Point{static_cast<int16_t>(x + 3), static_cast<int16_t>(bodyTop - 7)}, 2, ink);
  t.line(Point{static_cast<int16_t>(x + 3), static_cast<int16_t>(bodyTop - 7)},
         Point{static_cast<int16_t>(x + 12), static_cast<int16_t>(bodyTop - 7)}, 2, ink);
  t.line(Point{static_cast<int16_t>(x + 12), static_cast<int16_t>(bodyTop - 7)},
         Point{static_cast<int16_t>(x + 12), bodyTop}, 2, ink);
}

void tick(DrawTarget& t, int16_t x, int16_t centerY) {
  t.line(Point{x, centerY}, Point{static_cast<int16_t>(x + 10), static_cast<int16_t>(centerY + 12)}, 4, kInk);
  t.line(Point{static_cast<int16_t>(x + 10), static_cast<int16_t>(centerY + 12)},
         Point{static_cast<int16_t>(x + 26), static_cast<int16_t>(centerY - 12)}, 4, kInk);
}

void checkBox(DrawTarget& t, int16_t x, int16_t y, bool checked, int16_t size = 30) {
  t.stroke(Rect{x, y, size, size}, kInk, 3, 4);
  if (!checked) return;
  const int16_t cx = static_cast<int16_t>(x + size / 2);
  const int16_t cy = static_cast<int16_t>(y + size / 2);
  t.line(Point{static_cast<int16_t>(x + 7), cy}, Point{static_cast<int16_t>(cx - 1),
                                                       static_cast<int16_t>(cy + 7)}, 4, kInk);
  t.line(Point{static_cast<int16_t>(cx - 1), static_cast<int16_t>(cy + 7)},
         Point{static_cast<int16_t>(x + size - 7), static_cast<int16_t>(cy - 8)}, 4, kInk);
}

void cross(DrawTarget& t, int16_t centerX, int16_t centerY, int16_t arm) {
  t.line(Point{static_cast<int16_t>(centerX - arm), static_cast<int16_t>(centerY - arm)},
         Point{static_cast<int16_t>(centerX + arm), static_cast<int16_t>(centerY + arm)}, 3, kInk);
  t.line(Point{static_cast<int16_t>(centerX - arm), static_cast<int16_t>(centerY + arm)},
         Point{static_cast<int16_t>(centerX + arm), static_cast<int16_t>(centerY - arm)}, 3, kInk);
}

void batteryGlyph(DrawTarget& t, int16_t x, int16_t y, uint8_t percent) {
  constexpr int16_t kBodyWidth = 34;
  constexpr int16_t kBodyHeight = 17;
  t.stroke(Rect{x, y, kBodyWidth, kBodyHeight}, kPaper, 2);
  t.fill(Rect{static_cast<int16_t>(x + kBodyWidth + 1), static_cast<int16_t>(y + 5), 3, 7}, kPaper);
  const uint8_t level = percent > 100 ? 100 : percent;
  const int16_t inner = static_cast<int16_t>((kBodyWidth - 8) * level / 100);
  if (inner > 0) {
    t.fill(Rect{static_cast<int16_t>(x + 4), static_cast<int16_t>(y + 4), inner,
                static_cast<int16_t>(kBodyHeight - 8)},
           kPaper);
  }
}

// --- chrome -----------------------------------------------------------------

void header(DrawTarget& t, const SettingsUiState& state, const char* title, const char* action = nullptr) {
  t.fill(Rect{0, 0, L::width, L::headerBottom}, kInk);
  backChevron(t, L::margin + 4, 52, Color::White);
  heading(t, L::margin + 26, 66, title, TextAlign::Left, 260, Color::White);
  if (action != nullptr) {
    body(t, L::rightEdge - 150, 44, action, TextAlign::Right, 150, Color::White);
  }
  if (state.batteryKnown) {
    char percent[8];
    std::snprintf(percent, sizeof(percent), "%u%%", static_cast<unsigned>(state.batteryPercent));
    body(t, L::rightEdge - 62, 88, percent, TextAlign::Right, 62, Color::White);
    batteryGlyph(t, static_cast<int16_t>(L::rightEdge - 62 - 6 - 34), 80, state.batteryPercent);
  }
}

// Footer with a status slot: when the flow has something to say ("正在扫描…",
// "密码错误，请再试一次", "至少保留 4 个币") it replaces the left hint in an
// inverted strip, so every screen can report what just happened. Without this the
// status was set but never drawn on most screens.
void footer(DrawTarget& t, const SettingsUiState& state, const char* left,
            const char* right = nullptr, bool showStatus = true) {
  if (showStatus && state.status != nullptr) {
    const Rect strip{static_cast<int16_t>(L::margin - 6), static_cast<int16_t>(L::footerBaseline - 34),
                     300, 46};
    t.fill(strip, kInk, 6);
    body(t, L::margin, L::footerBaseline, state.status, TextAlign::Left, 288, Color::White);
  } else {
    body(t, L::margin, L::footerBaseline, left, TextAlign::Left, 270);
  }
  if (right != nullptr) {
    body(t, L::rightEdge - 230, L::footerBaseline, right, TextAlign::Right, 230);
  }
}

// One list row: big label, right-aligned value, chevron.
void valueRow(DrawTarget& t, int16_t top, int16_t height, const char* label, const char* value) {
  const int16_t baseline = static_cast<int16_t>(top + 58);
  heading(t, L::margin, baseline, label, TextAlign::Left, 230);
  if (value != nullptr) {
    body(t, L::rightEdge - 300, baseline, value, TextAlign::Right, 262);
  }
  chevron(t, L::rightEdge - 22, static_cast<int16_t>(top + 52));
  dashRule(t, static_cast<int16_t>(top + height - 6));
}

// An option row: label plus a tick when selected.
void optionRow(DrawTarget& t, int16_t top, int16_t height, const char* label, bool selected) {
  heading(t, L::margin, static_cast<int16_t>(top + 58), label, TextAlign::Left, 340);
  if (selected) tick(t, L::rightEdge - 40, static_cast<int16_t>(top + 46));
  dashRule(t, static_cast<int16_t>(top + height - 6));
}

void keyCap(DrawTarget& t, Rect box, const char* label, bool primary = false) {
  t.fill(box, primary ? kInk : kPaper, 6);
  t.stroke(box, kInk, primary ? 3 : 2, 6);
  body(t, box.x, static_cast<int16_t>(box.y + box.height / 2 + 9), label, TextAlign::Center, box.width,
       primary ? Color::White : Color::Black);
}

// --- screens ----------------------------------------------------------------

void drawHome(DrawTarget& t, const SettingsUiState& state) {
  header(t, state, "设置");
  const Settings& s = *state.settings;

  char interval[24];
  std::snprintf(interval, sizeof(interval), "%s", settingsRefreshLabel(s.refreshMinutes));
  char coins[24];
  std::snprintf(coins, sizeof(coins), "%u 个", static_cast<unsigned>(settingsVisibleCoins(s.coinMask)));

  int16_t top = L::rowsTop;
  valueRow(t, top, L::rowHeight, "网络", s.wifiSsid[0] != '\0' ? s.wifiSsid : "未配置");
  top = static_cast<int16_t>(top + L::rowHeight);
  valueRow(t, top, L::rowHeight, "城市", s.place[0] != '\0' ? s.place : s.city);
  top = static_cast<int16_t>(top + L::rowHeight);
  valueRow(t, top, L::rowHeight, "刷新间隔", interval);
  top = static_cast<int16_t>(top + L::rowHeight);
  valueRow(t, top, L::rowHeight, "默认页", settingsPageLabel(s.defaultPage));
  top = static_cast<int16_t>(top + L::rowHeight);
  valueRow(t, top, L::rowHeight, "自选币种", coins);
  top = static_cast<int16_t>(top + L::rowHeight);

  const Rect power{L::margin, static_cast<int16_t>(top + 12), static_cast<int16_t>(L::width - L::margin * 2),
                   92};
  t.stroke(power, kInk, 3, 8);
  if (state.confirmPowerOff) {
    t.fill(power, kInk, 8);
  }
  body(t, power.x, static_cast<int16_t>(power.y + 58),
       state.confirmPowerOff ? "再点一次确认关机" : "关机", TextAlign::Center, power.width,
       state.confirmPowerOff ? Color::White : Color::Black);

  footer(t, state, "点一行修改", "60 秒无操作退出");
}

void drawWifi(DrawTarget& t, const SettingsUiState& state) {
  header(t, state, "选择网络", "重新扫描");
  const WifiList& list = *state.networks;
  const Settings& s = *state.settings;

  if (list.count == 0) {
    heading(t, 0, 400, "还没有扫描到网络", TextAlign::Center, L::width);
    body(t, 0, 452, "点右上角「重新扫描」", TextAlign::Center, L::width);
  }

  const uint8_t pageCount = settingsWifiPageCount(list.count);
  const uint8_t page = state.wifiPage < pageCount ? state.wifiPage : 0;
  const uint8_t first = static_cast<uint8_t>(page * kWifiRowsPerPage);
  uint8_t drawn = 0;

  int16_t top = L::rowsTop;
  for (uint8_t i = first; i < list.count && i < first + kWifiRowsPerPage; ++i) {
    const WifiEntry& entry = list.items[i];
    ++drawn;
    const bool current = s.wifiSsid[0] != '\0' && std::strcmp(entry.ssid, s.wifiSsid) == 0;
    const Color color = current ? Color::White : Color::Black;
    if (current) t.fill(Rect{0, top, L::width, L::rowHeight - 4}, kInk);

    heading(t, L::margin, static_cast<int16_t>(top + 52), entry.ssid, TextAlign::Left, 330, color);
    const char* tag = current ? "已连接" : (entry.saved ? "已保存" : (entry.secure ? nullptr : "开放"));
    if (tag != nullptr) {
      body(t, L::margin, static_cast<int16_t>(top + 88), tag, TextAlign::Left, 200, color);
    }
    // Four bars mapped from dBm: -50 and better is full scale.
    uint8_t bars = 1;
    if (entry.rssi > -55) {
      bars = 4;
    } else if (entry.rssi > -67) {
      bars = 3;
    } else if (entry.rssi > -78) {
      bars = 2;
    }
    signalBars(t, static_cast<int16_t>(L::rightEdge - 36), static_cast<int16_t>(top + 56), bars, color);
    if (entry.secure) {
      lockGlyph(t, static_cast<int16_t>(L::rightEdge - 59), static_cast<int16_t>(top + 62), color);
    }
    dashRule(t, static_cast<int16_t>(top + L::rowHeight - 6));
    top = static_cast<int16_t>(top + L::rowHeight);
  }

  heading(t, L::margin, static_cast<int16_t>(top + 52), "手动输入网络名", TextAlign::Left, 300);
  chevron(t, L::rightEdge - 22, static_cast<int16_t>(top + 46));

  if (pageCount > 1) {
    char pageLabel[32];
    std::snprintf(pageLabel, sizeof(pageLabel), "%u/%u 页 · 上下滑翻",
                  static_cast<unsigned>(page + 1), static_cast<unsigned>(pageCount));
    footer(t, state, "只显示 2.4GHz 网络", pageLabel);
  } else {
    footer(t, state, "只显示 2.4GHz 网络", "点一行连接");
  }
  (void)drawn;
}

// Draws whatever settingsLayoutKeys() placed: same rects the flow hit-tests.
void drawKeyboard(DrawTarget& t, const SettingsUiState& state, const char* primaryLabel) {
  SettingsKey keys[kKeyboardKeyMax];
  const uint8_t count = settingsLayoutKeys(state.keyboardLayer, primaryLabel, keys, kKeyboardKeyMax);
  for (uint8_t i = 0; i < count; ++i) {
    const SettingsKey& key = keys[i];
    if (key.label == nullptr) continue;
    keyCap(t, key.rect, key.label, key.action == SettingsKeyAction::Primary);
  }
}

void drawPassword(DrawTarget& t, const SettingsUiState& state) {
  const char* title = state.editingSsid
                          ? "输入网络名"
                          : (state.settings->wifiSsid[0] != '\0' ? state.settings->wifiSsid : "输入密码");
  header(t, state, title);

  const Rect field{L::margin, 140, static_cast<int16_t>(L::width - L::margin * 2), 76};
  t.stroke(field, kInk, 3, 8);
  const char* text0 = state.editing != nullptr ? state.editing : "";
  if (!state.secret || state.revealed) {
    heading(t, static_cast<int16_t>(field.x + 16), static_cast<int16_t>(field.y + 52), text0,
            TextAlign::Left, 300);
  } else {
    const size_t length = std::strlen(text0);
    for (size_t i = 0; i < length && i < 16; ++i) {
      t.fill(Rect{static_cast<int16_t>(field.x + 22 + static_cast<int16_t>(i) * 22),
                  static_cast<int16_t>(field.y + 32), 12, 12},
             kInk, 6);
    }
  }
  if (state.secret) {
    t.line(Point{static_cast<int16_t>(L::rightEdge - 96), field.y},
           Point{static_cast<int16_t>(L::rightEdge - 96), static_cast<int16_t>(field.y + field.height)}, 2,
           kInk);
    body(t, static_cast<int16_t>(L::rightEdge - 80), static_cast<int16_t>(field.y + 50),
         state.revealed ? "隐藏" : "显示", TextAlign::Center, 64);
  }

  if (state.status != nullptr) {
    const Rect strip{L::margin, 236, static_cast<int16_t>(L::width - L::margin * 2), 58};
    t.fill(strip, kInk, 6);
    body(t, strip.x, static_cast<int16_t>(strip.y + 40), state.status, TextAlign::Center, strip.width,
         Color::White);
  } else {
    body(t, L::margin, 274, state.editingSsid ? "输入要连接的 WiFi 名称" : "点「显示」可以核对输入的密码",
         TextAlign::Left, 400);
  }

  drawKeyboard(t, state, state.primaryLabel != nullptr ? state.primaryLabel : "连接");
  footer(t, state, "点上面的网络可以重选", "连上后自动返回", /*showStatus=*/false);
}

void drawCity(DrawTarget& t, const SettingsUiState& state) {
  header(t, state, "选择城市");
  const Settings& s = *state.settings;
  const char* current = s.place[0] != '\0' ? s.place : s.city;

  int16_t top = L::rowsTop;
  for (uint8_t i = 0; i < kBuiltinCityCount; ++i) {
    const BuiltinCity& city = builtinCity(i);
    const bool selected = std::strcmp(city.label, current) == 0 ||
                          std::strcmp(city.searchName, current) == 0;
    heading(t, L::margin + 12, static_cast<int16_t>(top + 58), city.label, TextAlign::Left, 300);
    char coords[32];
    std::snprintf(coords, sizeof(coords), "%.2f, %.2f", static_cast<double>(city.latitude),
                  static_cast<double>(city.longitude));
    body(t, L::rightEdge - 200, static_cast<int16_t>(top + 58), coords, TextAlign::Right, 200);
    if (selected) tick(t, static_cast<int16_t>(L::margin - 18), static_cast<int16_t>(top + 46));
    dashRule(t, static_cast<int16_t>(top + L::cityRowHeight - 6), L::margin + 12);
    top = static_cast<int16_t>(top + L::cityRowHeight);
  }

  heading(t, L::margin, static_cast<int16_t>(top + 56), "搜索其他城市", TextAlign::Left, 300);
  chevron(t, L::rightEdge - 22, static_cast<int16_t>(top + 50));
  footer(t, state, "点一行选定", "坐标一起保存");
}

void drawCitySearch(DrawTarget& t, const SettingsUiState& state) {
  header(t, state, "搜索城市");

  const Rect field{L::margin, 140, static_cast<int16_t>(L::width - L::margin * 2), 72};
  t.stroke(field, kInk, 3, 8);
  const char* query = state.editing != nullptr ? state.editing : "";
  body(t, static_cast<int16_t>(field.x + 16), static_cast<int16_t>(field.y + 46),
       query[0] != '\0' ? query : "拼音或英文，例如 Paris", TextAlign::Left, 380,
       query[0] != '\0' ? Color::Black : Color::DarkGray);
  cross(t, static_cast<int16_t>(L::rightEdge - 24), static_cast<int16_t>(field.y + 36), 9);

  if (state.status != nullptr) {
    body(t, L::margin, 262, state.status, TextAlign::Left, 420);
  } else {
    body(t, L::margin, 262, "城市名用拼音或英文，同名城市会出现候选", TextAlign::Left, 460);
  }

  drawKeyboard(t, state, state.primaryLabel != nullptr ? state.primaryLabel : "搜索");
  footer(t, state, "点候选选定", "中文名也可");
}

void drawCityHits(DrawTarget& t, const SettingsUiState& state) {
  header(t, state, "选择候选");
  const GeoCandidates* candidates = state.candidates;
  if (candidates == nullptr || candidates->count == 0) {
    heading(t, 0, 400, "没有找到这个城市", TextAlign::Center, L::width);
    body(t, 0, 452, "换个拼写再试一次", TextAlign::Center, L::width);
    footer(t, state, "‹ 返回重新输入");
    return;
  }

  int16_t top = L::rowsTop + 20;
  for (uint8_t i = 0; i < candidates->count && i < kGeoCandidateMax; ++i) {
    const GeoCandidate& candidate = candidates->items[i];
    heading(t, L::margin + 12, static_cast<int16_t>(top + 50), candidate.name, TextAlign::Left, 300);
    char region[64];
    std::snprintf(region, sizeof(region), "%s · %s", candidate.admin1, candidate.country);
    body(t, L::margin + 12, static_cast<int16_t>(top + 90), region, TextAlign::Left, 300);
    char coords[32];
    std::snprintf(coords, sizeof(coords), "%.2f, %.2f", static_cast<double>(candidate.latitude),
                  static_cast<double>(candidate.longitude));
    body(t, L::rightEdge - 150, static_cast<int16_t>(top + 90), coords, TextAlign::Right, 150);
    dashRule(t, static_cast<int16_t>(top + 108), L::margin + 12);
    top = static_cast<int16_t>(top + 116);
  }
  footer(t, state, "点一行选定", "同名看省份");
}

void drawCoins(DrawTarget& t, const SettingsUiState& state) {
  header(t, state, "自选币种");
  const uint8_t mask = state.settings->coinMask;
  const uint8_t visible = settingsVisibleCoins(mask);
  char count[24];
  std::snprintf(count, sizeof(count), "已选 %u/%u", static_cast<unsigned>(visible),
                static_cast<unsigned>(kCoinCount));
  body(t, L::rightEdge - 130, 44, count, TextAlign::Right, 130, Color::White);

  int16_t top = L::rowsTop;
  for (uint8_t i = 0; i < kCoinCount; ++i) {
    const CoinInfo& coin = coinInfo(i);
    const bool checked = (mask & (1u << i)) != 0;
    checkBox(t, L::margin, static_cast<int16_t>(top + 31), checked);
    heading(t, static_cast<int16_t>(L::margin + 52), static_cast<int16_t>(top + 60), coin.symbol,
            TextAlign::Left, 140);
    body(t, static_cast<int16_t>(L::margin + 158), static_cast<int16_t>(top + 60), coin.nameZh,
         TextAlign::Left, 160);
    body(t, L::rightEdge - 80, static_cast<int16_t>(top + 60), checked ? "已选" : "未选",
         TextAlign::Right, 80);
    dashRule(t, static_cast<int16_t>(top + 86));
    top = static_cast<int16_t>(top + 92);
  }
  footer(t, state, "至少 4 个", "点一行勾选");
}

void drawOptionList(DrawTarget& t, const SettingsUiState& state, const char* title, const char* const* labels,
                    uint8_t count, uint8_t selected, const char* hint) {
  header(t, state, title);
  int16_t top = L::rowsTop;
  for (uint8_t i = 0; i < count; ++i) {
    optionRow(t, top, L::rowHeight, labels[i], i == selected);
    top = static_cast<int16_t>(top + L::rowHeight);
  }
  footer(t, state, "点一行选定", hint);
}

}  // namespace

// Key placement, in one place. Letter layers are QWERTY with Shift/delete on the
// third row's shoulders; the digit layer is three rows of ten with delete moved to
// the bottom row (where a gap opens up), because ten keys leave no room for a
// shoulder. Rows are centred by their own width, so changing a layer cannot
// silently shift a key out from under the finger.
uint8_t settingsLayoutKeys(uint8_t layer, const char* primaryLabel, SettingsKey* out, uint8_t capacity) {
  static const char* const kLower[3] = {"qwertyuiop", "asdfghjkl", "zxcvbnm"};
  static const char* const kUpper[3] = {"QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM"};
  static const char* const kDigits[3] = {"1234567890", "-_=+()[]{}", ".,?!@#$%^&*"};
  const uint8_t clamped = layer < kKeyboardLayers ? layer : 0;
  const char* const* rows = clamped == 1 ? kUpper : (clamped == 2 ? kDigits : kLower);

  const int16_t rowPitch = static_cast<int16_t>(L::keyHeight + 10);
  const auto rowWidth = [](size_t count) {
    return static_cast<int16_t>(count * (L::keyWide + L::keyGap) - L::keyGap);
  };
  const auto place = [&](uint8_t index, int16_t x, int16_t y, int16_t width, const char* label,
                         char output, SettingsKeyAction action) -> uint8_t {
    if (index >= capacity) return index;
    out[index].rect = Rect{x, y, width, L::keyHeight};
    out[index].label = label;
    out[index].output = output;
    out[index].action = action;
    return static_cast<uint8_t>(index + 1);
  };

  // Each key needs its own null-terminated one-character label: pointing every
  // key at an offset inside the row string would make key i read "the rest of the
  // row" (and the drawing would ellipsise all of them).
  static char charLabels[kKeyboardLayers][kKeyboardKeyMax][2] = {{{0}}};

  uint8_t count = 0;
  const bool digits = clamped == 2;
  for (uint8_t r = 0; r < 3; ++r) {
    const char* keys = rows[r];
    const size_t length = std::strlen(keys);
    const int16_t y = static_cast<int16_t>(L::keyboardTop + r * rowPitch);
    if (digits) {
      // Ten keys per row: use the full width, no shoulders.
      const int16_t x0 = static_cast<int16_t>((L::width - rowWidth(length)) / 2);
      for (size_t i = 0; i < length; ++i) {
        char* label = charLabels[clamped][count];
        label[0] = keys[i];
        label[1] = '\0';
        count = place(count, static_cast<int16_t>(x0 + i * (L::keyWide + L::keyGap)), y, L::keyWide,
                      label, keys[i], SettingsKeyAction::Character);
      }
      continue;
    }

    const bool shoulderRow = (r == 2);
    int16_t x = 0;
    if (shoulderRow) {
      count = place(count, L::keyX0, y, 64, "Shift", 0, SettingsKeyAction::Shift);
      x = static_cast<int16_t>(L::keyX0 + 64 + L::keyGap);
    } else {
      x = static_cast<int16_t>((L::width - rowWidth(length)) / 2);
    }
    for (size_t i = 0; i < length; ++i) {
      char* label = charLabels[clamped][count];
      label[0] = keys[i];
      label[1] = '\0';
      count = place(count, x, y, L::keyWide, label, keys[i], SettingsKeyAction::Character);
      x = static_cast<int16_t>(x + L::keyWide + L::keyGap);
    }
    if (shoulderRow) {
      count = place(count, static_cast<int16_t>(x + L::keyGap), y, 64, "Del", 0,
                    SettingsKeyAction::Delete);
    }
  }

  // Bottom row.
  const int16_t y = static_cast<int16_t>(L::keyboardTop + 3 * rowPitch);
  if (digits) {
    int16_t x = L::keyX0;
    count = place(count, x, y, 70, "ABC", 0, SettingsKeyAction::Mode);
    x = static_cast<int16_t>(x + 70 + L::keyGap);
    count = place(count, x, y, 200, "空格", ' ', SettingsKeyAction::Space);
    x = static_cast<int16_t>(x + 200 + L::keyGap);
    count = place(count, x, y, 70, "Del", 0, SettingsKeyAction::Delete);
    x = static_cast<int16_t>(x + 70 + L::keyGap);
    count = place(count, x, y, 140, primaryLabel, 0, SettingsKeyAction::Primary);
  } else {
    int16_t x = L::keyX0;
    count = place(count, x, y, 80, "123", 0, SettingsKeyAction::Mode);
    x = static_cast<int16_t>(x + 80 + L::keyGap);
    count = place(count, x, y, 250, "空格", ' ', SettingsKeyAction::Space);
    x = static_cast<int16_t>(x + 250 + L::keyGap);
    count = place(count, x, y, 150, primaryLabel, 0, SettingsKeyAction::Primary);
  }
  return count;
}

uint8_t settingsWifiPageCount(uint8_t networkCount) {
  if (networkCount == 0) return 1;
  return static_cast<uint8_t>((networkCount + kWifiRowsPerPage - 1) / kWifiRowsPerPage);
}

const char* settingsRefreshLabel(uint16_t minutes) {
  switch (minutes) {
    case 5: return "5 分钟";
    case 10: return "10 分钟";
    case 15: return "15 分钟";
    case 30: return "30 分钟";
    case 60: return "1 小时";
    case 120: return "2 小时";
    case 1: return "1 分钟";
    default: return "自定义";
  }
}

const char* settingsPageLabel(uint8_t page) {
  switch (page) {
    case 0: return "数字货币";
    case 1: return "当前天气";
    case 2: return "逐小时";
    case 3: return "空气质量";
    default: return "数字货币";
  }
}

const char* settingsScreenTitle(SettingsScreen screen) {
  switch (screen) {
    case SettingsScreen::Wifi: return "选择网络";
    case SettingsScreen::Password: return "输入密码";
    case SettingsScreen::City: return "选择城市";
    case SettingsScreen::CitySearch: return "搜索城市";
    case SettingsScreen::CityHits: return "选择候选";
    case SettingsScreen::Coins: return "自选币种";
    case SettingsScreen::Interval: return "刷新间隔";
    case SettingsScreen::DefaultPage: return "默认页";
    default: return "设置";
  }
}

uint8_t settingsVisibleCoins(uint8_t mask) {
  uint8_t count = 0;
  for (uint8_t i = 0; i < kCoinCount; ++i) {
    if ((mask & (1u << i)) != 0) ++count;
  }
  return count;
}

void renderSettingsScreen(freeink::ui::DrawTarget& target, const SettingsUiState& state) {
  static const char* const kIntervalLabels[] = {"1 分钟", "5 分钟", "10 分钟",
                                                "15 分钟", "30 分钟", "1 小时"};
  static const uint16_t kIntervalValues[] = {1, 5, 10, 15, 30, 60};
#if FREEINK_DEVICE_PAPERS3
  static const char* const kPageLabels[] = {"首页", "当前天气", "逐小时", "空气质量", "数字货币"};
  static constexpr uint8_t kPageOptionCount = 5;
#else
  static const char* const kPageLabels[] = {"数字货币", "当前天气", "逐小时", "空气质量"};
  static constexpr uint8_t kPageOptionCount = 4;
#endif
  if (state.settings == nullptr) return;

  // Clear the whole frame first: the panel diff only sees what changed, so
  // anything this screen does not draw would otherwise keep the previous
  // screen's pixels and the two would show through each other.
  target.fill(Rect{0, 0, L::width, L::height}, kPaper);

  switch (state.screen) {
    case SettingsScreen::Home:
      drawHome(target, state);
      break;
    case SettingsScreen::Wifi:
      if (state.networks != nullptr) drawWifi(target, state);
      break;
    case SettingsScreen::Password:
      drawPassword(target, state);
      break;
    case SettingsScreen::City:
      drawCity(target, state);
      break;
    case SettingsScreen::CitySearch:
      drawCitySearch(target, state);
      break;
    case SettingsScreen::CityHits:
      drawCityHits(target, state);
      break;
    case SettingsScreen::Coins:
      drawCoins(target, state);
      break;
    case SettingsScreen::Interval: {
      uint8_t selected = 4;
      for (uint8_t i = 0; i < 6; ++i) {
        if (kIntervalValues[i] == state.settings->refreshMinutes) selected = i;
      }
      drawOptionList(target, state, "刷新间隔", kIntervalLabels, 6, selected, "省电靠它");
      break;
    }
    case SettingsScreen::DefaultPage:
      drawOptionList(target, state, "默认页", kPageLabels, kPageOptionCount, state.settings->defaultPage,
                     "冷启动落在哪页");
      break;
  }
}

}  // namespace weather
