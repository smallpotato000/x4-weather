#pragma once

#include <FreeInkUICore.h>

#include <stdint.h>

#include "SettingsStore.h"
#include "WeatherData.h"  // GeoCandidates, CoinInfo

namespace weather {

// Screens of the touch settings UI. Drawn in the PaperS3's 540x960 portrait
// frame; the flow that feeds them (WiFi scan, connect, NVS writes) lives in
// main.cpp so this file stays pure drawing and can be rendered on the host.
enum class SettingsScreen : uint8_t {
  Home,
  Wifi,
  Password,
  City,        // the built-in shortlist
  CitySearch,  // typing a name (field + keyboard)
  CityHits,    // online candidates for what was typed
  Coins,
  Interval,
  DefaultPage,
  About,
};
static constexpr uint8_t kSettingsScreenCount = 9;

static constexpr uint8_t kWifiListMax = 12;
struct WifiEntry {
  char ssid[33] = {0};
  int8_t rssi = -127;  // dBm; -127 = unknown
  bool secure = false;
  bool saved = false;  // matches the credentials already stored
};
struct WifiList {
  uint8_t count = 0;
  WifiEntry items[kWifiListMax];
};

struct SettingsUiState {
  SettingsScreen screen = SettingsScreen::Home;
  const Settings* settings = nullptr;
  const WifiList* networks = nullptr;
  const GeoCandidates* candidates = nullptr;  // online hits for CitySearch

  // Transient UI state owned by the flow.
  const char* status = nullptr;            // e.g. "密码错误，请再试一次"
  const char* editing = nullptr;           // password or city query buffer
  bool secret = false;                     // mask `editing` (password)
  bool revealed = false;                   // "显示" toggle
  bool editingSsid = false;                // the field edits the network name
  const char* aboutVersion = nullptr;      // "v1.0.0"; the About screen's strings
  const char* aboutBoard = nullptr;        // board name
  const char* aboutBuild = nullptr;        // build stamp
  const char* aboutUpstream = nullptr;     // SDK / PR reference
  const char* primaryLabel = "连接";        // bottom-right key caption
  uint8_t selectedRow = 0;                 // highlighted row
  uint8_t keyboardLayer = 0;               // 0 lower, 1 upper, 2 digits/symbols
  uint8_t wifiPage = 0;                    // WiFi list is paged, not scrolled
  bool confirmPowerOff = false;            // first tap on 关机 already happened
  uint8_t batteryPercent = 82;
  bool batteryKnown = true;
};

// Draws one whole screen. The caller owns the panel refresh.
void renderSettingsScreen(freeink::ui::DrawTarget& target, const SettingsUiState& state);

// Keyboard layers: 0 lower, 1 upper, 2 digits/symbols. Layer 2 is what makes
// numeric WiFi passwords possible at all.
static constexpr uint8_t kKeyboardLayers = 3;

// What a key does when tapped.
enum class SettingsKeyAction : uint8_t {
  None,     // nothing drawn here (a gap)
  Character,
  Shift,
  Delete,
  Mode,    // letters <-> digits
  Space,
  Primary, // 连接 / 搜索 / 下一步
};

// One placed key. The drawing and the flow's hit testing both consume this list,
// so a tap can never disagree with the glyph under the finger (two hand-written
// copies of the row arithmetic did disagree on the wider digit layer).
struct SettingsKey {
  freeink::ui::Rect rect{};
  const char* label = nullptr;
  char output = 0;
  SettingsKeyAction action = SettingsKeyAction::None;
};

static constexpr uint8_t kKeyboardKeyMax = 40;
uint8_t settingsLayoutKeys(uint8_t layer, const char* primaryLabel, SettingsKey* out, uint8_t capacity);

// WiFi list paging.
static constexpr uint8_t kWifiRowsPerPage = 6;
uint8_t settingsWifiPageCount(uint8_t networkCount);

// Labels shared with the flow.
const char* settingsRefreshLabel(uint16_t minutes);
const char* settingsPageLabel(uint8_t page);
const char* settingsScreenTitle(SettingsScreen screen);
uint8_t settingsVisibleCoins(uint8_t mask);

// Row geometry, so the flow's hit testing agrees with the drawing.
struct SettingsLayout {
  static constexpr int16_t width = 540;
  static constexpr int16_t height = 960;
  static constexpr int16_t margin = 28;
  static constexpr int16_t headerBottom = 104;
  static constexpr int16_t rowsTop = 140;
  static constexpr int16_t rowHeight = 104;
  static constexpr int16_t cityRowHeight = 96;  // the shortlist has eight rows
  static constexpr int16_t footerBaseline = 928;
  static constexpr int16_t rightEdge = width - margin;
  // Keyboard (password screen).
  static constexpr int16_t keyX0 = 23;
  static constexpr int16_t keyWide = 44;
  static constexpr int16_t keyGap = 6;
  static constexpr int16_t keyHeight = 104;
  static constexpr int16_t keyboardTop = 330;
};

}  // namespace weather
