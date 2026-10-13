#pragma once

// Weather data model + WMO weather-code mapping.
//
// Freestanding (no Arduino dependency) so the same code compiles in the
// firmware and in the host render preview under tools/preview.

#include <stddef.h>
#include <stdint.h>

namespace weather {

// How many forecast days the UI shows (the Open-Meteo daily arrays are parsed
// into at most this many entries).
static constexpr uint8_t kForecastDays = 3;

// How many hourly entries the "hourly" page lists.
static constexpr uint8_t kForecastHours = 12;

// How many coins the crypto page lists.
static constexpr uint8_t kCoinCount = 7;

// Sparkline resolution kept per coin (the API sends 168 hourly points; they are
// downsampled on commit so the RTC cache stays small).
static constexpr uint8_t kSparklinePoints = 48;

struct CurrentWeather {
  bool valid = false;
  char time[20] = {0};  // local ISO-8601, "2026-10-12T16:30"
  float temperatureC = 0.0f;
  float apparentC = 0.0f;
  float windKph = 0.0f;
  uint8_t humidity = 0;   // %
  uint8_t code = 0;       // WMO weather code
  bool isDay = true;
};

struct DailyForecast {
  bool valid = false;
  char date[11] = {0};  // "2026-10-12"
  uint8_t code = 0;
  float tMin = 0.0f;
  float tMax = 0.0f;
};

struct HourlyForecast {
  bool valid = false;
  char time[20] = {0};                 // "2026-10-12T18:00"
  float temperatureC = 0.0f;
  uint8_t code = 0;
  uint8_t precipitationChance = 0;     // %
};

// Air quality (Open-Meteo's air-quality API). EAQI is the European AQI index;
// the pollutant values are µg/m³.
struct AirQuality {
  bool valid = false;
  char time[20] = {0};
  int europeanAqi = 0;
  float pm25 = 0.0f;
  float pm10 = 0.0f;
  float no2 = 0.0f;
  float ozone = 0.0f;
};

// One row of the crypto page. The symbol/name come from the static table below;
// only the quote is fetched.
struct CoinInfo {
  const char* symbol;  // "BTC"
  const char* nameZh;  // "比特币"
  const char* apiId;   // provider id (CoinGecko), "bitcoin"
};

// Sparkline values are stored in 0.01% steps relative to `sparkRef` (the first
// point of the week), which keeps ±327% in an int16 and makes the shape
// independent of the price scale.
struct CoinQuote {
  bool valid = false;
  float usd = 0.0f;
  float change24h = 0.0f;  // percent, negative when down
  uint8_t sparkCount = 0;  // 0 = no trend for this coin
  int16_t spark[kSparklinePoints] = {0};
  int16_t sparkMin = 0;
  int16_t sparkMax = 0;
  float sparkRef = 0.0f;   // the price the percentages refer to
};

// Order of the crypto rows.
enum class CryptoSort : uint8_t {
  Table = 0,      // table order (BTC, ETH, ...)
  Gainers = 1,    // biggest 24 h gain first
  Losers = 2,     // biggest 24 h drop first
};
static constexpr uint8_t kCryptoSortCount = 3;

struct CryptoBoard {
  bool ok = false;
  uint32_t fetchedEpoch = 0;
  CoinQuote coins[kCoinCount];
};

// Geocoding result: the coordinates the forecast is actually requested for.
struct GeoLocation {
  bool valid = false;
  char name[32] = {0};      // name as returned by the API (ASCII)
  char country[24] = {0};
  char timezone[32] = {0};  // IANA name, informational
  float latitude = 0.0f;
  float longitude = 0.0f;
};

struct WeatherReport {
  bool ok = false;
  char place[32] = {0};  // display name from config (UTF-8)
  CurrentWeather current;
  DailyForecast days[kForecastDays];
  uint8_t dayCount = 0;
  HourlyForecast hours[kForecastHours];
  uint8_t hourCount = 0;
  AirQuality air;
  CryptoBoard crypto;
  // Epoch seconds of the local fetch time (0 when the clock was not set).
  uint32_t fetchedEpoch = 0;
};

// Coarse icon selection for the panel.
enum class Icon : uint8_t {
  Unknown,
  ClearDay,
  ClearNight,
  PartlyDay,
  PartlyNight,
  Cloudy,
  Fog,
  Drizzle,
  Rain,
  Snow,
  Thunder,
};

// UTF-8 Chinese description for a WMO weather code.
const char* conditionText(uint8_t wmoCode);

// Icon for a WMO code; `isDay` only changes clear/partly-cloudy results.
Icon conditionIcon(uint8_t wmoCode, bool isDay);

// 今天 / 明天 / 后天 for forecast row 0..2.
const char* dayLabel(uint8_t index);

// Chinese level text for a European AQI value (优 / 良 / 轻度污染 / ...).
const char* aqiLevelText(int europeanAqi);

// 0..5 level bucket for that value (drives the segmented scale on the AQI page).
uint8_t aqiLevelIndex(int europeanAqi);

// "18:00" from an ISO timestamp; empty string when malformed.
const char* formatHourLabel(const char* iso, char* out, size_t outLen);

// 周日..周六 for tm_wday-style 0..6.
const char* weekdayName(int wday);

// Weekday (0 = Sunday) for an ISO date string "YYYY-MM-DD"; -1 when malformed.
int weekdayFromIsoDate(const char* iso);

// "HH:MM" for an epoch-second local timestamp; falls back to "--:--" when the
// clock is unset (epoch == 0). Returns `out`.
const char* formatClock(uint32_t epoch, char* out, size_t outLen);

// Rounds to one decimal and prints it into `out` (no stdio float formatting in
// the firmware build). Returns `out`.
const char* formatTemp1(float celsius, char* out, size_t outLen);

// Rounds to a whole number (wind speed display).
const char* formatWhole(float value, char* out, size_t outLen);

// Short date for a forecast row: "10-12". Empty string when malformed.
const char* formatShortDate(const char* iso, char* out, size_t outLen);

// "$83,393" / "$2,514.03" / "$0.3298": thousands separators and adaptive
// decimals (0 above 10k, 2 above 1, 4 below).
const char* formatUsd(float usd, char* out, size_t outLen);

// "+2.4%" / "-3.6%" — explicit sign, one decimal.
const char* formatPercent(float percent, char* out, size_t outLen);

// Coin table entry (index order is the crypto page's display order).
const CoinInfo& coinInfo(uint8_t index);

// Short label for the crypto sort mode ("表序" / "涨幅榜" / "跌幅榜").
const char* cryptoSortName(CryptoSort sort);

// Index of the coin whose provider id matches, or -1.
int coinIndexForApiId(const char* apiId);

}  // namespace weather
