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

// Parses a CoinCodex /api/coincodex/get_coin_history response — an object keyed
// by symbol whose values are [timestamp, price, volume, market cap] series:
//
//   {"BTC":[[1791273600,86034.07,...],[...]], "ETH":[...], ...}
//
// The last point is the current price, the 24 h change comes from the point four
// samples back (four samples a day) and the series becomes the sparkline. The
// live firmware gets one such response per coin, so keys the table does not know
// are ignored and `out` is not cleared: only the coins present are updated.
bool parseCoinSeries(const char* json, size_t length, CryptoBoard& out);

// Same, but for a whole board: starts from an empty board and requires at least
// half the table to be present (otherwise the page would be misleading).
bool parseCrypto(const char* json, size_t length, CryptoBoard& out);

// Parses a geocoding-api.open-meteo.com /v1/search response's first result
// (name, country, timezone, latitude, longitude).
bool parseGeocode(const char* json, size_t length, GeoLocation& out);

}  // namespace weather
