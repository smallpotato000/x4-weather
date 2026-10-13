#include "WeatherParse.h"

#include <StreamingJsonParser.h>

#include <cstdlib>
#include <cstring>

namespace weather {
namespace {

constexpr uint8_t kMaxKeyLength = 31;

// The API sends 168 hourly points per coin; buffer them (int16 relative steps)
// and downsample on commit.
constexpr uint16_t kSparklineRawMax = 200;

struct ParseContext {
  enum class Section : uint8_t { None, Current, Hourly, Daily };
  enum class Target : uint8_t { Weather, Air, Crypto, Geo };

  Target target = Target::Weather;
  WeatherReport* out = nullptr;   // weather target
  AirQuality* air = nullptr;      // air target
  CryptoBoard* crypto = nullptr;  // crypto target
  GeoLocation* geo = nullptr;     // geocoding target
  uint8_t coinValid = 0;          // coins with a price in this response
  // Crypto: the markets response is an array of coin objects, each with a
  // nested sparkline object, so the coin is identified by its "id" field and
  // committed when its object closes (field order independent).
  uint8_t cryptoDepth = 0;
  bool inSparkline = false;
  CoinQuote staging;              // usd / change24h of the coin being read
  char coinId[24] = {0};
  bool sawPrice = false;
  int16_t rawSpark[kSparklineRawMax] = {0};
  uint16_t rawCount = 0;
  float rawFirst = 0.0f;
  bool geoResultTaken = false;    // first geocoding result already consumed
  bool geoHasCoords = false;
  Section section = Section::None;
  char key[kMaxKeyLength + 1] = {0};
  char arrayKey[kMaxKeyLength + 1] = {0};
  bool inArray = false;
  uint8_t index = 0;     // element index inside the current array
  uint8_t dayCount = 0;  // daily.time entries seen
  bool sawCurrent = false;
};

bool keyIs(const char* key, const char* expected) { return std::strcmp(key, expected) == 0; }

void copyKey(char* destination, const char* source, size_t length) {
  const size_t n = length < kMaxKeyLength ? length : kMaxKeyLength;
  std::memcpy(destination, source, n);
  destination[n] = '\0';
}

// --- StreamingJsonParser callbacks -----------------------------------------

void onKey(void* ctx, const char* key, size_t len) {
  copyKey(static_cast<ParseContext*>(ctx)->key, key, len);
}

void onObjectStart(void* ctx) {
  ParseContext& p = *static_cast<ParseContext*>(ctx);

  if (p.target == ParseContext::Target::Crypto) {
    ++p.cryptoDepth;
    if (p.cryptoDepth == 1) {
      p.staging = CoinQuote{};
      p.coinId[0] = '\0';
      p.sawPrice = false;
      p.inSparkline = false;
      p.rawCount = 0;
      p.rawFirst = 0.0f;
    } else if (p.cryptoDepth == 2 && keyIs(p.key, "sparkline_in_7d")) {
      p.inSparkline = true;
    }
    p.section = ParseContext::Section::None;
    return;
  }

  if (p.target == ParseContext::Target::Geo) {
    // Only the first element of `results` is interesting.
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

// Downsample the buffered week and store the coin under its table index.
void commitCoin(ParseContext& p) {
  const int index = coinIndexForApiId(p.coinId);
  if (index < 0 || !p.sawPrice) return;

  CoinQuote quote;
  quote.usd = p.staging.usd;
  quote.change24h = p.staging.change24h;
  quote.valid = true;
  if (p.rawCount >= 2 && p.rawFirst > 0.0f) {
    int16_t low = p.rawSpark[0];
    int16_t high = p.rawSpark[0];
    for (uint16_t i = 1; i < p.rawCount; ++i) {
      if (p.rawSpark[i] < low) low = p.rawSpark[i];
      if (p.rawSpark[i] > high) high = p.rawSpark[i];
    }
    quote.sparkCount = kSparklinePoints;
    quote.sparkRef = p.rawFirst;
    quote.sparkMin = low;
    quote.sparkMax = high;
    for (uint8_t i = 0; i < kSparklinePoints; ++i) {
      const uint32_t source = static_cast<uint32_t>(i) * (p.rawCount - 1) / (kSparklinePoints - 1);
      quote.spark[i] = p.rawSpark[source];
    }
  }
  p.crypto->coins[index] = quote;
  ++p.coinValid;
}

void onObjectEnd(void* ctx) {
  ParseContext& p = *static_cast<ParseContext*>(ctx);

  if (p.target == ParseContext::Target::Crypto) {
    if (p.cryptoDepth == 0) return;
    --p.cryptoDepth;
    if (p.cryptoDepth == 0) {
      commitCoin(p);  // the coin object closed
    } else if (p.cryptoDepth == 1) {
      p.inSparkline = false;  // leaving sparkline_in_7d
    }
    return;
  }

  if (p.target == ParseContext::Target::Geo && p.section == ParseContext::Section::Current) {
    p.geoResultTaken = true;  // ignore any further results
  }
  p.section = ParseContext::Section::None;
  p.inArray = false;
  p.index = 0;
}

void onArrayStart(void* ctx) {
  ParseContext& p = *static_cast<ParseContext*>(ctx);

  if (p.target == ParseContext::Target::Geo) {
    // `results` is the geocoding array; its first object is the city we want.
    p.inArray = keyIs(p.key, "results");
    p.index = 0;
    return;
  }
  if (p.target == ParseContext::Target::Crypto) {
    if (p.cryptoDepth == 2 && keyIs(p.key, "price")) {
      p.inSparkline = true;
      p.rawCount = 0;
      p.rawFirst = 0.0f;
    }
    return;
  }
  if (p.target != ParseContext::Target::Weather) return;

  if (p.section != ParseContext::Section::Daily && p.section != ParseContext::Section::Hourly) return;
  p.inArray = true;
  p.index = 0;
  copyKey(p.arrayKey, p.key, std::strlen(p.key));
}

void onArrayEnd(void* ctx) { static_cast<ParseContext*>(ctx)->inArray = false; }

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
    if (p.inSparkline) {
      if (p.rawCount >= kSparklineRawMax) return;
      if (p.rawCount == 0) {
        p.rawFirst = number;
        p.rawSpark[p.rawCount++] = 0;  // first point is the reference
      } else if (p.rawFirst > 0.0f) {
        float relative = (number / p.rawFirst - 1.0f) * 10000.0f;
        if (relative > 32000.0f) relative = 32000.0f;
        if (relative < -32000.0f) relative = -32000.0f;
        p.rawSpark[p.rawCount++] = static_cast<int16_t>(relative);
      }
      return;
    }
    if (p.cryptoDepth == 1) {
      if (keyIs(p.key, "current_price")) {
        p.staging.usd = number;
        p.sawPrice = true;
      } else if (keyIs(p.key, "price_change_percentage_24h")) {
        p.staging.change24h = number;
      }
    }
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

void copyTimestamp(const char* value, size_t len, char* destination, size_t capacity) {
  const size_t n = len < capacity - 1 ? len : capacity - 1;
  std::memcpy(destination, value, n);
  destination[n] = '\0';
}

void onString(void* ctx, const char* value, size_t len) {
  ParseContext& p = *static_cast<ParseContext*>(ctx);

  if (p.target == ParseContext::Target::Air) {
    if (p.section == ParseContext::Section::Current && keyIs(p.key, "time")) {
      copyTimestamp(value, len, p.air->time, sizeof(p.air->time));
    }
    return;
  }

  if (p.target == ParseContext::Target::Crypto) {
    if (p.cryptoDepth == 1 && keyIs(p.key, "id")) {
      copyTimestamp(value, len, p.coinId, sizeof(p.coinId));
    }
    return;
  }

  if (p.target == ParseContext::Target::Geo) {
    if (p.section != ParseContext::Section::Current) return;
    if (keyIs(p.key, "name")) {
      copyTimestamp(value, len, p.geo->name, sizeof(p.geo->name));
    } else if (keyIs(p.key, "country")) {
      copyTimestamp(value, len, p.geo->country, sizeof(p.geo->country));
    } else if (keyIs(p.key, "timezone")) {
      copyTimestamp(value, len, p.geo->timezone, sizeof(p.geo->timezone));
    }
    return;
  }

  if (p.section == ParseContext::Section::Current) {
    if (keyIs(p.key, "time")) copyTimestamp(value, len, p.out->current.time, sizeof(p.out->current.time));
    return;
  }

  if (!p.inArray || !keyIs(p.arrayKey, "time")) return;
  if (p.section == ParseContext::Section::Daily) {
    if (p.index < kForecastDays) {
      DailyForecast& day = p.out->days[p.index];
      copyTimestamp(value, len, day.date, sizeof(day.date));
      day.valid = len >= 10;
    }
    ++p.index;
    if (p.index > p.dayCount) p.dayCount = p.index;
  } else if (p.section == ParseContext::Section::Hourly) {
    if (p.index < kForecastHours) {
      HourlyForecast& hour = p.out->hours[p.index];
      copyTimestamp(value, len, hour.time, sizeof(hour.time));
      hour.valid = len >= 13;
    }
    ++p.index;
    if (p.index > p.out->hourCount) p.out->hourCount = p.index;
  }
}

void onBool(void* ctx, bool value) {
  ParseContext& p = *static_cast<ParseContext*>(ctx);
  if (p.target != ParseContext::Target::Weather) return;
  if (p.section == ParseContext::Section::Current &&
      keyIs(p.key, "is_day")) {
    p.out->current.isDay = value;
  }
}

void onNull(void*) {}

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

struct CryptoStreamParser::Impl {
  ParseContext ctx;
  StreamingJsonParser parser;

  explicit Impl(CryptoBoard& board) : parser(makeCallbacks(ctx)) {
    ctx.target = ParseContext::Target::Crypto;
    ctx.crypto = &board;
  }
};

CryptoStreamParser::CryptoStreamParser(CryptoBoard& board) : impl_(new Impl(board)) {}

CryptoStreamParser::~CryptoStreamParser() { delete impl_; }

void CryptoStreamParser::feed(const char* data, size_t length) {
  if (data == nullptr || length == 0) return;
  impl_->parser.feed(data, length);
}

bool CryptoStreamParser::finish() const {
  // A couple of missing ids is survivable (the row renders as "--"); half of
  // them means the response shape changed and the page would be misleading.
  return !impl_->parser.hasError() && impl_->ctx.coinValid * 2 >= kCoinCount;
}

bool parseCrypto(const char* json, size_t length, CryptoBoard& out) {
  if (json == nullptr || length == 0) return false;

  CryptoBoard parsed;
  CryptoStreamParser sink(parsed);
  sink.feed(json, length);
  if (!sink.finish()) return false;

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

}  // namespace weather
