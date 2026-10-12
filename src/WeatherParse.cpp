#include "WeatherParse.h"

#include <StreamingJsonParser.h>

#include <cstdlib>
#include <cstring>

namespace weather {
namespace {

constexpr uint8_t kMaxKeyLength = 31;

struct ParseContext {
  enum class Section : uint8_t { None, Current, Hourly, Daily };
  enum class Target : uint8_t { Weather, Air };

  Target target = Target::Weather;
  WeatherReport* out = nullptr;   // weather target
  AirQuality* air = nullptr;      // air target
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

void onObjectEnd(void* ctx) {
  ParseContext& p = *static_cast<ParseContext*>(ctx);
  p.section = ParseContext::Section::None;
  p.inArray = false;
  p.index = 0;
}

void onArrayStart(void* ctx) {
  ParseContext& p = *static_cast<ParseContext*>(ctx);
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
  if (p.target == ParseContext::Target::Weather && p.section == ParseContext::Section::Current &&
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
