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

// Parses a CoinLore /api/ticker/?id=... response — an array of coin objects:
//
//   [{"id":"90","symbol":"BTC","price_usd":"83619.93",
//     "percent_change_24h":"-0.27", ...}, ...]
//
// Numbers arrive as strings. Keys the table does not know are ignored and `out`
// is not cleared: only the coins present are updated.
bool parseCoinQuotes(const char* json, size_t length, CryptoBoard& out);

// Parses a Gate.io /api/v4/spot/candlesticks response for `symbol`
// ([[ts, quote volume, close, high, low, open, ...], ...], oldest first) into
// that coin's 7-day sparkline, stored relative to the first close. The price and
// 24 h change are left alone — those come from the quotes request.
bool parseCoinCandles(const char* symbol, const char* json, size_t length, CryptoBoard& out);

// Quotes for a whole board: starts from an empty board and requires at least
// half the table to be present (otherwise the page would be misleading).
bool parseCrypto(const char* json, size_t length, CryptoBoard& out);

// Parses a geocoding-api.open-meteo.com /v1/search response's first result
// (name, country, timezone, latitude, longitude).
bool parseGeocode(const char* json, size_t length, GeoLocation& out);

// Parses up to kGeoCandidateMax results of the same response, which is what the
// city picker shows so same-named places ("Paris" in France and in Texas) can be
// told apart. Returns false when the response carries no usable result.
bool parseGeoCandidates(const char* json, size_t length, GeoCandidates& out);

}  // namespace weather
