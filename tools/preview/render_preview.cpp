// Host preview renderer: draws the real weather screen through the real
// FreeInkUI DisplayTarget into a simulated 800x480 panel framebuffer, so the
// portrait layout can be checked (and screenshotted) without hardware.
//
// The "ok" variant renders a *recorded* Open-Meteo response through the same
// parser the firmware uses (weather::parseForecast), so the preview shows the
// real numbers the panel would show. With no response file it falls back to a
// synthetic report.
//
// Build/run through tools/preview/render.sh.

#include <FreeInkUIDisplayTarget.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <sstream>
#include <string>

#include "CjkGlyphSource.h"
#include "ScaledDrawTarget.h"
#include "SettingsUi.h"
#include "WeatherParse.h"
#include "WeatherUi.h"

namespace {

// The X4 panel, and the PaperS3 panel the same layout is scaled onto. The
// framebuffer is sized for the bigger one; the active geometry is set in main().
constexpr int16_t kX4Width = 800;
constexpr int16_t kX4Height = 480;
constexpr int16_t kPaperWidth = 960;
constexpr int16_t kPaperHeight = 540;

int16_t gPanelWidth = kX4Width;
int16_t gPanelHeight = kX4Height;
int16_t gPanelWidthBytes = kX4Width / 8;

uint8_t gFramebuffer[(kPaperWidth / 8) * kPaperHeight];

// Synthetic fallback when no response file is given (also exercises the
// "unknown/rain" icon paths).
void fillSyntheticReport(weather::WeatherReport& report) {
  std::snprintf(report.place, sizeof(report.place), "%s", "武汉");
  report.ok = true;

  weather::CurrentWeather& now = report.current;
  now.valid = true;
  std::snprintf(now.time, sizeof(now.time), "%s", "2026-10-12T16:30");
  now.temperatureC = 19.9f;
  now.apparentC = 19.7f;
  now.windKph = 7.9f;
  now.humidity = 64;
  now.code = 51;  // 毛毛雨
  now.isDay = true;

  const char* dates[weather::kForecastDays] = {"2026-10-12", "2026-10-13", "2026-10-14"};
  const uint8_t codes[weather::kForecastDays] = {51, 0, 2};
  const float mins[weather::kForecastDays] = {15.9f, 14.4f, 10.6f};
  const float maxs[weather::kForecastDays] = {22.6f, 20.9f, 21.7f};
  for (uint8_t i = 0; i < weather::kForecastDays; ++i) {
    weather::DailyForecast& day = report.days[i];
    day.valid = true;
    std::snprintf(day.date, sizeof(day.date), "%s", dates[i]);
    day.code = codes[i];
    day.tMin = mins[i];
    day.tMax = maxs[i];
  }
  report.dayCount = weather::kForecastDays;
}

// "2026-10-12T17:30" -> epoch seconds, interpreted in the process TZ (the
// caller sets TZ=UTC so formatClock() prints back the same wall clock).
uint32_t epochFromIso(const char* iso) {
  int year = 0;
  int month = 0;
  int day = 0;
  int hour = 0;
  int minute = 0;
  if (std::sscanf(iso, "%d-%d-%dT%d:%d", &year, &month, &day, &hour, &minute) != 5) return 0;
  std::tm tm{};
  tm.tm_year = year - 1900;
  tm.tm_mon = month - 1;
  tm.tm_mday = day;
  tm.tm_hour = hour;
  tm.tm_min = minute;
  tm.tm_isdst = 0;
  const std::time_t epoch = timegm(&tm);
  return epoch > 0 ? static_cast<uint32_t>(epoch) : 0;
}

std::string readFile(const char* path) {
  std::ifstream file(path, std::ios::binary);
  if (!file.good()) return std::string();
  std::ostringstream buffer;
  buffer << file.rdbuf();
  return buffer.str();
}

bool loadReport(const char* path, weather::WeatherReport& report) {
  const std::string json = readFile(path);
  if (json.empty()) return false;
  if (!weather::parseForecast(json.c_str(), json.size(), report)) return false;
  std::snprintf(report.place, sizeof(report.place), "%s", "武汉");
  report.fetchedEpoch = epochFromIso(report.current.time);
  return true;
}

bool loadAirQuality(const char* path, weather::AirQuality& air) {
  if (path == nullptr) return false;
  const std::string json = readFile(path);
  if (json.empty()) return false;
  return weather::parseAirQuality(json.c_str(), json.size(), air);
}

// Quotes come from one file; each coin's 7-day sparkline comes from
// tools/preview/candles_<SYMBOL>.json (the firmware fetches them one by one).
bool loadCrypto(const char* path, weather::CryptoBoard& board) {
  if (path == nullptr) return false;
  const std::string json = readFile(path);
  if (json.empty()) return false;
  if (!weather::parseCrypto(json.c_str(), json.size(), board)) return false;

  const std::string quotesPath(path);
  const std::string directory = quotesPath.substr(0, quotesPath.find_last_of('/') + 1);
  for (uint8_t i = 0; i < weather::kCoinCount; ++i) {
    const weather::CoinInfo& info = weather::coinInfo(i);
    if (info.pair == nullptr) continue;
    const std::string candlesPath = directory + "candles_" + info.symbol + ".json";
    const std::string candles = readFile(candlesPath.c_str());
    if (!candles.empty()) {
      weather::parseCoinCandles(info.symbol, candles.c_str(), candles.size(), board);
    }
  }
  return true;
}

int writePbm(const char* path);

// Synthetic state for the settings screenshots: a configured home network, a
// short scan list, and the real Paris candidates (so the same-name case shows).
weather::Settings demoSettings() {
  weather::Settings settings;
  std::snprintf(settings.wifiSsid, sizeof(settings.wifiSsid), "%s", "home-wifi");
  std::snprintf(settings.wifiPassword, sizeof(settings.wifiPassword), "%s", "hunter2xy");
  std::snprintf(settings.city, sizeof(settings.city), "%s", "Wuhan");
  std::snprintf(settings.place, sizeof(settings.place), "%s", "武汉");
  settings.latitude = 30.5833f;
  settings.longitude = 114.2667f;
  settings.refreshMinutes = 30;
  settings.retryMinutes = 5;
  settings.defaultPage = 0;
  settings.coinMask = 0x7F;
  return settings;
}

weather::WifiList demoNetworks() {
  weather::WifiList list;
  struct Row {
    const char* ssid;
    int8_t rssi;
    bool secure;
  };
  const Row rows[] = {
      {"home-wifi", -52, true},   {"M5-PaperS3", -58, true},  {"Xiaomi_AX3000", -71, true},
      {"ChinaNet-8fZq", -76, true}, {"CMCC-Guest", -80, false}, {"TP-LINK_2.4G", -88, true},
  };
  for (const Row& row : rows) {
    weather::WifiEntry& entry = list.items[list.count++];
    std::snprintf(entry.ssid, sizeof(entry.ssid), "%s", row.ssid);
    entry.rssi = row.rssi;
    entry.secure = row.secure;
  }
  return list;
}

weather::GeoCandidates demoCandidates() {
  weather::GeoCandidates candidates;
  struct Row {
    const char* name;
    const char* admin1;
    const char* country;
    float lat;
    float lon;
  };
  const Row rows[] = {
      {"巴黎", "法兰西岛", "法国", 48.85f, 2.35f},
      {"Paris", "得克萨斯州", "美国", 33.66f, -95.56f},
      {"Paris", "田纳西州", "美国", 36.30f, -88.33f},
      {"巴黎", "肯塔基州", "美国", 38.21f, -84.25f},
      {"Paris", "伊利诺伊州", "美国", 39.61f, -87.70f},
  };
  for (const Row& row : rows) {
    weather::GeoCandidate& candidate = candidates.items[candidates.count++];
    std::snprintf(candidate.name, sizeof(candidate.name), "%s", row.name);
    std::snprintf(candidate.admin1, sizeof(candidate.admin1), "%s", row.admin1);
    std::snprintf(candidate.country, sizeof(candidate.country), "%s", row.country);
    std::snprintf(candidate.timezone, sizeof(candidate.timezone), "%s", "Asia/Shanghai");
    candidate.latitude = row.lat;
    candidate.longitude = row.lon;
  }
  return candidates;
}

// Renders one settings screen onto the PaperS3's panel geometry and returns true
// when the variant was a settings one.
bool renderSettingsVariant(const char* pageName, const char* out) {
  struct Mapping {
    const char* name;
    weather::SettingsScreen screen;
  };
  const Mapping kMappings[] = {
      {"settings", weather::SettingsScreen::Home},
      {"settings-wifi", weather::SettingsScreen::Wifi},
      {"settings-password", weather::SettingsScreen::Password},
      {"settings-city", weather::SettingsScreen::City},
      {"settings-city-search", weather::SettingsScreen::CitySearch},
      {"settings-city-hits", weather::SettingsScreen::CityHits},
      {"settings-coins", weather::SettingsScreen::Coins},
      {"settings-interval", weather::SettingsScreen::Interval},
      {"settings-page", weather::SettingsScreen::DefaultPage},
  };
  weather::SettingsScreen screen = weather::SettingsScreen::Home;
  bool matched = false;
  for (const Mapping& mapping : kMappings) {
    if (std::strcmp(pageName, mapping.name) == 0) {
      screen = mapping.screen;
      matched = true;
      break;
    }
  }
  if (!matched) return false;

  gPanelWidth = kPaperWidth;
  gPanelHeight = kPaperHeight;
  gPanelWidthBytes = gPanelWidth / 8;
  std::memset(gFramebuffer, 0xFF, sizeof(gFramebuffer));
  freeink::ui::DisplayTarget target(gFramebuffer, gPanelWidth, gPanelHeight, gPanelWidthBytes);
  weather::CjkGlyphSource cjk;
  weather::configureWeatherFonts(target, cjk);

  weather::Settings settings = demoSettings();
  weather::WifiList networks = demoNetworks();
  weather::GeoCandidates candidates = demoCandidates();

  weather::SettingsUiState state;
  state.screen = screen;
  state.settings = &settings;
  state.networks = &networks;
  state.candidates = &candidates;
  state.batteryPercent = 82;
  state.batteryKnown = true;
  if (screen == weather::SettingsScreen::Password) {
    state.editing = settings.wifiPassword;
    state.status = "密码错误，请再试一次";
  }
  if (screen == weather::SettingsScreen::CitySearch) {
    state.editing = "Paris";
  }
  if (screen == weather::SettingsScreen::CityHits) {
    state.editing = "Paris";
  }
  weather::renderSettingsScreen(target, state);
  return writePbm(out) == 0;
}

int writePbm(const char* path) {
  FILE* file = std::fopen(path, "wb");
  if (file == nullptr) return 1;
  std::fprintf(file, "P4\n%d %d\n", gPanelWidth, gPanelHeight);
  std::fwrite(gFramebuffer, 1, static_cast<size_t>(gPanelWidthBytes) * gPanelHeight, file);
  std::fclose(file);
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  const char* out = argc > 1 ? argv[1] : "preview.pbm";
  const char* variant = argc > 2 ? argv[2] : "ok";
  const char* responsePath = argc > 3 ? argv[3] : nullptr;
  const char* airPath = argc > 4 ? argv[4] : nullptr;
  const char* cryptoPath = argc > 5 ? argv[5] : nullptr;
  const int batteryPercent = argc > 6 ? std::atoi(argv[6]) : 82;

  // formatClock()/mktime() must agree on the wall clock the response carries.
  setenv("TZ", "UTC0", 1);
  tzset();

  // "paper-<variant>" renders the same pages onto the PaperS3's 960x540 panel
  // with the X4's 480x800 layout scaled by 540/480 and centred vertically.
  std::string page(variant);
  bool paper = false;
  if (page.rfind("paper-", 0) == 0) {
    paper = true;
    page = page.substr(6);
  }
  const char* pageName = page.c_str();

  // Settings screens are laid out natively in the PaperS3 frame, so they bypass
  // the weather report entirely.
  if (renderSettingsVariant(pageName, out)) {
    std::printf("wrote %s (panel %dx%d, settings screen %s)\n", out, gPanelWidth, gPanelHeight,
                pageName);
    return 0;
  }

  std::memset(gFramebuffer, 0xFF, sizeof(gFramebuffer));
  gPanelWidth = paper ? kPaperWidth : kX4Width;
  gPanelHeight = paper ? kPaperHeight : kX4Height;
  gPanelWidthBytes = gPanelWidth / 8;

  freeink::ui::DisplayTarget target(gFramebuffer, gPanelWidth, gPanelHeight, gPanelWidthBytes);
  weather::CjkGlyphSource cjk;
  weather::configureWeatherFonts(target, cjk);

  // The layout's own frame is always 480x800; on the PaperS3 that is scaled into
  // the 540x960 logical frame.
  // Fit the 480x800 layout into the logical frame: the smaller of the two axis
  // ratios, then centred (the PaperS3 frame is 540x960, so 1.125 and 30 px down).
  const float fitX = static_cast<float>(target.logicalWidth()) / 480.0f;
  const float fitY = static_cast<float>(target.logicalHeight()) / 800.0f;
  const float fit = fitX < fitY ? fitX : fitY;
  const int16_t offsetX = static_cast<int16_t>((target.logicalWidth() - 480.0f * fit) / 2.0f);
  const int16_t offsetY = static_cast<int16_t>((target.logicalHeight() - 800.0f * fit) / 2.0f);
  weather::ScaledDrawTarget scaled(target, fit, offsetX, offsetY);
  freeink::ui::DrawTarget& canvas = paper ? static_cast<freeink::ui::DrawTarget&>(scaled) : target;

  weather::WeatherReport report;
  weather::ScreenState state;
  state.place = "武汉";

  const bool hourlyPage = std::strcmp(pageName, "hourly") == 0;
  const bool airPage = std::strcmp(pageName, "aqi") == 0;
  const bool cryptoGainers = std::strcmp(pageName, "crypto-gainers") == 0;
  const bool cryptoLosers = std::strcmp(pageName, "crypto-losers") == 0;
  const bool cryptoPage = std::strcmp(pageName, "crypto") == 0 || cryptoGainers || cryptoLosers;
  state.page = hourlyPage ? weather::Page::Hourly
                          : (airPage ? weather::Page::AirQuality
                                     : (cryptoPage ? weather::Page::Crypto : weather::Page::Now));
  state.batteryKnown = batteryPercent >= 0;
  state.batteryPercent = static_cast<uint8_t>(batteryPercent < 0 ? 0 : batteryPercent);

  // The extra pages are only reachable during a button session, so they are
  // previewed with the paging hint visible.
  state.interactive = hourlyPage || airPage || cryptoPage;
  if (cryptoPage) {
    state.cryptoSort = cryptoGainers ? weather::CryptoSort::Gainers
                                     : (cryptoLosers ? weather::CryptoSort::Losers : weather::CryptoSort::Table);
    state.hint = cryptoGainers ? "上下排序 涨幅榜"
                               : (cryptoLosers ? "上下排序 跌幅榜" : "上下排序 表序");
  }

  if (std::strcmp(pageName, "busy") == 0) {
    state.busy = true;
    state.status = "正在连接 WiFi";
  } else if (std::strcmp(pageName, "error") == 0) {
    state.status = "获取失败，请检查网络与位置";
  } else {
    const bool loaded = responsePath != nullptr && loadReport(responsePath, report);
    if (!loaded) {
      if (responsePath != nullptr) {
        std::fprintf(stderr, "warning: could not parse %s, using the synthetic report\n", responsePath);
      }
      fillSyntheticReport(report);
      report.fetchedEpoch = epochFromIso(report.current.time);
    }
    if (airPage) {
      loadAirQuality(airPath, report.air);  // recorded numbers, or a "--" page
    }
    if (cryptoPage) {
      loadCrypto(cryptoPath, report.crypto);
    }
    state.report = &report;
    // Same footer the firmware composes after a successful fetch.
    static char statusBuffer[48];
    char clock[8];
    weather::formatClock(report.fetchedEpoch, clock, sizeof(clock));
    std::snprintf(statusBuffer, sizeof(statusBuffer), "更新于 %s", clock);
    state.status = statusBuffer;
  }

  weather::renderWeatherScreen(canvas, state);

  const int rc = writePbm(out);
  if (rc != 0) {
    std::fprintf(stderr, "error: cannot write %s\n", out);
    return rc;
  }
  std::printf("wrote %s (panel %dx%d, %s), variant=%s%s%s\n", out, gPanelWidth, gPanelHeight,
              paper ? "PaperS3 540x960 logical, X4 layout scaled 1.125" : "logical portrait 480x800",
              variant, responsePath != nullptr ? ", response=" : "",
              responsePath != nullptr ? responsePath : "");
  return 0;
}
