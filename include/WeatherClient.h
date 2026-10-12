#pragma once

// WiFi + Open-Meteo client for the X4 weather firmware.
//
// Two requests, both parsed straight off the socket with the SDK's
// allocation-free StreamingJsonParser (JsonSax) — no ArduinoJson, no DOM:
//   * api.open-meteo.com     — current conditions + hourly + 3-day forecast
//   * air-quality-api.open-meteo.com — European AQI + the main pollutants,
//     fetched only when the air-quality page is actually being shown.

#include <Arduino.h>

#include "WeatherData.h"

namespace weather {

struct ClientConfig {
  const char* ssid = "";
  const char* password = "";
  const char* place = "";  // shown in the header; ASCII unless the font covers it
  float latitude = 0.0f;
  float longitude = 0.0f;
  uint8_t forecastDays = kForecastDays;
  uint8_t forecastHours = kForecastHours;
  const char* timezone = "UTC0";  // POSIX TZ installed for the local clock
  const char* ntp1 = "pool.ntp.org";
  const char* ntp2 = "time.cloudflare.com";
  const char* ntp3 = "";
  uint16_t wifiTimeoutMs = 25000;
  uint16_t httpTimeoutMs = 15000;
};

class WeatherClient {
 public:
  explicit WeatherClient(const ClientConfig& config) : config_(config) {}

  // Join the configured AP and start SNTP. Blocking; returns true on success.
  bool beginWifi();
  // Drop the link and power the radio down (called between refreshes).
  void endWifi();
  bool wifiConnected() const;

  // Fetch and parse the forecast. `out` is only modified on success, so a
  // failed refresh leaves the last good report on screen.
  bool fetch(WeatherReport& out);

  // Fetch and parse the air-quality `current` block. Requires WiFi to be up.
  bool fetchAirQuality(AirQuality& out);

  // Seconds since the epoch once SNTP has set the clock; 0 means "not set".
  uint32_t epochNow() const;

  const char* lastError() const { return lastError_; }

 private:
  // Shared HTTPS GET: returns the body in `payload` on HTTP 200.
  bool httpGet(const String& url, String& payload);

  ClientConfig config_;
  const char* lastError_ = "";
};

}  // namespace weather
