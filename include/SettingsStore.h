#pragma once

#include <stdint.h>

namespace weather {

// User-editable device settings.
//
// The compile-time values in config.h (and secrets.h, when present) are the
// factory defaults; SettingsStore::load() overlays whatever the user saved in
// NVS on top of them, so a fresh board behaves exactly like the old
// compile-time-only firmware and a configured one survives reflashing.
struct Settings {
  char wifiSsid[33] = {0};
  char wifiPassword[65] = {0};
  char city[40] = {0};   // geocoding search term ("Wuhan", "武汉")
  char place[32] = {0};  // header label; empty = use the geocoded name
  float latitude = 0.0f;  // fallback when geocoding fails
  float longitude = 0.0f;
  uint16_t refreshMinutes = 30;
  uint16_t retryMinutes = 5;
  uint8_t defaultPage = 0;
  uint8_t cryptoSort = 0;
  uint8_t coinMask = 0x7F;  // bit i = draw coin i (the seven-coin default)
};

// NVS-backed store for the struct above. Writes are explicit: nothing here
// touches flash unless save() is called.
class SettingsStore {
 public:
  static Settings load();
  static bool save(const Settings& settings);
  // Forgets everything (a factory reset / "re-provision" entry point).
  static void clear();
};

}  // namespace weather
