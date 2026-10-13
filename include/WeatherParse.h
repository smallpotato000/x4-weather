#pragma once

// Open-Meteo response parsers — freestanding (the SDK's JsonSax parser and the
// data model only), so the exact code the firmware runs can be exercised by the
// host tests in tools/preview/test_parse.cpp.

#include <stddef.h>
#include <stdint.h>

#include "WeatherData.h"

namespace weather {

// Parses a /v1/forecast response with the `current`, `hourly` and `daily`
// blocks this firmware requests (see WeatherClient::fetch for the URL). Returns
// true when a usable current observation was found; `out` is then fully
// populated (out.ok = true, out.current.valid = true, dayCount/hourCount capped
// at kForecastDays/kForecastHours). On failure `out` is left untouched.
bool parseForecast(const char* json, size_t length, WeatherReport& out);

// Parses a air-quality-api.open-meteo.com /v1/air-quality response's `current`
// block (european_aqi, pm2_5, pm10, nitrogen_dioxide, ozone, time). On failure
// `out` is left untouched.
bool parseAirQuality(const char* json, size_t length, AirQuality& out);

// Parses a CoinGecko /api/v3/coins/markets response (an array of coin objects
// with current_price, price_change_percentage_24h and a 168-point
// sparkline_in_7d.price) into the coin table. Sparklines are downsampled to
// kSparklinePoints. Coins the response omits stay invalid; fewer than half the
// table present is treated as a failure.
bool parseCrypto(const char* json, size_t length, CryptoBoard& out);

// Incremental form of parseCrypto, so the ~28 KB markets payload can be fed to
// the SAX parser straight off the socket instead of sitting in RAM whole.
class CryptoStreamParser {
 public:
  explicit CryptoStreamParser(CryptoBoard& board);
  ~CryptoStreamParser();
  CryptoStreamParser(const CryptoStreamParser&) = delete;
  CryptoStreamParser& operator=(const CryptoStreamParser&) = delete;

  void feed(const char* data, size_t length);
  // True when the JSON was well formed and at least half the table arrived.
  bool finish() const;

 private:
  struct Impl;
  Impl* impl_;
};

// Parses a geocoding-api.open-meteo.com /v1/search response's first result
// (name, country, timezone, latitude, longitude).
bool parseGeocode(const char* json, size_t length, GeoLocation& out);

}  // namespace weather
