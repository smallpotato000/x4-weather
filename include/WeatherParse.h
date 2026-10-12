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

}  // namespace weather
