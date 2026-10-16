#include "SettingsStore.h"

#include <Preferences.h>

#include <cstdio>
#include <cstring>

#include "config.h"

namespace weather {
namespace {

constexpr const char* kNamespace = "x4weather";

// Keys are short because NVS caps names at 15 characters.
constexpr const char* kKeySsid = "ssid";
constexpr const char* kKeyPassword = "pass";
constexpr const char* kKeyCity = "city";
constexpr const char* kKeyPlace = "place";
constexpr const char* kKeyLatitude = "lat";
constexpr const char* kKeyLongitude = "lon";
constexpr const char* kKeyRefresh = "refresh";
constexpr const char* kKeyRetry = "retry";
constexpr const char* kKeyDefaultPage = "page";
constexpr const char* kKeyCryptosort = "csort";
constexpr const char* kKeyCoinMask = "coins";

void copyText(char* destination, size_t capacity, const char* source) {
  if (source == nullptr) {
    destination[0] = '\0';
    return;
  }
  std::snprintf(destination, capacity, "%s", source);
}

// Factory defaults: exactly what the firmware used before settings existed.
Settings factoryDefaults() {
  Settings settings;
  copyText(settings.wifiSsid, sizeof(settings.wifiSsid), WIFI_SSID);
  copyText(settings.wifiPassword, sizeof(settings.wifiPassword), WIFI_PASSWORD);
  copyText(settings.city, sizeof(settings.city), WEATHER_CITY);
  copyText(settings.place, sizeof(settings.place), WEATHER_PLACE);
  settings.latitude = WEATHER_LATITUDE;
  settings.longitude = WEATHER_LONGITUDE;
  settings.refreshMinutes = WEATHER_REFRESH_MINUTES;
  settings.retryMinutes = WEATHER_RETRY_MINUTES;
  settings.defaultPage = WEATHER_DEFAULT_PAGE;
  return settings;
}

// A saved string is only used when it is non-empty: an empty NVS value means
// "not configured", which must fall back to the factory default rather than
// wiping it.
void loadText(Preferences& prefs, const char* key, char* destination, size_t capacity) {
  const String value = prefs.getString(key, "");
  if (value.length() > 0) {
    std::snprintf(destination, capacity, "%s", value.c_str());
  }
}

}  // namespace

Settings SettingsStore::load() {
  Settings settings = factoryDefaults();

  // Read-write: opening the namespace read-only on a fresh device logs
  // "nvs_open failed: NOT_FOUND", and creating the (empty) namespace is harmless.
  Preferences prefs;
  if (!prefs.begin(kNamespace, /*readOnly=*/false)) {
    return settings;
  }
  loadText(prefs, kKeySsid, settings.wifiSsid, sizeof(settings.wifiSsid));
  loadText(prefs, kKeyPassword, settings.wifiPassword, sizeof(settings.wifiPassword));
  loadText(prefs, kKeyCity, settings.city, sizeof(settings.city));
  loadText(prefs, kKeyPlace, settings.place, sizeof(settings.place));
  settings.latitude = prefs.getFloat(kKeyLatitude, settings.latitude);
  settings.longitude = prefs.getFloat(kKeyLongitude, settings.longitude);
  settings.refreshMinutes =
      static_cast<uint16_t>(prefs.getUShort(kKeyRefresh, settings.refreshMinutes));
  settings.retryMinutes = static_cast<uint16_t>(prefs.getUShort(kKeyRetry, settings.retryMinutes));
  settings.defaultPage =
      static_cast<uint8_t>(prefs.getUChar(kKeyDefaultPage, settings.defaultPage));
  settings.cryptoSort = static_cast<uint8_t>(prefs.getUChar(kKeyCryptosort, settings.cryptoSort));
  settings.coinMask = static_cast<uint8_t>(prefs.getUChar(kKeyCoinMask, settings.coinMask));
  prefs.end();

  // Sanity: a corrupt or half-written record must not leave the device unable to
  // sleep sensibly or draw a page.
  if (settings.refreshMinutes == 0) settings.refreshMinutes = WEATHER_REFRESH_MINUTES;
  if (settings.retryMinutes == 0) settings.retryMinutes = WEATHER_RETRY_MINUTES;
  return settings;
}

bool SettingsStore::save(const Settings& settings) {
  Preferences prefs;
  if (!prefs.begin(kNamespace, /*readOnly=*/false)) return false;

  prefs.putString(kKeySsid, settings.wifiSsid);
  prefs.putString(kKeyPassword, settings.wifiPassword);
  prefs.putString(kKeyCity, settings.city);
  prefs.putString(kKeyPlace, settings.place);
  prefs.putFloat(kKeyLatitude, settings.latitude);
  prefs.putFloat(kKeyLongitude, settings.longitude);
  prefs.putUShort(kKeyRefresh, settings.refreshMinutes);
  prefs.putUShort(kKeyRetry, settings.retryMinutes);
  prefs.putUChar(kKeyDefaultPage, settings.defaultPage);
  prefs.putUChar(kKeyCryptosort, settings.cryptoSort);
  prefs.putUChar(kKeyCoinMask, settings.coinMask);
  prefs.end();
  return true;
}

void SettingsStore::clear() {
  Preferences prefs;
  if (!prefs.begin(kNamespace, /*readOnly=*/false)) return;
  prefs.clear();
  prefs.end();
}

}  // namespace weather
