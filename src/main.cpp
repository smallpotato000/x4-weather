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
#include <BoardConfig.h>
#include <EInkDisplay.h>
#include <InputManager.h>
#include <PowerManager.h>
#include <driver/gpio.h>
#include <esp_sleep.h>

#include "CjkGlyphSource.h"
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
bool haveReport = false;
bool firstAttemptDone = false;
bool coldBoot = true;
bool sessionActive = false;  // buttons live -> show the paging hint
weather::Page page = weather::Page::Now;

void setStatus(const char* text) { snprintf(statusLine, sizeof(statusLine), "%s", text); }

void paint(EInkDisplay::RefreshMode mode) {
  if (target == nullptr) return;
  weather::ScreenState state;
  state.report = haveReport ? &report : nullptr;
  state.place = displayPlace[0] != '\0' ? displayPlace : WEATHER_PLACE;
  state.status = statusLine;
  state.page = page;
  state.busy = !firstAttemptDone;
  state.interactive = sessionActive;
  state.cryptoSort = static_cast<weather::CryptoSort>(cryptoSortIndex);
  char hintLine[40];
  if (page == weather::Page::Crypto) {
    // On the crypto page up/down re-order the rows instead of paging.
    snprintf(hintLine, sizeof(hintLine), "上下排序 %s",
             weather::cryptoSortName(static_cast<weather::CryptoSort>(cryptoSortIndex)));
    state.hint = hintLine;
  }
  weather::renderWeatherScreen(*target, state);
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
  Serial.println("[weather] power key held: switching off");
  client.endWifi();
  display.deepSleep();
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
}

// --- location ---------------------------------------------------------------

// Point the client at the resolved coordinates and pick the header label.
void applyLocation() {
  if (location.valid) {
    client.setCoordinates(location.latitude, location.longitude);
  } else {
    client.setCoordinates(WEATHER_LATITUDE, WEATHER_LONGITUDE);
  }

  if (WEATHER_PLACE[0] != '\0') {
    snprintf(displayPlace, sizeof(displayPlace), "%s", WEATHER_PLACE);
  } else if (location.valid && location.name[0] != '\0') {
    snprintf(displayPlace, sizeof(displayPlace), "%s", location.name);
  } else {
    snprintf(displayPlace, sizeof(displayPlace), "天气");
  }
}

// Resolve WEATHER_CITY to coordinates once and keep them in RTC memory; falls
// back to the configured latitude/longitude when geocoding fails or no city is
// configured. Needs WiFi to be up.
void ensureLocation() {
  if (location.valid || WEATHER_CITY[0] == '\0') {
    applyLocation();
    return;
  }

  weather::GeoLocation found;
  if (!client.fetchGeocode(WEATHER_CITY, found)) {
    Serial.printf("[weather] geocoding failed: %s — using the configured coordinates\n",
                  client.lastError());
    applyLocation();
    return;
  }
  location = found;
  locationMagic = kLocationMagic;
  Serial.printf("[weather] geocoded \"%s\" -> %.4f, %.4f (%s, %s)\n", WEATHER_CITY,
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

[[noreturn]] void sleepFor(uint32_t minutes) {
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

  const uint64_t sleepUs = static_cast<uint64_t>(minutes) * 60ULL * 1000000ULL;
  esp_sleep_enable_timer_wakeup(sleepUs);
  if (wakeMask != 0) {
    freeink::PowerManager::armWakeOnPins(wakeMask, wakeLow);
  }

  Serial.printf("[weather] deep sleep %u min (wake mask 0x%llx, page %u, wake #%u)\n",
                static_cast<unsigned>(minutes), static_cast<unsigned long long>(wakeMask),
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

  uint32_t sleepMinutes = WEATHER_REFRESH_MINUTES;
  bool shouldPaint = true;

  if (!client.beginWifi()) {
    Serial.printf("[weather] wifi failed: %s\n", client.lastError());
    sleepMinutes = WEATHER_RETRY_MINUTES;
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
      if (page == weather::Page::AirQuality) ensureAirQuality();
      if (page == weather::Page::Crypto) ensureCrypto();
    } else {
      Serial.printf("[weather] fetch failed: %s\n", client.lastError());
      sleepMinutes = WEATHER_RETRY_MINUTES;
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

void setup() {
  ++wakeCount;

  Serial.begin(115200);
  delay(300);  // let USB CDC enumerate so the first log lines are visible

  // Install the local timezone before anything formats a clock: the system time
  // survives deep sleep, and a wake whose fetch fails still paints a footer time
  // before configTzTime() ever runs.
  setenv("TZ", WEATHER_TIMEZONE, 1);
  tzset();

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
  Serial.printf("\n[weather] Xteink X4 weather station, wake #%u (%s), clock %s\n",
                static_cast<unsigned>(wakeCount), causeText, bootClock);

  // Assert the battery latch (GPIO13 on the X4) before anything else, and make
  // sure a previous sleep did not leave the shared SD rail holding the display
  // bus.
  BoardConfig::holdPowerRails();
  BoardConfig::releaseSdRail();

  // Holding the power key while it is the *wake* source means "switch off".
  // (Not on a cold boot: holding the key is how the board is turned on.)
  if (cause == ESP_SLEEP_WAKEUP_GPIO && powerKeyHeldForPowerOff()) powerOff();

  display.begin();
  target = new freeink::ui::DisplayTarget(display.getFrameBuffer(), display.getDisplayWidth(),
                                          display.getDisplayHeight(), display.getDisplayWidthBytes());
  if (target == nullptr) {
    Serial.println("[weather] out of memory for the display target");
    sleepFor(WEATHER_RETRY_MINUTES);
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
      WEATHER_DEFAULT_PAGE < weather::kPageCount ? static_cast<uint8_t>(WEATHER_DEFAULT_PAGE) : 0;
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

#if WEATHER_INTERACTIVE_SECONDS > 0
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

  sleepFor(sleepMinutes);
}

void loop() {
  // Unreachable in normal operation: every setup() ends in deep sleep. Kept so
  // the Arduino runtime links; if it ever runs, sleep instead of spinning at
  // full clock.
  sleepFor(WEATHER_RETRY_MINUTES);
}
