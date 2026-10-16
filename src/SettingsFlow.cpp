#include "SettingsFlow.h"

#if FREEINK_DEVICE_PAPERS3

#include <Arduino.h>
#include <WiFi.h>

#include <cstdio>
#include <cstring>

#include "CityList.h"
#include "EInkDisplay.h"
#include "InputManager.h"
#include "SettingsStore.h"
#include "TouchIdle.h"
#include "WeatherClient.h"

namespace weather {
namespace {

using freeink::ui::DisplayTarget;
using freeink::ui::DrawTarget;
using freeink::ui::Point;
using freeink::ui::Rect;

using L = SettingsLayout;

constexpr uint32_t kIdleExitMs = 60000;   // no touch for this long -> leave settings
constexpr uint32_t kLongPressMs = 2500;   // hold to power off, like the data pages

SettingsFlowContext* g = nullptr;

// --- transient UI state -----------------------------------------------------
SettingsUiState ui;
char passwordBuffer[65] = {0};
char ssidBuffer[33] = {0};
char cityQuery[40] = {0};
char statusBuffer[80] = {0};
GeoCandidates cityHits;
bool sleeping = false;
bool confirmOff = false;
uint32_t lastActivity = 0;

// --- plumbing ---------------------------------------------------------------

void setStatus(const char* text) {
  std::snprintf(statusBuffer, sizeof(statusBuffer), "%s", text != nullptr ? text : "");
}

void clearStatus() { statusBuffer[0] = '\0'; }

void saveSettings() { SettingsStore::save(*g->settings); }

void syncUi() {
  ui.settings = g->settings;
  ui.networks = g->networks;
  ui.candidates = &cityHits;
  ui.status = statusBuffer[0] != '\0' ? statusBuffer : nullptr;
  ui.batteryKnown = g->batteryKnown;
  ui.batteryPercent = g->batteryPercent;
  ui.confirmPowerOff = confirmOff;

  switch (ui.screen) {
    case SettingsScreen::Password:
      ui.editing = ui.editingSsid ? ssidBuffer : passwordBuffer;
      ui.secret = !ui.editingSsid;
      ui.primaryLabel = ui.editingSsid ? "下一步" : "连接";
      break;
    case SettingsScreen::CitySearch:
      ui.editing = cityQuery;
      ui.secret = false;
      ui.primaryLabel = "搜索";
      break;
    default:
      ui.editing = nullptr;
      ui.secret = false;
      ui.primaryLabel = "连接";
      break;
  }
}

// `full` runs the clean waveform: use it whenever the layout changes wholesale
// (entering settings, switching screens, status text that replaces a longer
// line), and the fast differential path for single-key updates.
void repaint(bool full = false) {
  syncUi();
  renderSettingsScreen(*g->target, ui);
  g->display->displayBuffer(full ? EInkDisplay::FULL_REFRESH : EInkDisplay::FAST_REFRESH);
  lastActivity = millis();
}

void goTo(SettingsScreen screen) {
  ui.screen = screen;
  confirmOff = false;
  clearStatus();
  Serial.printf("[settings] screen -> %s\n", settingsScreenTitle(screen));
  Serial.flush();
  repaint(/*full=*/true);
}

bool inside(const Point& p, int16_t x, int16_t y, int16_t w, int16_t h) {
  return p.x >= x && p.x < x + w && p.y >= y && p.y < y + h;
}

// Row index for a tap in a list starting at `top`, or -1.
int rowAt(const Point& p, int16_t top, int16_t rowHeight, uint8_t count) {
  if (p.y < top) return -1;
  const int index = (p.y - top) / rowHeight;
  if (index < 0 || index >= static_cast<int>(count)) return -1;
  return index;
}

bool headerBackTapped(const Point& p) { return p.x < 80 && p.y < L::headerBottom; }
bool headerActionTapped(const Point& p) { return p.x > L::rightEdge - 170 && p.y < L::headerBottom; }

// --- WiFi -------------------------------------------------------------------

void scanWifi() {
  setStatus("正在扫描附近网络…");
  repaint(true);

  WiFi.mode(WIFI_STA);
  const int16_t found = WiFi.scanNetworks();
  WifiList& list = *g->networks;
  list.count = 0;

  for (int16_t i = 0; i < found; ++i) {
    const String ssid = WiFi.SSID(i);
    if (ssid.length() == 0 || ssid.length() > 32) continue;
    const int32_t rssi = WiFi.RSSI(i);

    // One row per SSID: neighbours and mesh nodes repeat the same name.
    bool merged = false;
    for (uint8_t k = 0; k < list.count; ++k) {
      if (std::strcmp(list.items[k].ssid, ssid.c_str()) == 0) {
        if (rssi > list.items[k].rssi) list.items[k].rssi = static_cast<int8_t>(rssi);
        if (WiFi.encryptionType(i) != WIFI_AUTH_OPEN) list.items[k].secure = true;
        merged = true;
        break;
      }
    }
    if (merged) continue;
    if (list.count >= kWifiListMax) break;

    WifiEntry& entry = list.items[list.count++];
    std::snprintf(entry.ssid, sizeof(entry.ssid), "%s", ssid.c_str());
    entry.rssi = static_cast<int8_t>(rssi);
    entry.secure = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
    entry.saved = std::strcmp(entry.ssid, g->settings->wifiSsid) == 0;
  }
  WiFi.scanDelete();

  char text[48];
  std::snprintf(text, sizeof(text), "扫描到 %u 个网络", static_cast<unsigned>(list.count));
  setStatus(text);
  repaint(true);
}

// Connects, and on success stores the credentials. Returns true when connected.
bool connectWifi(const char* ssid, const char* password) {
  setStatus("正在连接…");
  repaint(true);

  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true);
  delay(100);
  WiFi.begin(ssid, password);

  const uint32_t deadline = millis() + 20000;
  while (WiFi.status() != WL_CONNECTED && static_cast<int32_t>(millis() - deadline) < 0) {
    delay(120);
  }

  if (WiFi.status() != WL_CONNECTED) {
    const wl_status_t status = WiFi.status();
    if (status == WL_NO_SSID_AVAIL) {
      setStatus("找不到这个网络");
    } else if (status == WL_CONNECT_FAILED
#ifdef WL_WRONG_PASSWORD
               || status == WL_WRONG_PASSWORD
#endif
    ) {
      setStatus("密码错误，请再试一次");
    } else {
      setStatus("连接超时，请重试");
    }
    repaint(true);
    return false;
  }

  std::snprintf(g->settings->wifiSsid, sizeof(g->settings->wifiSsid), "%s", ssid);
  std::snprintf(g->settings->wifiPassword, sizeof(g->settings->wifiPassword), "%s", password);
  saveSettings();
  char text[72];
  std::snprintf(text, sizeof(text), "已连接 %s", ssid);
  setStatus(text);
  repaint(true);
  return true;
}

// --- city -------------------------------------------------------------------

void forgetAndApplyLocation() {
  if (g->hooks.forgetLocationCache != nullptr) g->hooks.forgetLocationCache();
  if (g->hooks.applyLocation != nullptr) g->hooks.applyLocation();
}

void selectCity(const char* label, const char* searchName, float latitude, float longitude) {
  Settings& settings = *g->settings;
  std::snprintf(settings.place, sizeof(settings.place), "%s", label);
  std::snprintf(settings.city, sizeof(settings.city), "%s", searchName);
  settings.latitude = latitude;
  settings.longitude = longitude;
  saveSettings();
  forgetAndApplyLocation();
  char text[64];
  std::snprintf(text, sizeof(text), "已选 %s", label);
  setStatus(text);
  ui.screen = SettingsScreen::Home;
  repaint();
}

void runCitySearch() {
  if (cityQuery[0] == '\0') {
    setStatus("先输入城市名（拼音或英文）");
    repaint();
    return;
  }
  setStatus("正在搜索…");
  repaint();

  if (!WiFi.isConnected() && !g->client->beginWifi()) {
    setStatus("搜索需要网络：先连上 WiFi");
    repaint();
    return;
  }
  GeoCandidates hits;
  if (!g->client->fetchGeoCandidates(cityQuery, hits)) {
    setStatus("没有找到这个城市，换个拼写试试");
    repaint();
    return;
  }
  cityHits = hits;
  clearStatus();
  ui.screen = SettingsScreen::CityHits;
  repaint();
}

// --- keyboard ---------------------------------------------------------------

enum class KeyKind : uint8_t { None, Letter, Shift, Delete, Mode, Space, Primary };

struct KeyHit {
  KeyKind kind = KeyKind::None;
  char letter = 0;
};

KeyHit hitKeyboard(const Point& p) {
  const int16_t rowHeightTotal = static_cast<int16_t>(L::keyHeight + 10);
  for (uint8_t row = 0; row < 3; ++row) {
    const int16_t y = static_cast<int16_t>(L::keyboardTop + row * rowHeightTotal);
    if (p.y < y || p.y >= y + L::keyHeight) continue;
    const int16_t rowX = static_cast<int16_t>(L::keyX0 + (row == 1 ? 25 : (row == 2 ? 50 : 0)));
    const uint8_t letters = row == 0 ? 10 : (row == 1 ? 9 : 7);
    for (uint8_t i = 0; i < letters; ++i) {
      const int16_t x = static_cast<int16_t>(rowX + i * (L::keyWide + L::keyGap));
      if (inside(p, x, y, L::keyWide, L::keyHeight)) {
        const char* rows[3] = {"qwertyuiop", "asdfghjkl", "zxcvbnm"};
        return KeyHit{KeyKind::Letter, rows[row][i]};
      }
    }
    if (row == 2) {
      if (inside(p, L::keyX0, y, 64, L::keyHeight)) return KeyHit{KeyKind::Shift, 0};
      const int16_t delX =
          static_cast<int16_t>(L::keyX0 + 64 + L::keyGap + 7 * (L::keyWide + L::keyGap) + L::keyGap);
      if (inside(p, delX, y, 64, L::keyHeight)) return KeyHit{KeyKind::Delete, 0};
    }
  }

  const int16_t bottomY = static_cast<int16_t>(L::keyboardTop + 3 * rowHeightTotal);
  if (p.y >= bottomY && p.y < bottomY + L::keyHeight) {
    if (inside(p, L::keyX0, bottomY, 80, L::keyHeight)) return KeyHit{KeyKind::Mode, 0};
    if (inside(p, static_cast<int16_t>(L::keyX0 + 80 + L::keyGap), bottomY, 250, L::keyHeight)) {
      return KeyHit{KeyKind::Space, 0};
    }
    const int16_t primaryX = static_cast<int16_t>(L::keyX0 + 80 + L::keyGap + 250 + L::keyGap);
    if (inside(p, primaryX, bottomY, 150, L::keyHeight)) return KeyHit{KeyKind::Primary, 0};
  }
  return KeyHit{};
}

void appendChar(char* buffer, size_t capacity, char c) {
  const size_t length = std::strlen(buffer);
  if (length + 1 >= capacity) return;
  buffer[length] = c;
  buffer[length + 1] = '\0';
}

void backspace(char* buffer) {
  const size_t length = std::strlen(buffer);
  if (length == 0) return;
  buffer[length - 1] = '\0';
}

// --- screen handlers --------------------------------------------------------

void handleHome(const Point& p) {
  const int row = rowAt(p, L::rowsTop, L::rowHeight, 6);
  const Rect power{L::margin, static_cast<int16_t>(L::rowsTop + 5 * L::rowHeight + 12),
                   static_cast<int16_t>(L::width - L::margin * 2), 92};

  if (inside(p, power.x, power.y, power.width, power.height)) {
    if (confirmOff) {
      Serial.println("[weather] settings: powering off");
      Serial.flush();
      if (g->hooks.powerOff != nullptr) g->hooks.powerOff();
      return;
    }
    confirmOff = true;
    setStatus("再点一次「关机」确认断电");
    repaint();
    return;
  }
  if (confirmOff) {
    confirmOff = false;
    clearStatus();
  }

  Serial.printf("[settings] home row %d\n", row);
  switch (row) {
    case 0:
      goTo(SettingsScreen::Wifi);
      scanWifi();
      break;
    case 1:
      goTo(SettingsScreen::City);
      break;
    case 2:
      goTo(SettingsScreen::Interval);
      break;
    case 3:
      goTo(SettingsScreen::DefaultPage);
      break;
    case 4:
      goTo(SettingsScreen::Coins);
      break;
    default:
      repaint();
      break;
  }
}

void handleWifi(const Point& p) {
  if (headerActionTapped(p)) {
    scanWifi();
    return;
  }
  const WifiList& list = *g->networks;
  const int row = rowAt(p, L::rowsTop, L::rowHeight, static_cast<uint8_t>(list.count + 1));
  if (row < 0) {
    repaint();
    return;
  }
  if (row == list.count) {  // manual entry
    ssidBuffer[0] = '\0';
    passwordBuffer[0] = '\0';
    ui.editingSsid = true;
    goTo(SettingsScreen::Password);
    return;
  }

  const WifiEntry& entry = list.items[row];
  Serial.printf("[settings] wifi row %d: %s%s\n", row, entry.ssid, entry.secure ? " (locked)" : " (open)");
  std::snprintf(ssidBuffer, sizeof(ssidBuffer), "%s", entry.ssid);
  passwordBuffer[0] = '\0';
  if (!entry.secure) {
    connectWifi(entry.ssid, "");
    return;
  }
  ui.editingSsid = false;
  goTo(SettingsScreen::Password);
}

void handlePassword(const Point& p) {
  const Rect field{L::margin, 140, static_cast<int16_t>(L::width - L::margin * 2), 76};
  if (ui.secret && inside(p, static_cast<int16_t>(L::rightEdge - 110), field.y, 96, field.height)) {
    ui.revealed = !ui.revealed;
    repaint();
    return;
  }

  const KeyHit key = hitKeyboard(p);
  char* buffer = ui.editingSsid ? ssidBuffer : passwordBuffer;
  const size_t capacity = ui.editingSsid ? sizeof(ssidBuffer) : sizeof(passwordBuffer);
  switch (key.kind) {
    case KeyKind::Letter:
      appendChar(buffer, capacity, key.letter);
      break;
    case KeyKind::Space:
      appendChar(buffer, capacity, ' ');
      break;
    case KeyKind::Delete:
      backspace(buffer);
      break;
    case KeyKind::Shift:
    case KeyKind::Mode:
      break;  // the symbol/shift layers are a later nicety; the list covers most SSIDs
    case KeyKind::Primary:
      if (ui.editingSsid) {
        if (ssidBuffer[0] == '\0') {
          setStatus("网络名不能为空");
        } else {
          ui.editingSsid = false;
          clearStatus();
        }
      } else {
        connectWifi(ssidBuffer[0] != '\0' ? ssidBuffer : g->settings->wifiSsid, passwordBuffer);
      }
      break;
    default:
      return;
  }
  repaint();
}

void handleCity(const Point& p) {
  const int row = rowAt(p, L::rowsTop, L::cityRowHeight, kBuiltinCityCount + 1);
  if (row < 0) {
    repaint();
    return;
  }
  if (row == kBuiltinCityCount) {  // "search other cities"
    cityQuery[0] = '\0';
    goTo(SettingsScreen::CitySearch);
    return;
  }
  const BuiltinCity& city = builtinCity(static_cast<uint8_t>(row));
  Serial.printf("[settings] city row %d: %s\n", row, city.label);
  selectCity(city.label, city.searchName, city.latitude, city.longitude);
}

void handleCitySearch(const Point& p) {
  const Rect field{L::margin, 140, static_cast<int16_t>(L::width - L::margin * 2), 72};
  if (inside(p, static_cast<int16_t>(L::rightEdge - 60), field.y, 50, field.height)) {
    cityQuery[0] = '\0';
    repaint();
    return;
  }

  const KeyHit key = hitKeyboard(p);
  switch (key.kind) {
    case KeyKind::Letter:
      appendChar(cityQuery, sizeof(cityQuery), key.letter);
      break;
    case KeyKind::Space:
      appendChar(cityQuery, sizeof(cityQuery), ' ');
      break;
    case KeyKind::Delete:
      backspace(cityQuery);
      break;
    case KeyKind::Primary:
      runCitySearch();
      return;
    default:
      return;
  }
  repaint();
}

void handleCityHits(const Point& p) {
  const int16_t top = static_cast<int16_t>(L::rowsTop + 20);
  const int row = rowAt(p, top, 116, cityHits.count);
  if (row < 0) {
    repaint();
    return;
  }
  const GeoCandidate& candidate = cityHits.items[row];
  const char* label = candidate.name[0] != '\0' ? candidate.name : candidate.admin1;
  selectCity(label, candidate.name, candidate.latitude, candidate.longitude);
}

void handleCoins(const Point& p) {
  const int row = rowAt(p, L::rowsTop, 92, kCoinCount);
  if (row < 0) {
    repaint();
    return;
  }
  Settings& settings = *g->settings;
  const uint8_t bit = static_cast<uint8_t>(1u << row);
  const bool on = (settings.coinMask & bit) != 0;
  if (on && settingsVisibleCoins(settings.coinMask) <= 4) {
    setStatus("至少保留 4 个币");
    repaint();
    return;
  }
  settings.coinMask = on ? static_cast<uint8_t>(settings.coinMask & ~bit)
                         : static_cast<uint8_t>(settings.coinMask | bit);
  saveSettings();
  clearStatus();
  repaint();
}

void handleOptionList(const Point& p, uint8_t count, const uint16_t* values) {
  const int row = rowAt(p, L::rowsTop, L::rowHeight, count);
  if (row < 0 || values == nullptr) {
    repaint();
    return;
  }
  g->settings->refreshMinutes = values[row];
  saveSettings();
  ui.screen = SettingsScreen::Home;
  setStatus("刷新间隔已更新");
  repaint();
}

void handleDefaultPage(const Point& p) {
  const int row = rowAt(p, L::rowsTop, L::rowHeight, 4);
  if (row < 0) {
    repaint();
    return;
  }
  g->settings->defaultPage = static_cast<uint8_t>(row);
  saveSettings();
  ui.screen = SettingsScreen::Home;
  setStatus("默认页已更新");
  repaint();
}

// Back chevron: one step up the hierarchy.
void goBack() {
  switch (ui.screen) {
    case SettingsScreen::Wifi:
    case SettingsScreen::City:
    case SettingsScreen::Coins:
    case SettingsScreen::Interval:
    case SettingsScreen::DefaultPage:
      clearStatus();
      ui.screen = SettingsScreen::Home;
      break;
    case SettingsScreen::Password:
      if (!ui.editingSsid) {
        clearStatus();
        ui.screen = SettingsScreen::Wifi;
      } else {
        ui.editingSsid = false;
        clearStatus();
        ui.screen = SettingsScreen::Wifi;
      }
      break;
    case SettingsScreen::CitySearch:
    case SettingsScreen::CityHits:
      clearStatus();
      ui.screen = SettingsScreen::City;
      break;
    default:
      return;  // handled by the caller: back from Home leaves settings
  }
  repaint();
}

}  // namespace

void runSettingsSession(SettingsFlowContext& context) {
  g = &context;
  ui = SettingsUiState{};
  ui.screen = SettingsScreen::Home;
  ui.revealed = false;
  ui.editingSsid = false;
  confirmOff = false;
  clearStatus();
  lastActivity = millis();
  sleeping = !usbPowerPresent();

  Serial.printf("[weather] settings session (%u ms idle timeout, %s)\n",
                static_cast<unsigned>(kIdleExitMs),
                sleeping ? "light sleep between touches" : "awake (USB present)");
  repaint(/*full=*/true);  // clean slate over whatever the data page left

  bool touching = false;
  uint16_t downX = 0;
  uint16_t downY = 0;
  uint16_t lastX = 0;
  uint16_t lastY = 0;
  uint32_t downAt = 0;

  while (millis() - lastActivity < kIdleExitMs) {
    context.input->update();
    const bool pressed = context.input->isTouchPressed();

    if (pressed) {
      const InputManager::TouchPoint point = context.input->getTouchPoint();
      if (point.valid) {
        lastX = point.x;
        lastY = point.y;
      }
      if (!touching) {
        downX = lastX;
        downY = lastY;
        downAt = millis();
        touching = true;
      }
    } else if (touching) {
      touching = false;
      const uint16_t upX = lastX;
      const uint16_t upY = lastY;
      const uint32_t held = millis() - downAt;
      lastActivity = millis();

      const freeink::ui::Point logical = freeink::ui::touchToLogical(
          context.target->deviceContext(),
          static_cast<float>(upX) / BoardConfig::ACTIVE.displayWidth,
          static_cast<float>(upY) / BoardConfig::ACTIVE.displayHeight);
      const int32_t dx = static_cast<int32_t>(upX) - downX;
      const int32_t dy = static_cast<int32_t>(upY) - downY;
      const int32_t absDx = dx < 0 ? -dx : dx;
      const int32_t absDy = dy < 0 ? -dy : dy;

      Serial.printf("[settings] %s touch logical=(%d,%d) %u ms d=(%d,%d)\n",
                    settingsScreenTitle(ui.screen), static_cast<int>(logical.x),
                    static_cast<int>(logical.y), static_cast<unsigned>(held),
                    static_cast<int>(dx), static_cast<int>(dy));

      if (held >= kLongPressMs && absDx < 40 && absDy < 40) {
        Serial.println("[settings] long press: powering off");
        Serial.flush();
        if (context.hooks.powerOff != nullptr) context.hooks.powerOff();
        return;
      }

      if (headerBackTapped(logical)) {
        if (ui.screen == SettingsScreen::Home) {
          Serial.println("[settings] leaving settings");
          return;
        }
        goBack();
        continue;
      }

      switch (ui.screen) {
        case SettingsScreen::Home:
          handleHome(logical);
          break;
        case SettingsScreen::Wifi:
          handleWifi(logical);
          break;
        case SettingsScreen::Password:
          handlePassword(logical);
          break;
        case SettingsScreen::City:
          handleCity(logical);
          break;
        case SettingsScreen::CitySearch:
          handleCitySearch(logical);
          break;
        case SettingsScreen::CityHits:
          handleCityHits(logical);
          break;
        case SettingsScreen::Coins:
          handleCoins(logical);
          break;
        case SettingsScreen::Interval: {
          static const uint16_t kValues[] = {1, 5, 10, 15, 30, 60};
          handleOptionList(logical, 6, kValues);
          break;
        }
        case SettingsScreen::DefaultPage:
          handleDefaultPage(logical);
          break;
      }
      continue;
    }

    if (!pressed && !touching) {
      idleSlice(sleeping);
    }
  }

  Serial.println("[settings] idle timeout: leaving settings");
  clearStatus();
  g = nullptr;
}

}  // namespace weather

#endif  // FREEINK_DEVICE_PAPERS3
