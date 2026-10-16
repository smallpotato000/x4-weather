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
  enum class Target : uint8_t { Weather, Air, Quotes, Candles, Geo };

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

  // Quotes (CoinLore): an array of coin objects with string numbers.
  char stagingSymbol[8] = {0};
  float stagingUsd = 0.0f;
  float stagingChange = 0.0f;
  bool sawQuotePrice = false;
  uint8_t quotesCommitted = 0;

  // Candles (Gate.io): [[ts, quote volume, close, high, low, open, ...], ...].
  uint8_t candleCoin = 0;   // resolved from the symbol before parsing starts
  uint8_t candleClose = 2;  // Gate: 2; Binance klines: 4
  bool inSeries = false;
  bool inCandle = false;
  uint8_t candleField = 0;
  float seriesPrice[kSeriesPointMax] = {0};
  uint16_t seriesCount = 0;
  uint8_t seriesCommitted = 0;

  // Geocoding: `results` is a list; each object is staged and committed so both
  // the single-result and the candidate-list API share one parser.
  GeoCandidates* candidates = nullptr;
  char stagingName[32] = {0};
  char stagingAdmin1[32] = {0};
  char stagingCountry[24] = {0};
  char stagingTimezone[32] = {0};
  float stagingLatitude = 0.0f;
  float stagingLongitude = 0.0f;
  bool geoHasCoords = false;
  uint8_t geoCommitted = 0;
  uint8_t geoArrayDepth = 0;  // `results` plus any nested array (postcodes, ...)
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
  copyKey(static_cast<ParseContext*>(ctx)->key, key, len);
}

void onObjectStart(void* ctx) {
  ParseContext& p = *static_cast<ParseContext*>(ctx);

  if (p.target == ParseContext::Target::Quotes) {
    p.section = ParseContext::Section::Current;  // one coin object
    p.stagingSymbol[0] = '\0';
    p.sawQuotePrice = false;
    return;
  }
  if (p.target == ParseContext::Target::Candles) {
    p.section = ParseContext::Section::None;  // arrays only
    return;
  }

  if (p.target == ParseContext::Target::Geo) {
    const bool wanted = p.inArray && p.index < kGeoCandidateMax;
    if (wanted) {
      p.stagingName[0] = '\0';
      p.stagingAdmin1[0] = '\0';
      p.stagingCountry[0] = '\0';
      p.stagingTimezone[0] = '\0';
      p.stagingLatitude = 0.0f;
      p.stagingLongitude = 0.0f;
      p.geoHasCoords = false;
    }
    p.section = wanted ? ParseContext::Section::Current : ParseContext::Section::None;
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

// Stores a price series as the coin's sparkline, in 0.01% steps relative to its
// first point: scale-free, and small enough to live in the RTC cache. 42 points
// for a 7-day window fits kSparklinePoints; longer series are downsampled.
bool storeSparkline(CoinQuote& quote, const float* prices, uint16_t count) {
  if (count < 2) return false;
  const float reference = prices[0];
  if (!(reference > 0.0f)) return false;  // a zero reference means broken data

  quote.sparkRef = reference;
  const bool fits = count <= kSparklinePoints;
  const uint16_t stored = fits ? count : kSparklinePoints;
  int16_t low = 0;
  int16_t high = 0;
  for (uint16_t i = 0; i < stored; ++i) {
    const uint32_t source = fits ? i : static_cast<uint32_t>(i) * (count - 1) / (kSparklinePoints - 1);
    float relative = (prices[source] / reference - 1.0f) * 10000.0f;
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
  return true;
}

// Each CoinLore object is one coin: symbol + price + 24 h change (as strings).
void commitQuote(ParseContext& p) {
  if (p.crypto == nullptr || !p.sawQuotePrice) return;
  const int index = coinIndexForSymbol(p.stagingSymbol);
  if (index < 0) return;

  CoinQuote& quote = p.crypto->coins[index];
  quote.usd = p.stagingUsd;
  quote.change24h = p.stagingChange;
  quote.valid = true;
  ++p.quotesCommitted;
}

void onObjectEnd(void* ctx) {
  ParseContext& p = *static_cast<ParseContext*>(ctx);

  if (p.target == ParseContext::Target::Quotes) {
    if (p.section == ParseContext::Section::Current) commitQuote(p);
    p.section = ParseContext::Section::None;
    return;
  }
  if (p.target == ParseContext::Target::Candles) return;

  if (p.target == ParseContext::Target::Geo && p.section == ParseContext::Section::Current) {
    // Commit this result: the parser reports how many arrived, and callers take
    // the first (single-result API) or all of them (picker).
    if (p.geoHasCoords && p.candidates != nullptr && p.geoCommitted < kGeoCandidateMax) {
      GeoCandidate& candidate = p.candidates->items[p.geoCommitted];
      copyText(p.stagingName, std::strlen(p.stagingName), candidate.name, sizeof(candidate.name));
      copyText(p.stagingAdmin1, std::strlen(p.stagingAdmin1), candidate.admin1, sizeof(candidate.admin1));
      copyText(p.stagingCountry, std::strlen(p.stagingCountry), candidate.country,
               sizeof(candidate.country));
      copyText(p.stagingTimezone, std::strlen(p.stagingTimezone), candidate.timezone,
               sizeof(candidate.timezone));
      candidate.latitude = p.stagingLatitude;
      candidate.longitude = p.stagingLongitude;
      ++p.geoCommitted;
      p.candidates->count = p.geoCommitted;
    }
    p.section = ParseContext::Section::None;
    ++p.index;
    return;
  }
  p.section = ParseContext::Section::None;
  p.inArray = false;
  p.index = 0;
}

void onArrayStart(void* ctx) {
  ParseContext& p = *static_cast<ParseContext*>(ctx);

  if (p.target == ParseContext::Target::Quotes) return;  // its elements are objects

  if (p.target == ParseContext::Target::Candles) {
    if (!p.inSeries) {
      p.inSeries = true;
      p.inCandle = false;
      p.seriesCount = 0;
    } else {
      p.inCandle = true;  // one [ts, quote volume, close, high, low, open, ...] candle
      p.candleField = 0;
    }
    return;
  }

  if (p.target == ParseContext::Target::Geo) {
    if (keyIs(p.key, "results")) {
      p.inArray = true;
      p.index = 0;
      p.geoArrayDepth = 1;
    } else if (p.inArray) {
      // A nested array inside a result (postcodes): track it so its end does not
      // look like the end of the candidate list.
      ++p.geoArrayDepth;
    }
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

  if (p.target == ParseContext::Target::Geo) {
    if (p.inArray && p.geoArrayDepth > 0 && --p.geoArrayDepth == 0) p.inArray = false;
    return;
  }

  if (p.target == ParseContext::Target::Candles) {
    if (p.inCandle) {
      p.inCandle = false;
    } else if (p.inSeries) {
      if (p.crypto != nullptr &&
          storeSparkline(p.crypto->coins[p.candleCoin], p.seriesPrice, p.seriesCount)) {
        ++p.seriesCommitted;
      }
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

  if (p.target == ParseContext::Target::Candles) {
    if (!p.inCandle) return;
    if (p.candleField == p.candleClose && p.seriesCount < kSeriesPointMax) {
      p.seriesPrice[p.seriesCount++] = number;  // the close column
    }
    ++p.candleField;
    return;
  }

  if (p.target == ParseContext::Target::Geo) {
    if (p.section != ParseContext::Section::Current) return;
    if (keyIs(p.key, "latitude")) {
      p.stagingLatitude = number;
      p.geoHasCoords = true;
    } else if (keyIs(p.key, "longitude")) {
      p.stagingLongitude = number;
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

  if (p.target == ParseContext::Target::Quotes) {
    if (p.section != ParseContext::Section::Current) return;
    if (keyIs(p.key, "symbol")) {
      copyText(value, len, p.stagingSymbol, sizeof(p.stagingSymbol));
    } else if (keyIs(p.key, "price_usd")) {
      p.stagingUsd = std::strtof(value, nullptr);
      p.sawQuotePrice = true;
    } else if (keyIs(p.key, "percent_change_24h")) {
      p.stagingChange = std::strtof(value, nullptr);
    }
    return;
  }
  if (p.target == ParseContext::Target::Candles) {
    // Candle fields arrive quoted on both providers (Gate quotes everything,
    // Binance quotes the numbers and sends the timestamps bare).
    if (p.inCandle) {
      if (p.candleField == p.candleClose && p.seriesCount < kSeriesPointMax) {
        p.seriesPrice[p.seriesCount++] = std::strtof(value, nullptr);
      }
      ++p.candleField;
    }
    return;
  }

  if (p.target == ParseContext::Target::Air) {
    if (p.section == ParseContext::Section::Current && keyIs(p.key, "time")) {
      copyText(value, len, p.air->time, sizeof(p.air->time));
    }
    return;
  }

  if (p.target == ParseContext::Target::Geo) {
    if (p.section != ParseContext::Section::Current) return;
    if (keyIs(p.key, "name")) {
      copyText(value, len, p.stagingName, sizeof(p.stagingName));
    } else if (keyIs(p.key, "admin1")) {
      copyText(value, len, p.stagingAdmin1, sizeof(p.stagingAdmin1));
    } else if (keyIs(p.key, "country")) {
      copyText(value, len, p.stagingCountry, sizeof(p.stagingCountry));
    } else if (keyIs(p.key, "timezone")) {
      copyText(value, len, p.stagingTimezone, sizeof(p.stagingTimezone));
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

bool parseCoinQuotes(const char* json, size_t length, CryptoBoard& out) {
  if (json == nullptr || length == 0) return false;

  ParseContext ctx;
  ctx.target = ParseContext::Target::Quotes;
  ctx.crypto = &out;

  StreamingJsonParser parser(makeCallbacks(ctx));
  parser.feed(json, length);

  return !parser.hasError() && ctx.quotesCommitted > 0;
}

bool parseCoinCandles(const char* symbol, const char* json, size_t length, CryptoBoard& out,
                      uint8_t closeField) {
  if (json == nullptr || length == 0) return false;
  const int index = coinIndexForSymbol(symbol);
  if (index < 0) return false;

  ParseContext ctx;
  ctx.target = ParseContext::Target::Candles;
  ctx.crypto = &out;
  ctx.candleCoin = static_cast<uint8_t>(index);
  ctx.candleClose = closeField;

  StreamingJsonParser parser(makeCallbacks(ctx));
  parser.feed(json, length);

  return !parser.hasError() && ctx.seriesCommitted > 0;
}

bool parseCrypto(const char* json, size_t length, CryptoBoard& out) {
  CryptoBoard parsed;
  if (!parseCoinQuotes(json, length, parsed)) return false;

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

bool parseGeoCandidates(const char* json, size_t length, GeoCandidates& out) {
  if (json == nullptr || length == 0) return false;

  GeoCandidates parsed;
  ParseContext ctx;
  ctx.target = ParseContext::Target::Geo;
  ctx.candidates = &parsed;

  StreamingJsonParser parser(makeCallbacks(ctx));
  parser.feed(json, length);

  if (parser.hasError() || parsed.count == 0) return false;

  out = parsed;
  return true;
}

bool parseGeocode(const char* json, size_t length, GeoLocation& out) {
  GeoCandidates candidates;
  if (!parseGeoCandidates(json, length, candidates) || candidates.count == 0) return false;

  // The forecast request only needs the best hit; the picker uses the full list.
  const GeoCandidate& first = candidates.items[0];
  GeoLocation parsed;
  copyText(first.name, std::strlen(first.name), parsed.name, sizeof(parsed.name));
  copyText(first.country, std::strlen(first.country), parsed.country, sizeof(parsed.country));
  copyText(first.timezone, std::strlen(first.timezone), parsed.timezone, sizeof(parsed.timezone));
  parsed.latitude = first.latitude;
  parsed.longitude = first.longitude;
  parsed.valid = true;
  out = parsed;
  return true;
}

}  // namespace weather
