// Host tests for the Open-Meteo parsers (weather::parseForecast,
// weather::parseAirQuality) and the data model's helpers. The parsers are the
// same code the firmware runs — only the socket in front of them differs.
//
// Build/run through tools/test_host.sh.

#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

#include "WeatherData.h"
#include "WeatherParse.h"

namespace {

int gFailures = 0;

void check(bool condition, const std::string& what) {
  if (condition) {
    std::printf("  ok    %s\n", what.c_str());
  } else {
    std::printf("  FAIL  %s\n", what.c_str());
    ++gFailures;
  }
}

void checkFloat(float actual, float expected, const std::string& what) {
  const float delta = actual > expected ? actual - expected : expected - actual;
  check(delta < 0.05f, what + " (got " + std::to_string(actual) + ")");
}

void checkString(const char* actual, const char* expected, const std::string& what) {
  check(std::strcmp(actual, expected) == 0, what + " (got \"" + actual + "\")");
}

std::string readFile(const char* path, bool& ok) {
  std::ifstream file(path, std::ios::binary);
  ok = file.good();
  std::ostringstream buffer;
  buffer << file.rdbuf();
  return buffer.str();
}

// The recorded responses the firmware's two URLs return for Wuhan
// (tools/preview/sample_response.json, tools/preview/sample_air_quality.json).
void testRecordedForecast(const std::string& json) {
  std::printf("recorded forecast response (%zu bytes)\n", json.size());
  weather::WeatherReport report;
  const bool ok = weather::parseForecast(json.c_str(), json.size(), report);
  check(ok, "parses");
  if (!ok) return;

  check(report.ok && report.current.valid, "report marked valid");
  checkString(report.current.time, "2026-10-12T18:45", "current observation time");
  checkFloat(report.current.temperatureC, 26.6f, "current temperature");
  checkFloat(report.current.apparentC, 28.0f, "apparent temperature");
  checkFloat(report.current.windKph, 5.6f, "wind speed");
  check(report.current.humidity == 54, "humidity 54%");
  check(report.current.code == 3, "WMO code 3 (overcast)");
  check(!report.current.isDay, "is_day == 0 -> night");
  checkString(weather::conditionText(report.current.code), "阴", "condition text");
  check(weather::conditionIcon(report.current.code, true) == weather::Icon::Cloudy, "overcast icon");

  check(report.dayCount == weather::kForecastDays, "three forecast days");
  const char* expectedDates[weather::kForecastDays] = {"2026-10-12", "2026-10-13", "2026-10-14"};
  for (uint8_t i = 0; i < weather::kForecastDays; ++i) {
    check(report.days[i].valid, std::string("day ") + std::to_string(i) + " valid");
    checkString(report.days[i].date, expectedDates[i], std::string("day ") + std::to_string(i) + " date");
  }
  check(report.days[0].code == 3 && report.days[1].code == 3 && report.days[2].code == 51,
        "daily weather codes 3 / 3 / 51");
  checkFloat(report.days[0].tMax, 29.7f, "day 0 max");
  checkFloat(report.days[0].tMin, 20.6f, "day 0 min");
  checkFloat(report.days[2].tMin, 17.9f, "day 2 min");

  check(report.hourCount == weather::kForecastHours, "twelve hourly entries");
  checkString(report.hours[0].time, "2026-10-12T18:00", "hour 0 timestamp");
  checkFloat(report.hours[0].temperatureC, 27.7f, "hour 0 temperature");
  checkFloat(report.hours[1].temperatureC, 26.3f, "hour 1 temperature");
  check(report.hours[0].code == 2, "hour 0 WMO code");
  check(report.hours[0].precipitationChance == 0, "hour 0 precipitation chance");
  check(report.hours[11].valid, "hour 11 valid");
  checkFloat(report.hours[11].temperatureC, 21.5f, "hour 11 temperature");

  char buffer[32];
  checkString(weather::formatHourLabel(report.hours[0].time, buffer, sizeof(buffer)), "18:00", "hour label");
  checkString(weather::formatHourLabel("bogus", buffer, sizeof(buffer)), "", "bad hour -> empty");

  // Derived UI values.
  check(weather::weekdayFromIsoDate("2026-10-12") == 1, "2026-10-12 is a Monday");
  check(weather::weekdayFromIsoDate("2026-10-18") == 0, "2026-10-18 is a Sunday");
  check(weather::weekdayFromIsoDate("2026-13-01") == -1, "bad month rejected");
  checkString(weather::formatTemp1(19.94f, buffer, sizeof(buffer)), "19.9", "temperature rounding");
  checkString(weather::formatTemp1(-3.45f, buffer, sizeof(buffer)), "-3.5", "negative rounding");
  checkString(weather::formatTemp1(0.04f, buffer, sizeof(buffer)), "0.0", "zero-ish rounding");
  checkString(weather::formatShortDate("2026-10-12", buffer, sizeof(buffer)), "10-12", "short date");
  checkString(weather::formatShortDate("bogus", buffer, sizeof(buffer)), "", "bad date -> empty");
}

void testRecordedAirQuality(const std::string& json) {
  std::printf("recorded air-quality response (%zu bytes)\n", json.size());
  weather::AirQuality air;
  const bool ok = weather::parseAirQuality(json.c_str(), json.size(), air);
  check(ok, "parses");
  if (!ok) return;

  check(air.valid, "marked valid");
  checkString(air.time, "2026-10-12T18:00", "observation time");
  check(air.europeanAqi == 80, "EAQI 80");
  checkFloat(air.pm25, 75.4f, "PM2.5");
  checkFloat(air.pm10, 76.4f, "PM10");
  checkFloat(air.no2, 100.9f, "NO2");
  checkFloat(air.ozone, 81.0f, "O3");

  // Bands are upper-bound inclusive: 0-20 优, 20-40 良, 40-60 轻度, 60-80 中度,
  // 80-100 重度, above 100 严重.
  check(weather::aqiLevelIndex(80) == 3, "EAQI 80 -> level 3 (upper bound inclusive)");
  checkString(weather::aqiLevelText(80), "中度污染", "EAQI 80 text");
  check(weather::aqiLevelIndex(81) == 4, "EAQI 81 -> level 4");
  checkString(weather::aqiLevelText(81), "重度污染", "EAQI 81 text");
  checkString(weather::aqiLevelText(10), "优", "EAQI 10 text");
  checkString(weather::aqiLevelText(35), "良", "EAQI 35 text");
  checkString(weather::aqiLevelText(50), "轻度污染", "EAQI 50 text");
  checkString(weather::aqiLevelText(70), "中度污染", "EAQI 70 text");
  checkString(weather::aqiLevelText(180), "严重污染", "EAQI 180 text");
  check(weather::aqiLevelIndex(0) == 0 && weather::aqiLevelIndex(21) == 1, "band edges");
}

void testRecordedCrypto(const std::string& json) {
  std::printf("recorded crypto response (%zu bytes)\n", json.size());
  weather::CryptoBoard board;
  const bool ok = weather::parseCrypto(json.c_str(), json.size(), board);
  check(ok, "parses");
  if (!ok) return;

  check(board.ok, "marked ok");
  check(board.coins[0].valid && board.coins[6].valid, "first and last coin valid");
  checkFloat(board.coins[0].usd, 83342.0f, "BTC price");
  checkFloat(board.coins[0].change24h, -0.76f, "BTC 24 h change");
  checkFloat(board.coins[1].usd, 2513.24f, "ETH price");
  checkFloat(board.coins[3].usd, 744.26f, "BNB price");
  checkFloat(board.coins[5].usd, 0.329749f, "TRX price");
  checkFloat(board.coins[6].usd, 531.46f, "XMR price");

  // The 168-point week is downsampled and stored relative to the first point.
  check(board.coins[0].sparkCount == weather::kSparklinePoints, "BTC sparkline downsampled to 48 points");
  checkFloat(board.coins[0].sparkRef, 85500.26f, "BTC sparkline reference");
  check(board.coins[0].spark[0] == 0, "first sparkline point is the reference");
  checkFloat(board.coins[0].sparkMin / 100.0f, -5.67f, "BTC week low (percent)");
  checkFloat(board.coins[0].sparkMax / 100.0f, 1.28f, "BTC week high (percent)");
  check(board.coins[6].sparkCount == weather::kSparklinePoints, "XMR sparkline present");
  checkString(weather::cryptoSortName(weather::CryptoSort::Table), "表序", "sort name: table");
  checkString(weather::cryptoSortName(weather::CryptoSort::Gainers), "涨幅榜", "sort name: gainers");
  checkString(weather::cryptoSortName(weather::CryptoSort::Losers), "跌幅榜", "sort name: losers");

  char buffer[32];
  checkString(weather::formatUsd(83393.0f, buffer, sizeof(buffer)), "$83,393", "usd above 10k");
  checkString(weather::formatUsd(83342.0f, buffer, sizeof(buffer)), "$83,342", "usd from the fixture");
  checkString(weather::formatUsd(2514.03f, buffer, sizeof(buffer)), "$2,514.03", "usd below 10k");
  checkString(weather::formatUsd(1.37f, buffer, sizeof(buffer)), "$1.37", "usd below 10");
  checkString(weather::formatUsd(0.329837f, buffer, sizeof(buffer)), "$0.3298", "usd below 1");
  checkString(weather::formatUsd(1234567.0f, buffer, sizeof(buffer)), "$1,234,567", "two separators");
  checkString(weather::formatPercent(-3.628f, buffer, sizeof(buffer)), "-3.6%", "negative percent");
  checkString(weather::formatPercent(2.44f, buffer, sizeof(buffer)), "+2.4%", "positive percent");
  checkString(weather::formatPercent(0.01f, buffer, sizeof(buffer)), "+0.0%", "flat percent");
}

void testRecordedGeocode(const std::string& json) {
  std::printf("recorded geocoding response (%zu bytes)\n", json.size());
  weather::GeoLocation geo;
  const bool ok = weather::parseGeocode(json.c_str(), json.size(), geo);
  check(ok, "parses");
  if (!ok) return;

  check(geo.valid, "marked valid");
  checkString(geo.name, "Wuhan", "name");
  checkString(geo.country, "China", "country");
  checkString(geo.timezone, "Asia/Shanghai", "timezone");
  checkFloat(geo.latitude, 30.58333f, "latitude");
  checkFloat(geo.longitude, 114.26667f, "longitude");
}

// The firmware feeds the ~28 KB markets payload to the SAX parser in 512-byte
// socket chunks, so the streaming path must agree with the one-shot parse.
void testCryptoStreaming(const std::string& json) {
  std::printf("crypto streaming (%zu bytes in 512-byte chunks)\n", json.size());
  weather::CryptoBoard streamed;
  {
    weather::CryptoStreamParser sink(streamed);
    const size_t chunk = 512;
    for (size_t offset = 0; offset < json.size(); offset += chunk) {
      const size_t remaining = json.size() - offset;
      const size_t length = remaining < chunk ? remaining : chunk;
      sink.feed(json.data() + offset, length);
    }
    check(sink.finish(), "chunked feed finishes cleanly");
  }
  check(streamed.ok == false, "board is only marked ok by parseCrypto()");  // sink fills coins only
  check(streamed.coins[0].valid && streamed.coins[6].valid, "chunked: first and last coin valid");
  checkFloat(streamed.coins[0].usd, 83342.0f, "chunked: BTC price");
  checkFloat(streamed.coins[0].change24h, -0.76f, "chunked: BTC 24 h change");
  check(streamed.coins[0].sparkCount == weather::kSparklinePoints, "chunked: sparkline points");
  check(streamed.coins[0].spark[0] == 0, "chunked: first point is the reference");
  check(streamed.coins[6].sparkCount == weather::kSparklinePoints, "chunked: last coin sparkline");
}

void testLongArraysAreCapped() {
  std::printf("longer-than-layout arrays\n");
  const char* json =
      "{\"current\":{\"time\":\"2026-01-01T00:00\",\"temperature_2m\":-3.4,\"is_day\":0,\"weather_code\":3},"
      "\"hourly\":{\"time\":[\"2026-01-01T00:00\",\"2026-01-01T01:00\",\"2026-01-01T02:00\",\"2026-01-01T03:00\","
      "\"2026-01-01T04:00\",\"2026-01-01T05:00\",\"2026-01-01T06:00\",\"2026-01-01T07:00\",\"2026-01-01T08:00\","
      "\"2026-01-01T09:00\",\"2026-01-01T10:00\",\"2026-01-01T11:00\",\"2026-01-01T12:00\"],"
      "\"temperature_2m\":[0,1,2,3,4,5,6,7,8,9,10,11,12],\"weather_code\":[0,0,1,1,2,2,3,3,45,45,51,51,61],"
      "\"precipitation_probability\":[0,0,0,0,0,0,0,0,0,10,20,30,40]},"
      "\"daily\":{\"time\":[\"2026-01-01\",\"2026-01-02\",\"2026-01-03\",\"2026-01-04\",\"2026-01-05\"],"
      "\"weather_code\":[3,71,0,2,1],\"temperature_2m_max\":[1,2,3,4,5],\"temperature_2m_min\":[-9,-8,-7,-6,-5]}}";
  weather::WeatherReport report;
  const bool ok = weather::parseForecast(json, std::strlen(json), report);
  check(ok, "parses");
  if (!ok) return;
  check(report.dayCount == weather::kForecastDays, "dayCount capped at 3");
  check(report.hourCount == weather::kForecastHours, "hourCount capped at 12");
  check(!report.current.isDay, "is_day == 0 -> night");
  checkFloat(report.current.temperatureC, -3.4f, "negative temperature");
  checkString(report.days[2].date, "2026-01-03", "third day date");
  checkFloat(report.days[2].tMax, 3.0f, "third day max");
  checkString(report.hours[11].time, "2026-01-01T11:00", "twelfth hour kept, thirteenth dropped");
  checkFloat(report.hours[11].temperatureC, 11.0f, "twelfth hour temperature");
  check(report.hours[11].precipitationChance == 30, "twelfth hour precipitation chance");
  check(weather::conditionIcon(0, false) == weather::Icon::ClearNight, "clear night icon");
  check(weather::conditionIcon(0, true) == weather::Icon::ClearDay, "clear day icon");
}

void testRejectsBrokenPayloads() {
  std::printf("malformed payloads\n");
  weather::WeatherReport report;
  weather::AirQuality air;
  // Sentinels: a failed parse must leave the caller's data untouched.
  report.current.temperatureC = 12.5f;
  air.europeanAqi = 42;

  const char* truncated = "{\"current\":{\"temperature_2m\":19.";
  check(!weather::parseForecast(truncated, std::strlen(truncated), report), "truncated response rejected");
  const char* noCurrent = "{\"daily\":{\"time\":[\"2026-01-01\"]}}";
  check(!weather::parseForecast(noCurrent, std::strlen(noCurrent), report), "response without `current` rejected");
  check(!weather::parseForecast(nullptr, 0, report), "null payload rejected");
  check(!weather::parseForecast("", 0, report), "empty payload rejected");
  checkFloat(report.current.temperatureC, 12.5f, "failed forecast left the report untouched");

  weather::CryptoBoard board;
  weather::GeoLocation geo;
  // One coin out of seven is below the "half the table" floor: reject it rather
  // than render a page that looks like the market vanished.
  const char* partialCrypto = "[{\"id\":\"bitcoin\",\"current_price\":83393,\"sparkline_in_7d\":{\"price\":[1,2]}}]";
  check(!weather::parseCrypto(partialCrypto, std::strlen(partialCrypto), board), "partial crypto rejected");
  const char* brokenCrypto = "[{\"id\":\"bitcoin\",\"current_price\":83";
  check(!weather::parseCrypto(brokenCrypto, std::strlen(brokenCrypto), board), "truncated crypto rejected");
  const char* emptyResults = "{\"results\":[]}";
  check(!weather::parseGeocode(emptyResults, std::strlen(emptyResults), geo), "empty geocoding results rejected");
  const char* noCoords = "{\"results\":[{\"name\":\"Nowhere\"}]}";
  check(!weather::parseGeocode(noCoords, std::strlen(noCoords), geo), "geocoding result without coords rejected");

  const char* airNoCurrent = "{\"current_units\":{\"european_aqi\":\"EAQI\"}}";
  check(!weather::parseAirQuality(airNoCurrent, std::strlen(airNoCurrent), air), "air response without data rejected");
  const char* airTruncated = "{\"current\":{\"european_aqi\":8";
  check(!weather::parseAirQuality(airTruncated, std::strlen(airTruncated), air), "truncated air response rejected");
  check(!weather::parseAirQuality(nullptr, 0, air), "null air payload rejected");
  check(air.europeanAqi == 42, "failed air parse left the struct untouched");
}

}  // namespace

int main(int argc, char** argv) {
  const char* forecastPath = argc > 1 ? argv[1] : "tools/preview/sample_response.json";
  const char* airPath = argc > 2 ? argv[2] : "tools/preview/sample_air_quality.json";
  const char* cryptoPath = argc > 3 ? argv[3] : "tools/preview/sample_crypto.json";
  const char* geocodePath = argc > 4 ? argv[4] : "tools/preview/sample_geocode.json";
  bool ok = false;
  const std::string forecast = readFile(forecastPath, ok);
  if (!ok) {
    std::printf("error: cannot read %s (run from the project root)\n", forecastPath);
    return 2;
  }
  const std::string air = readFile(airPath, ok);
  if (!ok) {
    std::printf("error: cannot read %s (run from the project root)\n", airPath);
    return 2;
  }
  const std::string crypto = readFile(cryptoPath, ok);
  if (!ok) {
    std::printf("error: cannot read %s (run from the project root)\n", cryptoPath);
    return 2;
  }
  const std::string geocode = readFile(geocodePath, ok);
  if (!ok) {
    std::printf("error: cannot read %s (run from the project root)\n", geocodePath);
    return 2;
  }

  testRecordedForecast(forecast);
  testRecordedAirQuality(air);
  testRecordedCrypto(crypto);
  testCryptoStreaming(crypto);
  testRecordedGeocode(geocode);
  testLongArraysAreCapped();
  testRejectsBrokenPayloads();

  if (gFailures == 0) {
    std::printf("all parser tests passed\n");
    return 0;
  }
  std::printf("%d check(s) FAILED\n", gFailures);
  return 1;
}
