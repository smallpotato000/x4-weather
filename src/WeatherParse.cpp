#include "WeatherParse.h"

#include <StreamingJsonParser.h>

#include <cstdlib>
#include <cstring>

namespace weather {
namespace {

constexpr uint8_t kMaxKeyLength = 31;

// One CoinCodex series response holds 41 points for a 7-day window at 4 samples
// a day; buffer them as floats and convert to the stored relative form on
// commit.
constexpr uint16_t kSeriesPointMax = 64;

struct ParseContext {
  enum class Section : uint8_t { None, Current, Hourly, Daily };
  enum class Target : uint8_t { Weather, Air, Crypto, Geo };

  Target target = Target::Weather;
  WeatherReport* out = nullptr;   // weather target
  AirQuality* air = nullptr;      // air target
  CryptoBoard* crypto = nullptr;  // crypto target
  GeoLocation* geo = nullptr;     // geocoding target
  Section section = Section::None;
  char key[kMaxKeyLength + 1] = {0};
  char arrayKey[kMaxKeyLength + 1] = {0};
  bool inArray = false;
  uint8_t index = 0;     // element index inside the current array
  uint8_t dayCount = 0;  // daily.time entries seen
  bool sawCurrent = false;

  // Crypto: {"BTC":[[ts, price, volume, market cap], ...], ...} — the key names
  // the coin, the outer array is the series, each inner array is one sample.
  int pendingCoin = -1;    // coin whose series is about to start
  uint8_t seriesCoin = 0;  // coin being collected
  bool inSeries = false;
  bool inCandle = false;
  uint8_t candleField = 0;
  float seriesPrice[kSeriesPointMax] = {0};
  uint16_t seriesCount = 0;
  uint8_t seriesCommitted = 0;

  // Geocoding: only the first entry of `results` is wanted.
  bool geoResultTaken = false;
  bool geoHasCoords = false;
};

bool keyIs(const char* key, const char* expected) { return std::strcmp(key, expected) == 0; }

void copyKey(char* destination, const char* source, size_t length) {
  const size_t n = length < kMaxKeyLength ? length : kMaxKeyLength;
  std::memcpy(destination, source, n);
  destination[n] = '\0';
}

void copyText(const char* value, size_t len, char* destination, size_t capacity) {
  const size_t n = len < capacity - 1 ? len : capacity - 1;
  std::memcpy(destination, value, n);
  destination[n] = '\0';
}

// --- StreamingJsonParser callbacks -----------------------------------------

void onKey(void* ctx, const char* key, size_t len) {
  ParseContext& p = *static_cast<ParseContext*>(ctx);
  copyKey(p.key, key, len);
  if (p.target == ParseContext::Target::Crypto) {
    p.pendingCoin = coinIndexForApiId(p.key);  // -1 for anything that is not a coin
  }
}

void onObjectStart(void* ctx) {
  ParseContext& p = *static_cast<ParseContext*>(ctx);

  if (p.target == ParseContext::Target::Crypto) {
    p.section = ParseContext::Section::None;  // the series carries no objects
    return;
  }

  if (p.target == ParseContext::Target::Geo) {
    p.section = (p.inArray && !p.geoResultTaken) ? ParseContext::Section::Current
                                                 : ParseContext::Section::None;
    return;
  }

  if (keyIs(p.key, "current")) {
    p.section = ParseContext::Section::Current;
  } else if (p.target == ParseContext::Target::Weather && keyIs(p.key, "hourly")) {
    p.section = ParseContext::Section::Hourly;
  } else if (p.target == ParseContext::Target::Weather && keyIs(p.key, "daily")) {
    p.section = ParseContext::Section::Daily;
  } else {
    // *_units / the top-level header object / hourly blocks of other APIs:
    // nothing we need.
    p.section = ParseContext::Section::None;
  }
}

// Turns the buffered series into a CoinQuote: the last point is the price, four
// samples back is 24 h ago, and the whole series becomes the sparkline (stored
// in 0.01% steps relative to its first point, so the RTC cache stays small and
// scale-free).
void commitSeries(ParseContext& p) {
  if (p.seriesCount < 2 || p.crypto == nullptr) return;

  const float reference = p.seriesPrice[0];
  if (!(reference > 0.0f)) return;

  CoinQuote& quote = p.crypto->coins[p.seriesCoin];
  quote = CoinQuote{};
  quote.usd = p.seriesPrice[p.seriesCount - 1];
  quote.change24h =
      p.seriesCount >= 5 ? (quote.usd / p.seriesPrice[p.seriesCount - 5] - 1.0f) * 100.0f : 0.0f;
  quote.sparkRef = reference;
  quote.valid = true;

  // 41 points for a 7-day window fits kSparklinePoints; longer responses are
  // downsampled.
  const bool fits = p.seriesCount <= kSparklinePoints;
  const uint16_t stored = fits ? p.seriesCount : kSparklinePoints;
  int16_t low = 0;
  int16_t high = 0;
  for (uint16_t i = 0; i < stored; ++i) {
    const uint32_t source =
        fits ? i : static_cast<uint32_t>(i) * (p.seriesCount - 1) / (kSparklinePoints - 1);
    float relative = (p.seriesPrice[source] / reference - 1.0f) * 10000.0f;
    if (relative > 32000.0f) relative = 32000.0f;
    if (relative < -32000.0f) relative = -32000.0f;
    const int16_t value = static_cast<int16_t>(relative);
    quote.spark[i] = value;
    if (i == 0 || value < low) low = value;
    if (i == 0 || value > high) high = value;
  }
  quote.sparkCount = static_cast<uint8_t>(stored);
  quote.sparkMin = low;
  quote.sparkMax = high;
  ++p.seriesCommitted;
}

void onObjectEnd(void* ctx) {
  ParseContext& p = *static_cast<ParseContext*>(ctx);

  if (p.target == ParseContext::Target::Geo && p.section == ParseContext::Section::Current) {
    p.geoResultTaken = true;  // ignore any further results
  }
  p.section = ParseContext::Section::None;
  p.inArray = false;
  p.index = 0;
}

void onArrayStart(void* ctx) {
  ParseContext& p = *static_cast<ParseContext*>(ctx);

  if (p.target == ParseContext::Target::Crypto) {
    if (!p.inSeries && p.pendingCoin >= 0) {
      p.inSeries = true;
      p.inCandle = false;
      p.seriesCoin = static_cast<uint8_t>(p.pendingCoin);
      p.pendingCoin = -1;
      p.seriesCount = 0;
    } else if (p.inSeries) {
      p.inCandle = true;  // one [ts, price, volume, market cap] sample
      p.candleField = 0;
    }
    return;
  }

  if (p.target == ParseContext::Target::Geo) {
    // `results` is the geocoding array; its first object is the city we want.
    p.inArray = keyIs(p.key, "results");
    p.index = 0;
    return;
  }
  if (p.target != ParseContext::Target::Weather) return;

  if (p.section != ParseContext::Section::Daily && p.section != ParseContext::Section::Hourly) return;
  p.inArray = true;
  p.index = 0;
  copyKey(p.arrayKey, p.key, std::strlen(p.key));
}

void onArrayEnd(void* ctx) {
  ParseContext& p = *static_cast<ParseContext*>(ctx);

  if (p.target == ParseContext::Target::Crypto) {
    if (p.inCandle) {
      p.inCandle = false;
    } else if (p.inSeries) {
      commitSeries(p);
      p.inSeries = false;
    }
    return;
  }
  p.inArray = false;
}

void onBool(void* ctx, bool value) {
  ParseContext& p = *static_cast<ParseContext*>(ctx);
  if (p.target != ParseContext::Target::Weather) return;
  if (p.section == ParseContext::Section::Current && keyIs(p.key, "is_day")) {
    p.out->current.isDay = value;
  }
}

void onNull(void*) {}

// Consumes one element of the daily arrays we track.
void handleDailyNumber(ParseContext& p, float number) {
  const bool tracked = keyIs(p.arrayKey, "temperature_2m_max") || keyIs(p.arrayKey, "temperature_2m_min") ||
                       keyIs(p.arrayKey, "weather_code");
  if (!tracked) return;  // not an array we consume; leave the index alone
  if (p.index >= kForecastDays) {
    ++p.index;  // longer response than the layout holds
    return;
  }
  DailyForecast& day = p.out->days[p.index];
  if (keyIs(p.arrayKey, "temperature_2m_max")) {
    day.tMax = number;
  } else if (keyIs(p.arrayKey, "temperature_2m_min")) {
    day.tMin = number;
  } else {
    day.code = static_cast<uint8_t>(number + 0.5f);
  }
  ++p.index;
}

void handleHourlyNumber(ParseContext& p, float number) {
  const bool tracked = keyIs(p.arrayKey, "temperature_2m") || keyIs(p.arrayKey, "weather_code") ||
                       keyIs(p.arrayKey, "precipitation_probability");
  if (!tracked) return;
  if (p.index >= kForecastHours) {
    ++p.index;
    return;
  }
  HourlyForecast& hour = p.out->hours[p.index];
  if (keyIs(p.arrayKey, "temperature_2m")) {
    hour.temperatureC = number;
  } else if (keyIs(p.arrayKey, "weather_code")) {
    hour.code = static_cast<uint8_t>(number + 0.5f);
  } else {
    hour.precipitationChance = static_cast<uint8_t>(number + 0.5f);
  }
  ++p.index;
}

void onNumber(void* ctx, const char* value, size_t) {
  ParseContext& p = *static_cast<ParseContext*>(ctx);
  const float number = std::strtof(value, nullptr);

  if (p.target == ParseContext::Target::Crypto) {
    if (!p.inCandle) return;
    if (p.candleField == 1) {
      if (p.seriesCount < kSeriesPointMax) p.seriesPrice[p.seriesCount++] = number;
    }
    ++p.candleField;  // 0 = timestamp, 1 = price, then volume / market cap
    return;
  }

  if (p.target == ParseContext::Target::Geo) {
    if (p.section != ParseContext::Section::Current) return;
    if (keyIs(p.key, "latitude")) {
      p.geo->latitude = number;
      p.geoHasCoords = true;
    } else if (keyIs(p.key, "longitude")) {
      p.geo->longitude = number;
      p.geoHasCoords = true;
    }
    return;
  }

  if (p.target == ParseContext::Target::Air) {
    if (p.section != ParseContext::Section::Current) return;
    AirQuality& air = *p.air;
    if (keyIs(p.key, "european_aqi")) {
      air.europeanAqi = static_cast<int>(number + 0.5f);
      p.sawCurrent = true;
    } else if (keyIs(p.key, "pm2_5")) {
      air.pm25 = number;
      p.sawCurrent = true;
    } else if (keyIs(p.key, "pm10")) {
      air.pm10 = number;
    } else if (keyIs(p.key, "nitrogen_dioxide")) {
      air.no2 = number;
    } else if (keyIs(p.key, "ozone")) {
      air.ozone = number;
    }
    return;
  }

  if (p.section == ParseContext::Section::Current) {
    CurrentWeather& now = p.out->current;
    if (keyIs(p.key, "temperature_2m")) {
      now.temperatureC = number;
      p.sawCurrent = true;
    } else if (keyIs(p.key, "apparent_temperature")) {
      now.apparentC = number;
    } else if (keyIs(p.key, "wind_speed_10m")) {
      now.windKph = number;
    } else if (keyIs(p.key, "relative_humidity_2m")) {
      now.humidity = static_cast<uint8_t>(number + 0.5f);
    } else if (keyIs(p.key, "weather_code")) {
      now.code = static_cast<uint8_t>(number + 0.5f);
      p.sawCurrent = true;
    } else if (keyIs(p.key, "is_day")) {
      now.isDay = number != 0.0f;
    }
    return;
  }

  if (!p.inArray) return;
  if (p.section == ParseContext::Section::Daily) {
    handleDailyNumber(p, number);
  } else if (p.section == ParseContext::Section::Hourly) {
    handleHourlyNumber(p, number);
  }
}

void onString(void* ctx, const char* value, size_t len) {
  ParseContext& p = *static_cast<ParseContext*>(ctx);

  if (p.target == ParseContext::Target::Crypto) return;  // the series is all numbers

  if (p.target == ParseContext::Target::Air) {
    if (p.section == ParseContext::Section::Current && keyIs(p.key, "time")) {
      copyText(value, len, p.air->time, sizeof(p.air->time));
    }
    return;
  }

  if (p.target == ParseContext::Target::Geo) {
    if (p.section != ParseContext::Section::Current) return;
    if (keyIs(p.key, "name")) {
      copyText(value, len, p.geo->name, sizeof(p.geo->name));
    } else if (keyIs(p.key, "country")) {
      copyText(value, len, p.geo->country, sizeof(p.geo->country));
    } else if (keyIs(p.key, "timezone")) {
      copyText(value, len, p.geo->timezone, sizeof(p.geo->timezone));
    }
    return;
  }

  if (p.section == ParseContext::Section::Current) {
    if (keyIs(p.key, "time")) copyText(value, len, p.out->current.time, sizeof(p.out->current.time));
    return;
  }

  if (!p.inArray || !keyIs(p.arrayKey, "time")) return;
  if (p.section == ParseContext::Section::Daily) {
    if (p.index < kForecastDays) {
      DailyForecast& day = p.out->days[p.index];
      copyText(value, len, day.date, sizeof(day.date));
      day.valid = len >= 10;
    }
    ++p.index;
    if (p.index > p.dayCount) p.dayCount = p.index;
  } else if (p.section == ParseContext::Section::Hourly) {
    if (p.index < kForecastHours) {
      HourlyForecast& hour = p.out->hours[p.index];
      copyText(value, len, hour.time, sizeof(hour.time));
      hour.valid = len >= 13;
    }
    ++p.index;
    if (p.index > p.out->hourCount) p.out->hourCount = p.index;
  }
}

JsonCallbacks makeCallbacks(ParseContext& ctx) {
  JsonCallbacks callbacks{};
  callbacks.ctx = &ctx;
  callbacks.onKey = &onKey;
  callbacks.onString = &onString;
  callbacks.onNumber = &onNumber;
  callbacks.onBool = &onBool;
  callbacks.onNull = &onNull;
  callbacks.onObjectStart = &onObjectStart;
  callbacks.onObjectEnd = &onObjectEnd;
  callbacks.onArrayStart = &onArrayStart;
  callbacks.onArrayEnd = &onArrayEnd;
  return callbacks;
}

}  // namespace

bool parseForecast(const char* json, size_t length, WeatherReport& out) {
  if (json == nullptr || length == 0) return false;

  WeatherReport parsed;
  ParseContext ctx;
  ctx.target = ParseContext::Target::Weather;
  ctx.out = &parsed;

  StreamingJsonParser parser(makeCallbacks(ctx));
  parser.feed(json, length);

  if (parser.hasError() || !ctx.sawCurrent) return false;

  parsed.current.valid = true;
  parsed.dayCount = ctx.dayCount > kForecastDays ? kForecastDays : ctx.dayCount;
  parsed.hourCount = parsed.hourCount > kForecastHours ? kForecastHours : parsed.hourCount;
  parsed.ok = true;
  out = parsed;
  return true;
}

bool parseAirQuality(const char* json, size_t length, AirQuality& out) {
  if (json == nullptr || length == 0) return false;

  AirQuality parsed;
  ParseContext ctx;
  ctx.target = ParseContext::Target::Air;
  ctx.air = &parsed;

  StreamingJsonParser parser(makeCallbacks(ctx));
  parser.feed(json, length);

  if (parser.hasError() || !ctx.sawCurrent) return false;

  parsed.valid = true;
  out = parsed;
  return true;
}

bool parseCoinSeries(const char* json, size_t length, CryptoBoard& out) {
  if (json == nullptr || length == 0) return false;

  ParseContext ctx;
  ctx.target = ParseContext::Target::Crypto;
  ctx.crypto = &out;

  StreamingJsonParser parser(makeCallbacks(ctx));
  parser.feed(json, length);

  return !parser.hasError() && ctx.seriesCommitted > 0;
}

bool parseCrypto(const char* json, size_t length, CryptoBoard& out) {
  CryptoBoard parsed;
  if (!parseCoinSeries(json, length, parsed)) return false;

  uint8_t valid = 0;
  for (uint8_t i = 0; i < kCoinCount; ++i) {
    if (parsed.coins[i].valid) ++valid;
  }
  // A couple of missing symbols is survivable (the row renders as "--"); half of
  // them means the response shape changed and the page would be misleading.
  if (valid * 2 < kCoinCount) return false;

  parsed.ok = true;
  out = parsed;
  return true;
}

bool parseGeocode(const char* json, size_t length, GeoLocation& out) {
  if (json == nullptr || length == 0) return false;

  GeoLocation parsed;
  ParseContext ctx;
  ctx.target = ParseContext::Target::Geo;
  ctx.geo = &parsed;

  StreamingJsonParser parser(makeCallbacks(ctx));
  parser.feed(json, length);

  if (parser.hasError() || !ctx.geoResultTaken || !ctx.geoHasCoords) return false;

  parsed.valid = true;
  out = parsed;
  return true;
}

}  // namespace weather
