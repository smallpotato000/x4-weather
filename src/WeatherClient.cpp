#include "WeatherClient.h"

#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <time.h>

#include <cstdio>

#include "WeatherParse.h"

namespace weather {
namespace {

// Sanity floor for "the clock is set": 2020-09-13.
constexpr uint32_t kEpochFloor = 1600000000u;

// Percent-encode a UTF-8 string for a query parameter (city names may be
// non-ASCII; the geocoding API expects them encoded).
String urlEncode(const char* text) {
  static const char* kHex = "0123456789ABCDEF";
  String encoded;
  if (text == nullptr) return encoded;
  for (const uint8_t* cursor = reinterpret_cast<const uint8_t*>(text); *cursor != 0; ++cursor) {
    const uint8_t c = *cursor;
    const bool unreserved = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                            c == '-' || c == '_' || c == '.' || c == '~';
    if (unreserved) {
      encoded += static_cast<char>(c);
    } else if (c == ' ') {
      encoded += '+';
    } else {
      encoded += '%';
      encoded += kHex[c >> 4];
      encoded += kHex[c & 0x0F];
    }
  }
  return encoded;
}

}  // namespace

uint32_t WeatherClient::epochNow() const {
  const time_t now = time(nullptr);
  return now >= static_cast<time_t>(kEpochFloor) ? static_cast<uint32_t>(now) : 0;
}

bool WeatherClient::wifiConnected() const { return WiFi.status() == WL_CONNECTED; }

bool WeatherClient::beginWifi() {
  lastError_ = "";
  if (WiFi.status() == WL_CONNECTED) return true;

  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setHostname("x4-weather");
  WiFi.begin(config_.ssid, config_.password);

  const uint32_t deadline = millis() + config_.wifiTimeoutMs;
  while (WiFi.status() != WL_CONNECTED && static_cast<int32_t>(millis() - deadline) < 0) {
    delay(200);
  }
  if (WiFi.status() != WL_CONNECTED) {
    lastError_ = "WiFi connect failed";
    return false;
  }

  configTzTime(config_.timezone, config_.ntp1, config_.ntp2, config_.ntp3);
  const uint32_t clockDeadline = millis() + 8000;
  while (epochNow() == 0 && static_cast<int32_t>(millis() - clockDeadline) < 0) {
    delay(100);
  }
  return true;
}

void WeatherClient::endWifi() {
  // Idempotent: the firmware calls this after every fetch and again on the way
  // into deep sleep, and WiFi.disconnect() logs an error once the stack is
  // already down.
  if (WiFi.getMode() == WIFI_MODE_NULL) return;
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
}

bool WeatherClient::httpGet(const String& url, String& payload) {
  WiFiClientSecure secure;
  // Certificate validation is off: the ESP32 has no trustworthy wall clock
  // before SNTP and this build carries no CA bundle. The payload is public
  // weather data; see the README for pinning a CA with secure.setCACert().
  secure.setInsecure();

  HTTPClient http;
  http.setConnectTimeout(config_.httpTimeoutMs);
  http.setTimeout(config_.httpTimeoutMs);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  if (!http.begin(secure, url)) {
    lastError_ = "http.begin failed";
    return false;
  }

  const int status = http.GET();
  if (status != HTTP_CODE_OK) {
    lastError_ = status > 0 ? "HTTP error" : "HTTP connection failed";
    http.end();
    return false;
  }

  payload = http.getString();
  http.end();
  if (payload.isEmpty()) {
    lastError_ = "empty response";
    return false;
  }
  return true;
}

bool WeatherClient::fetch(WeatherReport& out) {
  lastError_ = "";
  if (WiFi.status() != WL_CONNECTED) {
    lastError_ = "WiFi not connected";
    return false;
  }

  String url = "https://api.open-meteo.com/v1/forecast?latitude=";
  url += String(config_.latitude, 4);
  url += "&longitude=";
  url += String(config_.longitude, 4);
  url += "&current=temperature_2m,relative_humidity_2m,apparent_temperature,is_day,weather_code,wind_speed_10m";
  url += "&hourly=temperature_2m,weather_code,precipitation_probability";
  url += "&daily=weather_code,temperature_2m_max,temperature_2m_min";
  url += "&timezone=auto&forecast_days=";
  url += String(config_.forecastDays);
  url += "&forecast_hours=";
  url += String(config_.forecastHours);

  String payload;
  if (!httpGet(url, payload)) return false;

  WeatherReport parsed;
  std::snprintf(parsed.place, sizeof(parsed.place), "%s", config_.place);
  if (!parseForecast(payload.c_str(), payload.length(), parsed)) {
    lastError_ = "unexpected response";
    return false;
  }

  parsed.fetchedEpoch = epochNow();
  out = parsed;
  return true;
}

bool WeatherClient::fetchAirQuality(AirQuality& out) {
  lastError_ = "";
  if (WiFi.status() != WL_CONNECTED) {
    lastError_ = "WiFi not connected";
    return false;
  }

  String url = "https://air-quality-api.open-meteo.com/v1/air-quality?latitude=";
  url += String(config_.latitude, 4);
  url += "&longitude=";
  url += String(config_.longitude, 4);
  url += "&current=european_aqi,pm2_5,pm10,nitrogen_dioxide,ozone&timezone=auto";

  String payload;
  if (!httpGet(url, payload)) return false;

  AirQuality parsed;
  if (!parseAirQuality(payload.c_str(), payload.length(), parsed)) {
    lastError_ = "unexpected air-quality response";
    return false;
  }
  out = parsed;
  return true;
}

// Sources are chosen for reachability without a proxy from mainland China
// networks (verified from the device itself): CoinCodex and CoinGecko answer 403
// / time out there, while CoinLore and Gate.io work.
bool WeatherClient::fetchCrypto(CryptoBoard& out) {
  lastError_ = "";
  if (WiFi.status() != WL_CONNECTED) {
    lastError_ = "WiFi not connected";
    return false;
  }

  WiFiClientSecure secure;
  secure.setInsecure();

  HTTPClient http;
  http.setConnectTimeout(config_.httpTimeoutMs);
  http.setTimeout(config_.httpTimeoutMs);
  http.setUserAgent("x4-weather/1.0");
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

  // 1) One request for every quote (price + 24 h change, including XMR).
  String quotesUrl = "https://api.coinlore.net/api/ticker/?id=";
  for (uint8_t i = 0; i < kCoinCount; ++i) {
    if (i > 0) quotesUrl += ',';
    quotesUrl += coinInfo(i).apiId;
  }

  CryptoBoard board;
  uint8_t quotes = 0;
  int quotesStatus = 0;
  if (http.begin(secure, quotesUrl)) {
    quotesStatus = http.GET();
    if (quotesStatus == HTTP_CODE_OK) {
      const String payload = http.getString();
      if (parseCoinQuotes(payload.c_str(), payload.length(), board)) {
        for (uint8_t i = 0; i < kCoinCount; ++i) {
          if (board.coins[i].valid) ++quotes;
        }
      }
    }
    http.end();
  }
  Serial.printf("[weather] crypto quotes HTTP %d: %u/%u coins\n", quotesStatus,
                static_cast<unsigned>(quotes), static_cast<unsigned>(kCoinCount));
  if (quotes * 2 < kCoinCount) {
    lastError_ = "quote request failed";
    return false;
  }

  // 2) One request per coin for the 7-day sparkline (best effort: a missing line
  // is better than no page). XMR has no reachable source, see the coin table.
  uint8_t lines = 0;
  for (uint8_t i = 0; i < kCoinCount; ++i) {
    const char* pair = coinInfo(i).pair;
    if (pair == nullptr) continue;

    String url = "https://api.gateio.ws/api/v4/spot/candlesticks?interval=4h&limit=42&currency_pair=";
    url += pair;
    if (!http.begin(secure, url)) continue;
    if (http.GET() == HTTP_CODE_OK) {
      const String payload = http.getString();
      if (parseCoinCandles(coinInfo(i).symbol, payload.c_str(), payload.length(), board)) ++lines;
    }
    http.end();
  }
  Serial.printf("[weather] crypto sparklines: %u\n", static_cast<unsigned>(lines));

  board.ok = true;
  board.fetchedEpoch = epochNow();
  out = board;
  return true;
}

bool WeatherClient::fetchGeoCandidates(const char* city, GeoCandidates& out) {
  lastError_ = "";
  if (WiFi.status() != WL_CONNECTED) {
    lastError_ = "WiFi not connected";
    return false;
  }

  String url = "https://geocoding-api.open-meteo.com/v1/search?count=5&language=zh&format=json&name=";
  url += urlEncode(city);

  String payload;
  if (!httpGet(url, payload)) return false;

  GeoCandidates parsed;
  if (!parseGeoCandidates(payload.c_str(), payload.length(), parsed)) {
    lastError_ = "city not found";
    return false;
  }
  out = parsed;
  return true;
}

bool WeatherClient::fetchGeocode(const char* city, GeoLocation& out) {
  lastError_ = "";
  if (WiFi.status() != WL_CONNECTED) {
    lastError_ = "WiFi not connected";
    return false;
  }

  String url = "https://geocoding-api.open-meteo.com/v1/search?count=1&format=json&name=";
  url += urlEncode(city);

  String payload;
  if (!httpGet(url, payload)) return false;

  GeoLocation parsed;
  if (!parseGeocode(payload.c_str(), payload.length(), parsed)) {
    lastError_ = "city not found";
    return false;
  }
  out = parsed;
  return true;
}

}  // namespace weather
