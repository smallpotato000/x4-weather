// Xteink X4 (ESP32-C3) deep-sleep weather station on the FreeInk SDK.
//
// One wake = one cycle: connect WiFi, pull the forecast from Open-Meteo, paint
// the selected 480x800 portrait page, refresh the panel, then cut the radio and
// panel power and drop into deep sleep. The RTC timer wakes the chip every
// WEATHER_REFRESH_MINUTES; the power key wakes it early.
//
// Buttons (only live while the chip is awake — the C3 X4's keys sit on an ADC
// ladder, so they cannot be deep-sleep wake sources):
//   power (short)  wake into the interactive session
//   left / up      previous page    (当前 / 逐小时 / 空气质量)
//   right / down   next page
//   confirm        fetch again now
//   back           stop the session and sleep immediately
//   power (hold)   switch off — the battery latch is pulled low
//
// There is no idle loop: every cycle ends in esp_deep_sleep_start(), so setup()
// is the whole program and loop() is only a safety net.

#include <Arduino.h>
#include <BatteryMonitor.h>
#include <BoardConfig.h>
#include <EInkDisplay.h>
#include <InputManager.h>
#include <PowerManager.h>
#include <driver/gpio.h>
#include <esp_sleep.h>

#if FREEINK_DEVICE_PAPERS3
#include <BoardPaperS3.h>  // powerOff(): the PMS150G latch takes a pulse train
#endif

#include "CjkGlyphSource.h"
#include "ScaledDrawTarget.h"
#include "SettingsFlow.h"
#include "SettingsStore.h"
#include "TouchIdle.h"
#if FREEINK_DEVICE_PAPERS3
#include <Rtc.h>  // BM8563: keeps the wake times on the half hour
#endif
#include "WeatherClient.h"
#include "WeatherUi.h"
#include "config.h"

// The JSON parser holds a 2 KB token buffer and the render path is deep; give
// the Arduino loop task the SDK's 16 KB instead of the 8 KB default.
SET_LOOP_TASK_STACK_SIZE(16 * 1024);

namespace {

constexpr uint32_t kInteractiveMs = static_cast<uint32_t>(WEATHER_INTERACTIVE_SECONDS) * 1000UL;
constexpr uint32_t kPowerOffHoldMs = static_cast<uint32_t>(WEATHER_POWER_OFF_HOLD_MS);
// How long a timer wake stays awake hoping for a key press before sleeping.
constexpr uint32_t kKeyWindowOnTimerWakeMs = 1200;

// Survives deep sleep (RTC memory), for the log line and diagnostics.
RTC_DATA_ATTR uint32_t wakeCount = 0;

// The last good report is kept in RTC memory across deep sleep: a wake that
// cannot reach the network then repaints the cached weather plus a failure
// footer instead of dropping back to the "no data" placeholder. Magic-guarded
// because RTC memory holds garbage on a cold boot.
// Magic bumped whenever WeatherReport's layout changes (hourly + air quality in
// 'WXC2', crypto in 'WXC3'), so a stale RTC cache from an older build is
// ignored. The geocoded location has its own magic.
constexpr uint32_t kCacheMagic = 0x57584333;  // 'WXC3'
constexpr uint32_t kLocationMagic = 0x574C4331;  // 'WLC1'
RTC_DATA_ATTR uint32_t cacheMagic = 0;
RTC_DATA_ATTR weather::WeatherReport cachedReport;
RTC_DATA_ATTR uint32_t locationMagic = 0;
RTC_DATA_ATTR weather::GeoLocation cachedLocation;

// The page the user last looked at, so the next automatic refresh paints the
// same one instead of snapping back to the current conditions. The crypto page's
// row order is remembered the same way.
RTC_DATA_ATTR uint8_t pageIndex = 0;
RTC_DATA_ATTR uint8_t cryptoSortIndex = 0;

// The EInkDisplay constructor still takes pins for source compatibility, but
// begin() reads the active board profile (Xteink X4: SCLK 8 / MOSI 10 / CS 21 /
// DC 4 / RST 5 / BUSY 6).
// Battery telemetry path comes from the active board profile (X4: an ADC pin
// behind a 2:1 divider; the SDK turns millivolts into a percentage off a 1S
// Li-ion curve, quantised to 10% because voltage cannot resolve it finer).
BatteryMonitor batteryMonitor;

EInkDisplay display(BoardConfig::DEFAULT_DEVICE.display.sclk,
                    BoardConfig::DEFAULT_DEVICE.display.mosi,
                    BoardConfig::DEFAULT_DEVICE.display.cs,
                    BoardConfig::DEFAULT_DEVICE.display.dc,
                    BoardConfig::DEFAULT_DEVICE.display.rst,
                    BoardConfig::DEFAULT_DEVICE.display.busy);
InputManager input;

weather::ClientConfig makeClientConfig() {
  weather::ClientConfig config;
  config.ssid = WIFI_SSID;
  config.password = WIFI_PASSWORD;
  config.place = WEATHER_PLACE;
  config.latitude = WEATHER_LATITUDE;
  config.longitude = WEATHER_LONGITUDE;
  config.forecastDays = WEATHER_FORECAST_DAYS;
  config.forecastHours = WEATHER_FORECAST_HOURS;
  config.timezone = WEATHER_TIMEZONE;
  config.ntp1 = WEATHER_NTP_1;
  config.ntp2 = WEATHER_NTP_2;
  config.ntp3 = WEATHER_NTP_3;
  config.wifiTimeoutMs = WEATHER_WIFI_TIMEOUT_MS;
  config.httpTimeoutMs = WEATHER_HTTP_TIMEOUT_MS;
  return config;
}

weather::WeatherClient client(makeClientConfig());
weather::WeatherReport report;
weather::CjkGlyphSource cjkGlyphSource;
freeink::ui::DisplayTarget* target = nullptr;

char statusLine[64] = {0};
char displayPlace[32] = {0};  // header label: config name, else the geocoded one
weather::GeoLocation location;
weather::Settings settings;  // compile-time defaults overlaid with NVS
#if FREEINK_DEVICE_PAPERS3
weather::WifiList wifiNetworks;  // last scan, drawn by the settings UI
#endif
bool haveReport = false;
bool batteryKnown = false;
uint8_t batteryPercent = 0;
bool firstAttemptDone = false;
bool coldBoot = true;
bool sessionActive = false;  // buttons live -> show the paging hint
weather::Page page = weather::Page::Now;

void setStatus(const char* text) { snprintf(statusLine, sizeof(statusLine), "%s", text); }

// The weather pages are laid out in a 480x800 logical frame. On the PaperS3 the
// portrait frame is 540x960, so the same layout is drawn through a scaling
// wrapper (fit + centre) instead of growing a second layout implementation.
freeink::ui::DrawTarget& canvas() {
#if FREEINK_DEVICE_PAPERS3
  static weather::ScaledDrawTarget* scaled = nullptr;
  if (scaled == nullptr && target != nullptr) {
    const float fitX = static_cast<float>(target->logicalWidth()) / 480.0f;
    const float fitY = static_cast<float>(target->logicalHeight()) / 800.0f;
    const float fit = fitX < fitY ? fitX : fitY;
    scaled = new weather::ScaledDrawTarget(
        *target, fit, static_cast<int16_t>((target->logicalWidth() - 480.0f * fit) / 2.0f),
        static_cast<int16_t>((target->logicalHeight() - 800.0f * fit) / 2.0f));
    Serial.printf("[weather] layout scaled x%.3f into %dx%d logical frame\n",
                  static_cast<double>(fit), static_cast<int>(target->logicalWidth()),
                  static_cast<int>(target->logicalHeight()));
  }
  if (scaled != nullptr) return *scaled;
#endif
  return *target;
}

void paint(EInkDisplay::RefreshMode mode) {
  if (target == nullptr) return;
  weather::ScreenState state;
  state.report = haveReport ? &report : nullptr;
  state.place = displayPlace[0] != '\0' ? displayPlace : settings.place;
  state.status = statusLine;
  state.page = page;
  state.busy = !firstAttemptDone;
  state.interactive = sessionActive;
  state.cryptoSort = static_cast<weather::CryptoSort>(cryptoSortIndex);
  state.batteryKnown = batteryKnown;
  state.batteryPercent = batteryPercent;
  state.coinMask = settings.coinMask;
  char hintLine[40];
  if (page == weather::Page::Crypto) {
    // On the crypto page up/down re-order the rows instead of paging.
    snprintf(hintLine, sizeof(hintLine), "上下排序 %s",
             weather::cryptoSortName(static_cast<weather::CryptoSort>(cryptoSortIndex)));
    state.hint = hintLine;
  }
  weather::renderWeatherScreen(canvas(), state);
  display.displayBuffer(mode);
}

// --- power key --------------------------------------------------------------

bool powerKeyDown() {
  const int8_t pin = BoardConfig::ACTIVE.input.power;
  if (pin < 0) return false;
  const bool activeHigh = BoardConfig::ACTIVE.input.powerActiveHigh;
  pinMode(pin, activeHigh ? INPUT_PULLDOWN : INPUT_PULLUP);
  return digitalRead(pin) == (activeHigh ? HIGH : LOW);
}

// True when the power key is held past the power-off threshold. Returns
// immediately when it is not pressed, so it is cheap to poll in the session.
bool powerKeyHeldForPowerOff() {
  if (!powerKeyDown()) return false;
  const uint32_t start = millis();
  while (powerKeyDown()) {
    if (millis() - start >= kPowerOffHoldMs) return true;
    delay(20);
  }
  return false;
}

// Switch off: pull the battery latch low (X4: GPIO13 gates the battery MOSFET,
// so on battery this really cuts power) and sleep with only the power key armed.
// On USB power the board stays alive, and this then behaves like "off until the
// power key is pressed again".
[[noreturn]] void powerOff() {
  Serial.println("[weather] switching off");
  client.endWifi();
  display.deepSleep();

#if FREEINK_DEVICE_PAPERS3
  // No power latch for firmware to hold and no readable button: the PMS150G
  // releases the rail on a pulse train, and nothing but the RTC alarm can turn
  // the board back on. Fall through to a sleep in case the rail did not cut.
  freeink::PowerManager::powerDownRailsForSleep();
  Serial.println("[weather] sending the power-off pulse train");
  Serial.flush();
  BoardPaperS3::powerOff();
  esp_sleep_enable_timer_wakeup(60ULL * 1000000ULL);
  Serial.println("[weather] off (pulse train sent)");
  Serial.flush();
  esp_deep_sleep_start();
#else
  freeink::PowerManager::powerDownRailsForSleep();

  // Let the key go before arming it, or the wake fires the instant we sleep.
  const uint32_t deadline = millis() + 5000;
  while (powerKeyDown() && static_cast<int32_t>(millis() - deadline) < 0) delay(20);

  for (const int8_t pin : {BoardConfig::ACTIVE.power.latch0, BoardConfig::ACTIVE.power.latch1}) {
    if (pin < 0) continue;
    const auto gpio = static_cast<gpio_num_t>(pin);
    gpio_hold_dis(gpio);
    pinMode(pin, OUTPUT);
    digitalWrite(pin, LOW);  // battery latch off
    gpio_hold_en(gpio);
  }

  freeink::PowerManager::armPowerButtonWakeup();
  Serial.printf("[weather] off (wake #%u)\n", static_cast<unsigned>(wakeCount));
  Serial.flush();
  freeink::PowerManager::deepSleep();  // no timer: only the power key comes back
#endif
}

// --- battery ----------------------------------------------------------------

void refreshBattery() {
  const BatteryMonitor::Status status = batteryMonitor.readStatus();
  if (status.supported && status.percentageKnown) {
    batteryPercent = static_cast<uint8_t>(status.percentage > 100 ? 100 : status.percentage);
    batteryKnown = true;
  }
  if (status.millivoltsKnown) {
    Serial.printf("[weather] battery %u mV, %u%%\n", static_cast<unsigned>(status.millivolts),
                  static_cast<unsigned>(batteryPercent));
  } else {
    Serial.println("[weather] battery not readable on this board");
  }
}

// --- location ---------------------------------------------------------------

// Point the client at the resolved coordinates and pick the header label.
void applyLocation() {
  if (location.valid) {
    client.setCoordinates(location.latitude, location.longitude);
  } else {
    client.setCoordinates(settings.latitude, settings.longitude);
  }

  if (settings.place[0] != '\0') {
    snprintf(displayPlace, sizeof(displayPlace), "%s", settings.place);
  } else if (location.valid && location.name[0] != '\0') {
    snprintf(displayPlace, sizeof(displayPlace), "%s", location.name);
  } else {
    snprintf(displayPlace, sizeof(displayPlace), "天气");
  }
}

// Drops the RTC-geocoded coordinates so the next cycle resolves them again (the
// settings UI calls this after the user picks a different city).
void forgetLocationCache() {
  location = weather::GeoLocation{};
  locationMagic = 0;
}

// Resolve WEATHER_CITY to coordinates once and keep them in RTC memory; falls
// back to the configured latitude/longitude when geocoding fails or no city is
// configured. Needs WiFi to be up.
void ensureLocation() {
  if (location.valid || settings.city[0] == '\0') {
    applyLocation();
    return;
  }

  weather::GeoLocation found;
  if (!client.fetchGeocode(settings.city, found)) {
    Serial.printf("[weather] geocoding failed: %s — using the configured coordinates\n",
                  client.lastError());
    applyLocation();
    return;
  }
  location = found;
  locationMagic = kLocationMagic;
  Serial.printf("[weather] geocoded \"%s\" -> %.4f, %.4f (%s, %s)\n", settings.city,
                static_cast<double>(found.latitude), static_cast<double>(found.longitude), found.timezone,
                found.country);
  applyLocation();
}

// --- data -------------------------------------------------------------------

// Fetches the forecast into `report` (assumes WiFi is up). Returns false and
// leaves the previous report alone on failure.
bool fetchWeather() {
  weather::WeatherReport fresh;
  if (!client.fetch(fresh)) return false;
  report = fresh;
  haveReport = true;
  cachedReport = report;
  cacheMagic = kCacheMagic;

  // The API reports local time (timezone=auto), so the footer is right wherever
  // the geocoded city is — no timezone bookkeeping needed for the display.
  char observation[8];
  weather::formatHourLabel(report.current.time, observation, sizeof(observation));
  if (observation[0] == '\0') {
    weather::formatClock(report.fetchedEpoch, observation, sizeof(observation));
  }
  snprintf(statusLine, sizeof(statusLine), "更新于 %s", observation);
  Serial.printf("[weather] %s %.1f C, code %u, %u day(s), %u hour(s)\n", report.place,
                static_cast<double>(report.current.temperatureC), static_cast<unsigned>(report.current.code),
                static_cast<unsigned>(report.dayCount), static_cast<unsigned>(report.hourCount));
  return true;
}

// Air quality lives on a second endpoint and is only fetched when the page that
// shows it is on screen — that keeps the normal cycle to a single request.
bool ensureAirQuality() {
  if (report.air.valid) return true;
  if (!client.wifiConnected() && !client.beginWifi()) return false;

  weather::AirQuality air;
  if (!client.fetchAirQuality(air)) {
    Serial.printf("[weather] air quality failed: %s\n", client.lastError());
    return false;
  }
  report.air = air;
  cachedReport = report;
  cacheMagic = kCacheMagic;
  Serial.printf("[weather] EAQI %d, PM2.5 %.1f\n", air.europeanAqi, static_cast<double>(air.pm25));
  return true;
}

// The crypto board is a third endpoint (CoinGecko) and, like air quality, is
// only requested while its page is on screen.
bool ensureCrypto() {
  if (report.crypto.ok) return true;
  if (!client.wifiConnected() && !client.beginWifi()) return false;

  weather::CryptoBoard board;
  if (!client.fetchCrypto(board)) {
    Serial.printf("[weather] crypto failed: %s\n", client.lastError());
    return false;
  }
  report.crypto = board;
  cachedReport = report;
  cacheMagic = kCacheMagic;
  Serial.printf("[weather] BTC %.0f / ETH %.2f / XMR %.2f\n", static_cast<double>(board.coins[0].usd),
                static_cast<double>(board.coins[1].usd), static_cast<double>(board.coins[6].usd));
  return true;
}

// --- deep sleep -------------------------------------------------------------

// Every GPIO the board exposes as a press-to-wake key, as a wake mask.
// Nav keys are only included on plain-digital-button boards: the C3 X3/X4 put
// their navigation keys on an ADC resistor ladder, where a pressed key sits at
// a divided voltage instead of a clean LOW, so a level wake on those pins is
// unreliable. Their power key is a real GPIO and does wake the chip.
uint64_t buttonWakeMask() {
  const auto& boardInput = BoardConfig::ACTIVE.input;
  uint64_t mask = 0;
  if (boardInput.power >= 0) mask |= 1ULL << boardInput.power;
  if (BoardConfig::ACTIVE.inputStyle == BoardConfig::InputStyle::DigitalButtons) {
    for (const int8_t pin : {boardInput.back, boardInput.confirm, boardInput.left, boardInput.right, boardInput.up,
                             boardInput.down}) {
      if (pin >= 0) mask |= 1ULL << pin;
    }
  }
  return mask;
}

bool anyWakePinLow(uint64_t mask) {
  for (int pin = 0; pin < 64; ++pin) {
    if (((mask >> pin) & 1) == 0) continue;
    if (digitalRead(pin) == LOW) return true;
  }
  return false;
}

// How long to wait for a held wake key to be released before sleeping anyway.
constexpr uint32_t kWakeKeyReleaseTimeoutMs = 15000;

void waitForWakePinsReleased(uint64_t mask, uint32_t timeoutMs) {
  if (mask == 0) return;
  const uint32_t deadline = millis() + timeoutMs;
  while (anyWakePinLow(mask) && static_cast<int32_t>(millis() - deadline) < 0) {
    delay(20);
  }
}

// Asserts the board's power-rail latch pins and holds them through deep sleep.
// On the X4 GPIO13 gates the battery MOSFET: without the hold the board would
// power off instead of waking on the timer (units that self-latch keep running,
// but holding is a no-op there).
void holdLatchPinsForSleep() {
  for (const int8_t pin : {BoardConfig::ACTIVE.power.latch0, BoardConfig::ACTIVE.power.latch1}) {
    if (pin < 0) continue;
    const auto gpio = static_cast<gpio_num_t>(pin);
    gpio_hold_dis(gpio);
    pinMode(pin, OUTPUT);
    digitalWrite(pin, HIGH);
    gpio_hold_en(gpio);
  }
}

#if FREEINK_DEVICE_PAPERS3
freeink::Rtc rtc;
bool rtcReady = false;
#endif

// Writes the RTC from the SNTP-synced clock, so the clock the sleep alignment
// reads stays right even though the ESP32's own timers are reset by deep sleep.
void syncRtc() {
#if FREEINK_DEVICE_PAPERS3
  if (!rtcReady) return;
  const time_t now = time(nullptr);
  if (now < 1700000000) return;  // SNTP has not landed yet
  struct tm local = {};
  localtime_r(&now, &local);
  freeink::Rtc::DateTime dt;
  dt.year = static_cast<uint16_t>(local.tm_year + 1900);
  dt.month = static_cast<uint8_t>(local.tm_mon + 1);
  dt.day = static_cast<uint8_t>(local.tm_mday);
  dt.hour = static_cast<uint8_t>(local.tm_hour);
  dt.minute = static_cast<uint8_t>(local.tm_min);
  dt.second = static_cast<uint8_t>(local.tm_sec);
  dt.weekday = static_cast<uint8_t>(local.tm_wday);
  if (rtc.set(dt)) {
    Serial.printf("[weather] rtc set to %04u-%02u-%02u %02u:%02u\n", dt.year, dt.month, dt.day,
                  dt.hour, dt.minute);
  }
#endif
}

// Sleep length that lands the next wake on a whole refresh boundary (:00 / :30),
// measured on the RTC so successive cycles do not drift. Plain period otherwise.
uint32_t alignedSleepSeconds(uint32_t fallbackSeconds) {
#if FREEINK_DEVICE_PAPERS3
  const uint32_t period = static_cast<uint32_t>(settings.refreshMinutes) * 60U;
  if (!rtcReady || period == 0) return fallbackSeconds;
  freeink::Rtc::DateTime dt;
  if (!rtc.now(dt)) return fallbackSeconds;
  struct tm tmValue = {};
  tmValue.tm_year = dt.year - 1900;
  tmValue.tm_mon = dt.month - 1;
  tmValue.tm_mday = dt.day;
  tmValue.tm_hour = dt.hour;
  tmValue.tm_min = dt.minute;
  tmValue.tm_sec = dt.second;
  const time_t now = mktime(&tmValue);  // local time; TZ is set in setup()
  if (now <= 0) return fallbackSeconds;
  const uint32_t local = static_cast<uint32_t>(now);
  const uint32_t next = (local / period + 1U) * period;
  uint32_t seconds = next - local;
  if (seconds < 20U) seconds = period;  // just missed the boundary
  if (seconds > period + 60U) seconds = period + 60U;
  Serial.printf("[weather] sleep aligned to the next %u min boundary: %u s\n",
                static_cast<unsigned>(settings.refreshMinutes), static_cast<unsigned>(seconds));
  return seconds;
#else
  return fallbackSeconds;
#endif
}

[[noreturn]] void sleepForSeconds(uint32_t seconds) {
  const uint64_t wakeMask = buttonWakeMask();
  const bool wakeLow = !BoardConfig::ACTIVE.input.powerActiveHigh;

  // Give the (possibly still held) wake pins a defined idle level, so the
  // release check below is meaningful; the C3 deep-sleep wake source applies
  // its own internal pull for the sleep itself.
  for (int pin = 0; pin < 64; ++pin) {
    if (((wakeMask >> pin) & 1) == 0) continue;
    pinMode(pin, wakeLow ? INPUT_PULLUP : INPUT_PULLDOWN);
  }
  waitForWakePinsReleased(wakeMask, kWakeKeyReleaseTimeoutMs);

  client.endWifi();

  // Panel sleep command first, then park the rails (the SDK documents that
  // order); PowerManager::deepSleep() enables the deep-sleep hold so the parked
  // levels survive, and isolates the remaining floating GPIOs.
  display.deepSleep();
  holdLatchPinsForSleep();
  freeink::PowerManager::powerDownRailsForSleep();

  const uint64_t sleepUs = static_cast<uint64_t>(seconds) * 1000000ULL;
  esp_sleep_enable_timer_wakeup(sleepUs);
  if (wakeMask != 0) {
    freeink::PowerManager::armWakeOnPins(wakeMask, wakeLow);
  }

  Serial.printf("[weather] deep sleep %u s (wake mask 0x%llx, page %u, wake #%u)\n",
                static_cast<unsigned>(seconds), static_cast<unsigned long long>(wakeMask),
                static_cast<unsigned>(pageIndex), static_cast<unsigned>(wakeCount));
  Serial.flush();

  freeink::PowerManager::deepSleep();  // does not return
}

// --- one cycle --------------------------------------------------------------

// Returns the number of minutes to sleep after this attempt. Painting policy:
// the panel is only repainted when there is something new (or cached) to show,
// so a failed cycle with no data leaves the frame already on the panel alone.
uint32_t runCycle(bool isColdBoot) {
  Serial.printf("[weather] refreshing (wake #%u, heap %u B free)\n", static_cast<unsigned>(wakeCount),
                static_cast<unsigned>(ESP.getFreeHeap()));

  refreshBattery();

  uint32_t sleepMinutes = settings.refreshMinutes;
  bool shouldPaint = true;

  if (!client.beginWifi()) {
    Serial.printf("[weather] wifi failed: %s\n", client.lastError());
    sleepMinutes = settings.retryMinutes;
    if (haveReport) {
      setStatus("无法连接 WiFi，显示上次数据");
    } else if (isColdBoot) {
      setStatus("无法连接 WiFi，请检查设置");
    } else {
      shouldPaint = false;  // keep the last good frame
    }
  } else {
    // Coordinates first (a city name is resolved once, then cached in RTC), then
    // the forecast, then whatever extra data the visible page needs.
    ensureLocation();
    if (fetchWeather()) {
      syncRtc();  // SNTP has set the ESP32 clock by now
      if (page == weather::Page::AirQuality) ensureAirQuality();
      if (page == weather::Page::Crypto) ensureCrypto();
    } else {
      Serial.printf("[weather] fetch failed: %s\n", client.lastError());
      sleepMinutes = settings.retryMinutes;
      char clock[8];
      weather::formatClock(client.epochNow(), clock, sizeof(clock));
      if (haveReport) {
        snprintf(statusLine, sizeof(statusLine), "获取失败 %s，显示上次数据", clock);
      } else if (isColdBoot) {
        setStatus("获取失败，请检查网络与位置");
      } else {
        shouldPaint = false;  // keep the last good frame
      }
    }
  }

  firstAttemptDone = true;
  if (shouldPaint) paint(EInkDisplay::FULL_REFRESH);
  return sleepMinutes;
}

// --- interactive session ----------------------------------------------------

#if WEATHER_INTERACTIVE_SECONDS > 0

// Waits up to `timeoutMs` for any button; returns the button index or -1.
int waitForAnyButton(uint32_t timeoutMs) {
  const uint32_t deadline = millis() + timeoutMs;
  while (static_cast<int32_t>(millis() - deadline) < 0) {
    input.update();
    for (uint8_t button = 0; button <= InputManager::BTN_POWER; ++button) {
      if (input.wasPressed(button)) return button;
    }
    delay(10);
  }
  return -1;
}

// Keeps the panel awake and lets the buttons drive it: left/right page through
// the three screens, confirm re-fetches, back sleeps immediately. Leaves the
// selected page as a clean standing image (FULL_REFRESH) before returning.
void interactiveSession(int initialButton) {
  refreshBattery();
  sessionActive = true;
  Serial.printf("[weather] interactive session (%u s)\n", static_cast<unsigned>(WEATHER_INTERACTIVE_SECONDS));
  paint(EInkDisplay::FULL_REFRESH);  // repaint with the paging hint

  uint32_t lastActivity = millis();
  int pending = initialButton;
  while (static_cast<int32_t>(millis() - lastActivity) < static_cast<int32_t>(kInteractiveMs)) {
    if (powerKeyHeldForPowerOff()) powerOff();  // long press = switch off

    int button = pending;
    pending = -1;
    if (button < 0) {
      input.update();
      for (uint8_t candidate = 0; candidate <= InputManager::BTN_POWER; ++candidate) {
        if (input.wasPressed(candidate)) {
          button = candidate;
          break;
        }
      }
    }

    if (button >= 0 && button != InputManager::BTN_POWER) {
      lastActivity = millis();
      if (button == InputManager::BTN_BACK) break;  // sleep now

      bool repaint = false;
      if (button == InputManager::BTN_LEFT) {
        page = weather::previousPage(page);
        repaint = true;
      } else if (button == InputManager::BTN_RIGHT) {
        page = weather::nextPage(page);
        repaint = true;
      } else if (button == InputManager::BTN_UP || button == InputManager::BTN_DOWN) {
        if (page == weather::Page::Crypto) {
          // Same keys that page elsewhere cycle the row order here.
          const uint8_t step = button == InputManager::BTN_UP ? 1 : weather::kCryptoSortCount - 1;
          cryptoSortIndex = static_cast<uint8_t>((cryptoSortIndex + step) % weather::kCryptoSortCount);
          Serial.printf("[weather] crypto sort: %s\n",
                        weather::cryptoSortName(static_cast<weather::CryptoSort>(cryptoSortIndex)));
          settings.cryptoSort = cryptoSortIndex;
          weather::SettingsStore::save(settings);
        } else {
          page = button == InputManager::BTN_UP ? weather::previousPage(page) : weather::nextPage(page);
        }
        repaint = true;
      } else if (button == InputManager::BTN_CONFIRM) {
        Serial.println("[weather] confirm: re-fetching");
        if (client.wifiConnected() || client.beginWifi()) {
          ensureLocation();
          fetchWeather();
          if (page == weather::Page::AirQuality) ensureAirQuality();
          if (page == weather::Page::Crypto) ensureCrypto();
        }
        repaint = true;
      }

      if (repaint) {
        pageIndex = static_cast<uint8_t>(page);
        if (page == weather::Page::AirQuality) ensureAirQuality();
        if (page == weather::Page::Crypto) ensureCrypto();
        paint(EInkDisplay::FAST_REFRESH);  // snappy page flips
      }
    }
    delay(20);
  }

  sessionActive = false;
  if (target != nullptr) paint(EInkDisplay::FULL_REFRESH);  // clean standing image
}

#endif  // WEATHER_INTERACTIVE_SECONDS > 0

}  // namespace


#if FREEINK_DEVICE_PAPERS3
// Touch session. The PaperS3 has no readable buttons at all, so every wake ends
// with a window like this one: taps and swipes drive exactly the actions the
// X4's keys drive, and the window closes after WEATHER_TOUCH_SESSION_MS of
// silence. (A light-sleep variant with touch-to-wake is the next step; this
// version stays awake, which costs about what an X4 button session costs.)
enum class TouchSessionResult : uint8_t { Sleep, Settings };

// Touch session. A tap on the header strip hands over to the settings UI.
TouchSessionResult interactiveTouchSession() {
  refreshBattery();
  pageIndex = static_cast<uint8_t>(page);
  client.endWifi();  // the window is touch-only; page data is already cached
  bool sleeping = !weather::usbPowerPresent();  // current idle mode, re-checked per slice
  Serial.printf("[weather] touch session (%u ms idle timeout, page %u, %s)\n",
                static_cast<unsigned>(WEATHER_TOUCH_SESSION_MS), static_cast<unsigned>(pageIndex),
                sleeping ? "light sleep between touches" : "awake: USB present");

  uint32_t lastActivity = millis();
  uint32_t downAt = 0;
  uint16_t downX = 0;
  uint16_t downY = 0;
  uint16_t lastX = 0;  // last point seen while the finger was down
  uint16_t lastY = 0;
  bool touching = false;

  uint32_t irqWakes = 0;
  uint32_t timerWakes = 0;

  while (millis() - lastActivity < WEATHER_TOUCH_SESSION_MS) {
    input.update();
    const bool pressed = input.isTouchPressed();

    // Nothing on the glass: sleep instead of spinning at full clock. WiFi is
    // already down, so the only thing that can end this is a finger or the
    // slice timer.
    if (!pressed && !touching) {
      // USB on the bench means the log matters more than the microamps; on
      // battery the session sleeps between touches. Checked every slice so
      // unplugging (or plugging) switches modes without a reboot.
      const bool usb = weather::usbPowerPresent();
      if (usb != !sleeping) {
        sleeping = !usb;
        Serial.printf("[weather] session idle mode: %s\n",
                      sleeping ? "light sleep between touches" : "awake (USB present)");
        Serial.flush();
      }
      if (!sleeping) {
        delay(10);
        continue;
      }
      const uint32_t idle = millis() - lastActivity;
      const uint32_t remaining = WEATHER_TOUCH_SESSION_MS - idle;
      const uint32_t slice =
          remaining < weather::kWakeSliceMs ? remaining : weather::kWakeSliceMs;
      const esp_sleep_wakeup_cause_t cause = weather::lightSleepSlice(slice);
      if (cause == ESP_SLEEP_WAKEUP_GPIO) {
        ++irqWakes;
      } else {
        ++timerWakes;
      }
      if (irqWakes + timerWakes <= 6) {  // a few lines prove which source fires
        Serial.printf("[weather] light-sleep wake #%u: %s\n",
                      static_cast<unsigned>(irqWakes + timerWakes),
                      cause == ESP_SLEEP_WAKEUP_GPIO ? "touch irq" : "slice timer");
        Serial.flush();
      }
      continue;  // re-read the controller immediately after waking
    }

    if (pressed) {
      const InputManager::TouchPoint point = input.getTouchPoint();
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
      // The release itself carries no valid point, so the gesture is measured
      // against the last position the finger actually reported.
      const uint16_t upX = lastX;
      const uint16_t upY = lastY;
      const int32_t dx = static_cast<int32_t>(upX) - downX;
      const int32_t dy = static_cast<int32_t>(upY) - downY;
      Serial.printf("[weather] touch (%u,%u) -> (%u,%u), %u ms, d=(%d,%d)\n",
                    static_cast<unsigned>(downX), static_cast<unsigned>(downY),
                    static_cast<unsigned>(upX), static_cast<unsigned>(upY),
                    static_cast<unsigned>(millis() - downAt), static_cast<int>(dx),
                    static_cast<int>(dy));
      const uint32_t held = millis() - downAt;
      lastActivity = millis();

      const freeink::ui::Point logical = freeink::ui::touchToLogical(
          target->deviceContext(), static_cast<float>(upX) / BoardConfig::ACTIVE.displayWidth,
          static_cast<float>(upY) / BoardConfig::ACTIVE.displayHeight);
      const int16_t logicalWidth = target->logicalWidth();
      const int16_t logicalHeight = target->logicalHeight();
      const int32_t absDx = dx < 0 ? -dx : dx;
      const int32_t absDy = dy < 0 ? -dy : dy;

      bool repaint = false;
      if (held >= 2500 && absDx < 40 && absDy < 40) {
        // Flush before the rail goes away, or the log of the power-off is lost.
        Serial.printf("[weather] long press %u ms at logical (%d,%d): powering off\n",
                      static_cast<unsigned>(held), static_cast<int>(logical.x),
                      static_cast<int>(logical.y));
        Serial.flush();
        powerOff();  // does not return
      } else if (absDy > 60 && absDy > absDx) {
        // Vertical swipe: on the crypto page it re-orders the rows.
        if (page == weather::Page::Crypto) {
          const uint8_t step = dy < 0 ? 1 : weather::kCryptoSortCount - 1;
          cryptoSortIndex = static_cast<uint8_t>((cryptoSortIndex + step) % weather::kCryptoSortCount);
          Serial.printf("[weather] crypto sort: %s\n",
                        weather::cryptoSortName(static_cast<weather::CryptoSort>(cryptoSortIndex)));
          settings.cryptoSort = cryptoSortIndex;
          weather::SettingsStore::save(settings);  // survives a reflash
          repaint = true;
        }
      } else if (absDx > 60) {
        page = dx < 0 ? weather::nextPage(page) : weather::previousPage(page);
        repaint = true;
      } else {
        // Tap: header strip opens settings, bottom strip closes the session, the
        // halves page.
        if (logical.y < 104) {
          Serial.println("[weather] header tap: opening settings");
          return TouchSessionResult::Settings;
        }
        if (logical.y > logicalHeight - 80) {
          Serial.printf("[weather] bottom tap at logical (%d,%d): closing the session\n",
                        static_cast<int>(logical.x), static_cast<int>(logical.y));
          lastActivity = 0;
          break;
        }
        const weather::Page before = page;
        page = logical.x < logicalWidth / 2 ? weather::previousPage(page) : weather::nextPage(page);
        Serial.printf("[weather] tap logical (%d,%d): page %u -> %u\n", static_cast<int>(logical.x),
                      static_cast<int>(logical.y), static_cast<unsigned>(before),
                      static_cast<unsigned>(page));
        repaint = true;
      }

      if (repaint) {
        pageIndex = static_cast<uint8_t>(page);
        if (page == weather::Page::AirQuality) ensureAirQuality();
        if (page == weather::Page::Crypto) ensureCrypto();
        paint(EInkDisplay::FAST_REFRESH);
      }
    }
    delay(10);
  }
  Serial.printf("[weather] touch session over (page %u after %u ms; %u irq wakes, %u timer wakes)\n",
                static_cast<unsigned>(pageIndex), static_cast<unsigned>(millis()),
                static_cast<unsigned>(irqWakes), static_cast<unsigned>(timerWakes));
  return TouchSessionResult::Sleep;
}
#endif

void setup() {
  ++wakeCount;

  Serial.begin(115200);
  delay(300);  // let USB CDC enumerate so the first log lines are visible

  // Settings first: NVS (what the user configured on the touch board) over the
  // compile-time defaults in config.h/secrets.h. Everything below reads the
  // resolved values instead of the macros.
  settings = weather::SettingsStore::load();
  client.setCredentials(settings.wifiSsid, settings.wifiPassword);
  client.setPlace(settings.place);

  // Install the local timezone before anything formats a clock: the system time
  // survives deep sleep, and a wake whose fetch fails still paints a footer time
  // before configTzTime() ever runs.
  setenv("TZ", WEATHER_TIMEZONE, 1);
  tzset();

#if FREEINK_DEVICE_PAPERS3
  rtcReady = rtc.begin();
  Serial.printf("[weather] rtc %s\n", rtcReady ? "ready (BM8563)" : "not found");
#endif

  const esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
  const char* causeText = "power-on/reset";
  if (cause == ESP_SLEEP_WAKEUP_TIMER) {
    causeText = "timer";
  } else if (cause == ESP_SLEEP_WAKEUP_GPIO) {
    causeText = "button";
  }
  coldBoot = (cause == ESP_SLEEP_WAKEUP_UNDEFINED);

  char bootClock[8];
  weather::formatClock(client.epochNow(), bootClock, sizeof(bootClock));
  Serial.printf("\n[weather] %s weather station, wake #%u (%s), clock %s\n", BoardConfig::ACTIVE.name,
                static_cast<unsigned>(wakeCount), causeText, bootClock);

  // Assert the battery latch (GPIO13 on the X4) before anything else, and make
  // sure a previous sleep did not leave the shared SD rail holding the display
  // bus.
  BoardConfig::holdPowerRails();
  BoardConfig::releaseSdRail();

#if !FREEINK_DEVICE_PAPERS3
  // Holding the power key while it is the *wake* source means "switch off".
  // (Not on a cold boot: holding the key is how the board is turned on.)
  if (cause == ESP_SLEEP_WAKEUP_GPIO && powerKeyHeldForPowerOff()) powerOff();
#endif

  display.begin();
  target = new freeink::ui::DisplayTarget(display.getFrameBuffer(), display.getDisplayWidth(),
                                          display.getDisplayHeight(), display.getDisplayWidthBytes());
  if (target == nullptr) {
    Serial.println("[weather] out of memory for the display target");
    sleepForSeconds(settings.retryMinutes * 60U);
  }
  // Landscape-native 800x480 panel -> DisplayTarget defaults to Portrait, so the
  // UI lays out in a 480x800 logical frame that reads upright on the held-tall X4.
  weather::configureWeatherFonts(*target, cjkGlyphSource);

  // Restore the previous cycle's report, the geocoded location and the selected
  // page (RTC memory).
  if (cacheMagic == kCacheMagic && cachedReport.ok) {
    report = cachedReport;
    haveReport = true;
  }
  if (locationMagic == kLocationMagic && cachedLocation.valid) {
    location = cachedLocation;
  }
  applyLocation();
  const uint8_t defaultPage =
      settings.defaultPage < weather::kPageCount ? static_cast<uint8_t>(settings.defaultPage) : 0;
  if (!coldBoot && pageIndex < weather::kPageCount) {
    page = static_cast<weather::Page>(pageIndex);  // resume the page the user left
  } else {
    page = static_cast<weather::Page>(defaultPage);
  }
  pageIndex = static_cast<uint8_t>(page);
  if (cryptoSortIndex >= weather::kCryptoSortCount) cryptoSortIndex = 0;

  input.begin();

  // Only a cold boot (blank panel) shows the placeholder while the first fetch
  // runs. On any wake the panel still holds the last frame: fetching first saves
  // a whole refresh, and a failed cycle can leave that frame untouched.
  if (coldBoot) {
    setStatus("正在连接 WiFi");
    paint(EInkDisplay::FULL_REFRESH);
  }

  const uint32_t sleepMinutes = runCycle(coldBoot);
  // A normal cycle sleeps to the next boundary; a retry keeps its short gap so a
  // flaky network is not waited out for half an hour.
  const uint32_t sleepSeconds =
      sleepMinutes == settings.refreshMinutes ? alignedSleepSeconds(sleepMinutes * 60U)
                                              : sleepMinutes * 60U;

#if FREEINK_DEVICE_PAPERS3
  // There are no readable buttons on the PaperS3, so every wake opens a touch
  // window: the panel is already showing fresh data, and taps land immediately.
  // A header tap hands over to the settings UI, which returns back here.
  for (;;) {
    if (interactiveTouchSession() != TouchSessionResult::Settings) break;

    weather::SettingsFlowContext flow;
    flow.input = &input;
    flow.display = &display;
    flow.target = target;
    flow.client = &client;
    flow.settings = &settings;
    flow.networks = &wifiNetworks;
    flow.batteryKnown = batteryKnown;
    flow.batteryPercent = batteryPercent;
    flow.hooks.applyLocation = &applyLocation;
    flow.hooks.forgetLocationCache = &forgetLocationCache;
    flow.hooks.powerOff = &powerOff;
    weather::runSettingsSession(flow);
    batteryKnown = flow.batteryKnown;
    batteryPercent = flow.batteryPercent;

    // Back to the data pages: apply whatever the user changed and repaint.
    client.setCredentials(settings.wifiSsid, settings.wifiPassword);
    client.setPlace(settings.place);
    pageIndex = static_cast<uint8_t>(page);
    refreshBattery();
    paint(EInkDisplay::FULL_REFRESH);
  }
#elif WEATHER_INTERACTIVE_SECONDS > 0
  // A power-key wake goes straight into the session; a timer wake waits a moment
  // in case someone is pressing a key right then.
  int initialButton = -1;
  if (cause == ESP_SLEEP_WAKEUP_GPIO) {
    initialButton = InputManager::BTN_POWER;
  } else {
    initialButton = waitForAnyButton(kKeyWindowOnTimerWakeMs);
  }
  if (initialButton >= 0) interactiveSession(initialButton);
#endif

  sleepForSeconds(sleepSeconds);
}

void loop() {
  // Unreachable in normal operation: every setup() ends in deep sleep. Kept so
  // the Arduino runtime links; if it ever runs, sleep instead of spinning at
  // full clock.
  sleepForSeconds(settings.retryMinutes * 60U);
}
