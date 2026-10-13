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
#include "WeatherParse.h"
#include "WeatherUi.h"

namespace {

constexpr int16_t kPanelWidth = 800;
constexpr int16_t kPanelHeight = 480;
constexpr int16_t kPanelWidthBytes = kPanelWidth / 8;

uint8_t gFramebuffer[kPanelWidthBytes * kPanelHeight];

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
  const char* out = argc > 1 ? argv[1] : "preview.pbm";
  const char* variant = argc > 2 ? argv[2] : "ok";
  const char* responsePath = argc > 3 ? argv[3] : nullptr;
  const char* airPath = argc > 4 ? argv[4] : nullptr;
  const char* cryptoPath = argc > 5 ? argv[5] : nullptr;

  // formatClock()/mktime() must agree on the wall clock the response carries.
  setenv("TZ", "UTC0", 1);
  tzset();

  std::memset(gFramebuffer, 0xFF, sizeof(gFramebuffer));
  freeink::ui::DisplayTarget target(gFramebuffer, kPanelWidth, kPanelHeight, kPanelWidthBytes);
  weather::CjkGlyphSource cjk;
  weather::configureWeatherFonts(target, cjk);

  weather::WeatherReport report;
  weather::ScreenState state;
  state.place = "武汉";

  const bool hourlyPage = std::strcmp(variant, "hourly") == 0;
  const bool airPage = std::strcmp(variant, "aqi") == 0;
  const bool cryptoGainers = std::strcmp(variant, "crypto-gainers") == 0;
  const bool cryptoLosers = std::strcmp(variant, "crypto-losers") == 0;
  const bool cryptoPage = std::strcmp(variant, "crypto") == 0 || cryptoGainers || cryptoLosers;
  state.page = hourlyPage ? weather::Page::Hourly
                          : (airPage ? weather::Page::AirQuality
                                     : (cryptoPage ? weather::Page::Crypto : weather::Page::Now));
  // The extra pages are only reachable during a button session, so they are
  // previewed with the paging hint visible.
  state.interactive = hourlyPage || airPage || cryptoPage;
  if (cryptoPage) {
    state.cryptoSort = cryptoGainers ? weather::CryptoSort::Gainers
                                     : (cryptoLosers ? weather::CryptoSort::Losers : weather::CryptoSort::Table);
    state.hint = cryptoGainers ? "上下排序 涨幅榜"
                               : (cryptoLosers ? "上下排序 跌幅榜" : "上下排序 表序");
  }

  if (std::strcmp(variant, "busy") == 0) {
    state.busy = true;
    state.status = "正在连接 WiFi";
  } else if (std::strcmp(variant, "error") == 0) {
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

  weather::renderWeatherScreen(target, state);

  const int rc = writePbm(out);
  if (rc != 0) {
    std::fprintf(stderr, "error: cannot write %s\n", out);
    return rc;
  }
  std::printf("wrote %s (panel %dx%d, logical portrait 480x800), variant=%s%s%s\n", out, kPanelWidth,
              kPanelHeight, variant, responsePath != nullptr ? ", response=" : "",
              responsePath != nullptr ? responsePath : "");
  return 0;
}
