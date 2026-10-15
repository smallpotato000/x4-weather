// Host-only mockups of the touch settings screens for the PaperS3 port.
//
// These are *design sketches*, not the shipping UI: they draw through the same
// FreeInkUI DisplayTarget and the same baked fonts the firmware uses, into a
// 960x540 panel framebuffer with the portrait orientation, so the logical frame
// below is 540x960 (PaperS3 held tall, like the data pages).
//
// Build/run through tools/sketch/render.sh <screen>; the Chinese strings here
// are part of the font charset (tools/gen_all.sh scans this directory).
//
//   settings  设置首页            wifi    选择网络
//   password  输入密码（键盘）     city    选择城市（候选）
//   coins     自选币种（多选）
#include <FreeInkUIDisplayTarget.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "CjkGlyphSource.h"
#include "WeatherUi.h"
#include "fonts/WeatherTitleFont.h"  // kWeatherTitleFont's home

namespace {

using freeink::ui::BitmapFont;
using freeink::ui::Color;
using freeink::ui::DisplayTarget;
using freeink::ui::FontId;
using freeink::ui::Paint;
using freeink::ui::Point;
using freeink::ui::Rect;
using freeink::ui::TextAlign;
using freeink::ui::TextStyle;

constexpr int16_t kPanelWidth = 960;  // ED047TC1 landscape RAM (PaperS3)
constexpr int16_t kPanelHeight = 540;
constexpr int16_t kPanelWidthBytes = kPanelWidth / 8;
uint8_t gFramebuffer[kPanelWidthBytes * kPanelHeight];

// Logical portrait frame everything below is laid out in.
constexpr int16_t kWidth = 540;
constexpr int16_t kHeight = 960;
constexpr int16_t kMargin = 28;
constexpr int16_t kHeaderBottom = 104;
constexpr int16_t kFooterBaseline = 928;
constexpr int16_t kRowsTop = 140;
constexpr int16_t kRowHeight = 104;
constexpr int16_t kRightEdge = kWidth - kMargin;  // 512

constexpr FontId kBody = weather::kFontBody;    // Noto Sans, 24 px
constexpr FontId kTitle = weather::kFontTitle;  // WeatherTitle, 34 px

const Paint kInk = Paint::solid(Color::Black);
const Paint kPaper = Paint::solid(Color::White);

// --- text -------------------------------------------------------------------

void text(DisplayTarget& t, FontId slot, const BitmapFont& font, int16_t x, int16_t baseline,
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
          int16_t box = 200, Color color = Color::Black) {
  text(t, kBody, freeink::ui::kNotoSansFont, x, baseline, s, align, box, color);
}

void heading(DisplayTarget& t, int16_t x, int16_t baseline, const char* s, TextAlign align = TextAlign::Left,
             int16_t box = 260, Color color = Color::Black) {
  text(t, kTitle, freeink::ui::kWeatherTitleFont, x, baseline, s, align, box, color);
}

// --- chrome -----------------------------------------------------------------

void batteryGlyph(DisplayTarget& t, int16_t x, int16_t y, uint8_t percent) {
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

// A black title bar with an optional back chevron, a right-aligned action label
// and the battery, exactly like the data pages' header.
void header(DisplayTarget& t, const char* label, const char* action = nullptr, bool back = false,
            uint8_t battery = 82) {
  t.fill(Rect{0, 0, kWidth, kHeaderBottom}, kInk);
  if (back) {
    const int16_t x = kMargin + 4;
    t.line(Point{static_cast<int16_t>(x + 10), 36}, Point{x, 52}, 3, kPaper);
    t.line(Point{x, 52}, Point{static_cast<int16_t>(x + 10), 68}, 3, kPaper);
  }
  heading(t, back ? kMargin + 26 : kMargin, 66, label, TextAlign::Left, 250, Color::White);
  if (action != nullptr) {
    body(t, kRightEdge - 150, 44, action, TextAlign::Right, 150, Color::White);
  }
  char percent[8];
  std::snprintf(percent, sizeof(percent), "%u%%", static_cast<unsigned>(battery));
  body(t, kRightEdge - 62, 88, percent, TextAlign::Right, 62, Color::White);
  batteryGlyph(t, static_cast<int16_t>(kRightEdge - 62 - 6 - 34), 80, battery);
}

void footer(DisplayTarget& t, const char* left, const char* right = nullptr) {
  body(t, kMargin, kFooterBaseline, left, TextAlign::Left, 280);
  if (right != nullptr) {
    body(t, kRightEdge - 230, kFooterBaseline, right, TextAlign::Right, 230);
  }
}

void dashedRule(DisplayTarget& t, int16_t y, int16_t x0 = kMargin, int16_t x1 = kRightEdge) {
  for (int16_t x = x0; x < x1; x += 9) {
    const int16_t end = static_cast<int16_t>(x + 5 < x1 ? x + 5 : x1);
    t.line(Point{x, y}, Point{end, y}, 1, kInk);
  }
}

void chevron(DisplayTarget& t, int16_t x, int16_t centerY) {
  t.line(Point{x, static_cast<int16_t>(centerY - 10)}, Point{static_cast<int16_t>(x + 9), centerY}, 3, kInk);
  t.line(Point{static_cast<int16_t>(x + 9), centerY}, Point{x, static_cast<int16_t>(centerY + 10)}, 3, kInk);
}

// Four ascending bars; `level` 0..4 filled.
void signalBars(DisplayTarget& t, int16_t x, int16_t baseline, uint8_t level, Color color = Color::Black) {
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

void lockGlyph(DisplayTarget& t, int16_t x, int16_t baseline, Color color = Color::Black) {
  const Paint ink = Paint::solid(color);
  const int16_t bodyTop = static_cast<int16_t>(baseline - 13);
  t.fill(Rect{x, bodyTop, 15, 13}, ink);
  // Shackle: a squared arch above the body.
  t.line(Point{static_cast<int16_t>(x + 3), bodyTop},
         Point{static_cast<int16_t>(x + 3), static_cast<int16_t>(bodyTop - 7)}, 2, ink);
  t.line(Point{static_cast<int16_t>(x + 3), static_cast<int16_t>(bodyTop - 7)},
         Point{static_cast<int16_t>(x + 12), static_cast<int16_t>(bodyTop - 7)}, 2, ink);
  t.line(Point{static_cast<int16_t>(x + 12), static_cast<int16_t>(bodyTop - 7)},
         Point{static_cast<int16_t>(x + 12), bodyTop}, 2, ink);
}

void checkBox(DisplayTarget& t, int16_t x, int16_t y, bool checked, int16_t size = 30) {
  t.stroke(Rect{x, y, size, size}, kInk, 3, 4);
  if (!checked) return;
  const int16_t cx = static_cast<int16_t>(x + size / 2);
  const int16_t cy = static_cast<int16_t>(y + size / 2);
  t.line(Point{static_cast<int16_t>(x + 7), cy}, Point{static_cast<int16_t>(cx - 1),
                                                       static_cast<int16_t>(cy + 7)},
         4, kInk);
  t.line(Point{static_cast<int16_t>(cx - 1), static_cast<int16_t>(cy + 7)},
         Point{static_cast<int16_t>(x + size - 7), static_cast<int16_t>(cy - 8)}, 4, kInk);
}

void cross(DisplayTarget& t, int16_t centerX, int16_t centerY, int16_t arm) {
  t.line(Point{static_cast<int16_t>(centerX - arm), static_cast<int16_t>(centerY - arm)},
         Point{static_cast<int16_t>(centerX + arm), static_cast<int16_t>(centerY + arm)}, 3, kInk);
  t.line(Point{static_cast<int16_t>(centerX - arm), static_cast<int16_t>(centerY + arm)},
         Point{static_cast<int16_t>(centerX + arm), static_cast<int16_t>(centerY - arm)}, 3, kInk);
}

// One settings row: big label, right-aligned value, chevron.
void settingsRow(DisplayTarget& t, int16_t top, const char* label, const char* value) {
  const int16_t baseline = static_cast<int16_t>(top + 58);
  heading(t, kMargin, baseline, label, TextAlign::Left, 240);
  if (value != nullptr) {
    body(t, kRightEdge - 300, baseline, value, TextAlign::Right, 262);
  }
  chevron(t, kRightEdge - 22, static_cast<int16_t>(top + 52));
  dashedRule(t, static_cast<int16_t>(top + kRowHeight - 6));
}

// --- screens ----------------------------------------------------------------

void drawSettings(DisplayTarget& t) {
  header(t, "设置");
  settingsRow(t, kRowsTop, "网络", "akiko-home · 已连接");
  settingsRow(t, static_cast<int16_t>(kRowsTop + kRowHeight), "城市", "武汉 · 中国");
  settingsRow(t, static_cast<int16_t>(kRowsTop + kRowHeight * 2), "刷新间隔", "30 分钟");
  settingsRow(t, static_cast<int16_t>(kRowsTop + kRowHeight * 3), "默认页", "数字货币");
  settingsRow(t, static_cast<int16_t>(kRowsTop + kRowHeight * 4), "自选币种", "7 个");

  // Power off is a full-width outlined action, not a value row.
  const int16_t top = static_cast<int16_t>(kRowsTop + kRowHeight * 5 + 12);
  const Rect box{kMargin, top, static_cast<int16_t>(kWidth - kMargin * 2), 92};
  t.stroke(box, kInk, 3, 8);
  body(t, box.x, static_cast<int16_t>(top + 58), "关机", TextAlign::Center, box.width);
  footer(t, "点一行修改", "30 秒无操作自动退出");
}

void drawWifi(DisplayTarget& t) {
  header(t, "选择网络", "重新扫描", true);
  struct Network {
    const char* ssid;
    uint8_t bars;
    bool locked;
    const char* tag;
  };
  const Network networks[] = {
      {"akiko-home", 4, true, "已连接"},
      {"M5-PaperS3", 4, true, "已保存"},
      {"Xiaomi_AX3000", 3, true, nullptr},
      {"ChinaNet-8fZq", 2, true, nullptr},
      {"CMCC-Guest", 2, false, "开放"},
      {"TP-LINK_2.4G", 1, true, nullptr},
  };
  int16_t top = kRowsTop;
  for (const Network& network : networks) {
    const bool current = network.tag != nullptr && std::strcmp(network.tag, "已连接") == 0;
    const Color color = current ? Color::White : Color::Black;
    if (current) t.fill(Rect{0, top, kWidth, kRowHeight}, kInk);
    heading(t, kMargin, static_cast<int16_t>(top + 52), network.ssid, TextAlign::Left, 330, color);
    if (network.tag != nullptr) {
      body(t, kMargin, static_cast<int16_t>(top + 88), network.tag, TextAlign::Left, 200, color);
    }
    signalBars(t, static_cast<int16_t>(kRightEdge - 36), static_cast<int16_t>(top + 56), network.bars, color);
    if (network.locked) {
      lockGlyph(t, static_cast<int16_t>(kRightEdge - 59), static_cast<int16_t>(top + 62), color);
    }
    dashedRule(t, static_cast<int16_t>(top + kRowHeight - 6));
    top = static_cast<int16_t>(top + kRowHeight);
  }
  heading(t, kMargin, static_cast<int16_t>(top + 58), "手动输入网络名", TextAlign::Left, 300);
  chevron(t, kRightEdge - 22, static_cast<int16_t>(top + 52));
  footer(t, "只显示 2.4GHz 网络", "点一行连接");
}

void keyCap(DisplayTarget& t, Rect box, const char* label, bool primary = false) {
  t.fill(box, primary ? kInk : kPaper, 6);
  t.stroke(box, kInk, primary ? 3 : 2, 6);
  body(t, box.x, static_cast<int16_t>(box.y + box.height / 2 + 9), label, TextAlign::Center, box.width,
       primary ? Color::White : Color::Black);
}

void drawPassword(DisplayTarget& t) {
  header(t, "akiko-home", nullptr, true);

  // Masked field with a reveal toggle.
  const Rect field{kMargin, 140, static_cast<int16_t>(kWidth - kMargin * 2), 76};
  t.stroke(field, kInk, 3, 8);
  for (uint8_t i = 0; i < 8; ++i) {
    t.fill(Rect{static_cast<int16_t>(field.x + 22 + i * 22), static_cast<int16_t>(field.y + 32), 12, 12}, kInk,
           6);
  }
  t.line(Point{static_cast<int16_t>(kRightEdge - 96), field.y}, Point{static_cast<int16_t>(kRightEdge - 96),
                                                                     static_cast<int16_t>(field.y + field.height)},
         2, kInk);
  body(t, static_cast<int16_t>(kRightEdge - 80), static_cast<int16_t>(field.y + 50), "显示",
       TextAlign::Center, 64);

  // Failure strip: an inverted bar is the e-ink way to shout.
  const Rect error{kMargin, 236, static_cast<int16_t>(kWidth - kMargin * 2), 58};
  t.fill(error, kInk, 6);
  body(t, error.x, static_cast<int16_t>(error.y + 40), "密码错误，请再试一次", TextAlign::Center, error.width,
       Color::White);

  // QWERTY. Three letter rows plus a bottom action row.
  constexpr int16_t kX0 = 23;
  constexpr int16_t kWide = 44;
  constexpr int16_t kGap = 6;
  constexpr int16_t kKeyHeight = 104;
  const char* rows[3] = {"qwertyuiop", "asdfghjkl", "zxcvbnm"};
  int16_t top = 330;
  for (uint8_t r = 0; r < 3; ++r) {
    const size_t count = std::strlen(rows[r]);
    int16_t x = static_cast<int16_t>(kX0 + (r == 1 ? 25 : (r == 2 ? 50 : 0)));
    for (size_t i = 0; i < count; ++i) {
      char label[2] = {rows[r][i], '\0'};
      keyCap(t, Rect{x, top, kWide, kKeyHeight}, label);
      x = static_cast<int16_t>(x + kWide + kGap);
    }
    top = static_cast<int16_t>(top + kKeyHeight + 10);
  }
  // Shift / delete ride on the third row's shoulders.
  keyCap(t, Rect{kX0, static_cast<int16_t>(top - kKeyHeight - 10), 64, kKeyHeight}, "Shift");
  keyCap(t, Rect{static_cast<int16_t>(kX0 + 64 + kGap + 7 * (kWide + kGap) + kGap),
                 static_cast<int16_t>(top - kKeyHeight - 10), 64, kKeyHeight},
         "Del");

  // Bottom row: symbol layer, space, connect.
  keyCap(t, Rect{kX0, top, 80, kKeyHeight}, "123");
  keyCap(t, Rect{static_cast<int16_t>(kX0 + 80 + kGap), top, 250, kKeyHeight}, "空格");
  keyCap(t, Rect{static_cast<int16_t>(kX0 + 80 + kGap + 250 + kGap), top, 150, kKeyHeight}, "连接", true);

  footer(t, "也可以点上面的网络重选", "连上后自动返回");
}

void drawCity(DisplayTarget& t) {
  header(t, "选择城市", nullptr, true);

  const Rect search{kMargin, 136, static_cast<int16_t>(kWidth - kMargin * 2), 72};
  t.stroke(search, kInk, 3, 8);
  body(t, static_cast<int16_t>(search.x + 16), static_cast<int16_t>(search.y + 48), "Paris", TextAlign::Left,
       320);
  cross(t, static_cast<int16_t>(kRightEdge - 24), static_cast<int16_t>(search.y + 36), 9);

  struct Candidate {
    const char* name;
    const char* region;
    const char* coords;
  };
  const Candidate candidates[] = {
      {"巴黎", "法兰西岛 · 法国", "48.85, 2.35"},
      {"Paris", "得克萨斯州 · 美国", "33.66, -95.56"},
      {"Paris", "田纳西州 · 美国", "36.30, -88.33"},
      {"巴黎", "肯塔基州 · 美国", "38.21, -84.25"},
      {"Paris", "伊利诺伊州 · 美国", "39.61, -87.70"},
  };
  int16_t top = 232;
  for (const Candidate& candidate : candidates) {
    heading(t, kMargin + 12, static_cast<int16_t>(top + 50), candidate.name, TextAlign::Left, 300);
    body(t, kMargin + 12, static_cast<int16_t>(top + 88), candidate.region, TextAlign::Left, 300);
    body(t, kRightEdge - 150, static_cast<int16_t>(top + 88), candidate.coords, TextAlign::Right, 150);
    dashedRule(t, static_cast<int16_t>(top + 104), kMargin + 12);
    top = static_cast<int16_t>(top + 112);
  }
  heading(t, kMargin, static_cast<int16_t>(top + 56), "从内置城市里选", TextAlign::Left, 300);
  chevron(t, kRightEdge - 22, static_cast<int16_t>(top + 50));
  footer(t, "点一行选定", "坐标与时区一起保存");
}

void drawCoins(DisplayTarget& t) {
  header(t, "自选币种", "已选 7/8", true);
  struct Coin {
    const char* symbol;
    const char* name;
    bool selected;
  };
  const Coin coins[] = {
      {"BTC", "比特币", true},  {"ETH", "以太坊", true}, {"XRP", "瑞波币", true}, {"BNB", "币安币", true},
      {"SOL", "Solana", true},  {"TRX", "波场", true},   {"XMR", "门罗币", true},
  };
  int16_t top = kRowsTop;
  for (const Coin& coin : coins) {
    checkBox(t, kMargin, static_cast<int16_t>(top + 31), coin.selected);
    heading(t, static_cast<int16_t>(kMargin + 52), static_cast<int16_t>(top + 60), coin.symbol,
            TextAlign::Left, 140);
    body(t, static_cast<int16_t>(kMargin + 158), static_cast<int16_t>(top + 60), coin.name, TextAlign::Left,
         160);
    body(t, kRightEdge - 80, static_cast<int16_t>(top + 60), coin.selected ? "已选" : "未选",
         TextAlign::Right, 80);
    dashedRule(t, static_cast<int16_t>(top + 86));
    top = static_cast<int16_t>(top + 92);
  }
  // Add-more affordance: a dashed frame rather than a checkbox.
  const Rect add{kMargin, static_cast<int16_t>(top + 6), static_cast<int16_t>(kWidth - kMargin * 2), 74};
  for (int16_t x = add.x; x < add.x + add.width; x += 11) {
    t.line(Point{x, add.y}, Point{static_cast<int16_t>(x + 6 < add.x + add.width ? x + 6 : add.x + add.width),
                                  add.y},
           1, kInk);
    t.line(Point{x, static_cast<int16_t>(add.y + add.height)},
           Point{static_cast<int16_t>(x + 6 < add.x + add.width ? x + 6 : add.x + add.width),
                 static_cast<int16_t>(add.y + add.height)},
           1, kInk);
  }
  body(t, add.x, static_cast<int16_t>(add.y + 48), "添加币种（狗狗币 / ADA / …）", TextAlign::Center,
       add.width);
  footer(t, "至少 4 个 · 最多 8 个", "点一行勾选");
}

int writePbm(const char* path) {
  FILE* file = std::fopen(path, "wb");
  if (file == nullptr) return 1;
  std::fprintf(file, "P4\n%d %d\n", kPanelWidth, kPanelHeight);
  std::fwrite(gFramebuffer, 1, sizeof(gFramebuffer), file);
  std::fclose(file);
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  const char* out = argc > 1 ? argv[1] : "sketch.pbm";
  const char* screen = argc > 2 ? argv[2] : "settings";

  std::memset(gFramebuffer, 0xFF, sizeof(gFramebuffer));  // white paper
  freeink::ui::DisplayTarget target(gFramebuffer, kPanelWidth, kPanelHeight, kPanelWidthBytes);
  weather::CjkGlyphSource cjk;
  weather::configureWeatherFonts(target, cjk);

  if (std::strcmp(screen, "settings") == 0) {
    drawSettings(target);
  } else if (std::strcmp(screen, "wifi") == 0) {
    drawWifi(target);
  } else if (std::strcmp(screen, "password") == 0) {
    drawPassword(target);
  } else if (std::strcmp(screen, "city") == 0) {
    drawCity(target);
  } else if (std::strcmp(screen, "coins") == 0) {
    drawCoins(target);
  } else {
    std::fprintf(stderr, "unknown screen: %s\\n", screen);
    return 2;
  }
  return writePbm(out);
}
