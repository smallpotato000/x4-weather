#pragma once

// Touch settings session for the PaperS3: hit testing, WiFi scan/connect, city
// picking and NVS writes. The drawing lives in SettingsUi, the device-side bits
// that only main.cpp can do (RTC caches, power-off) come in through hooks, so
// this file stays about the flow.

#if FREEINK_DEVICE_PAPERS3

#include <EInkDisplay.h>  // EInkDisplay is an alias for freeink::FreeInkDisplay
#include <FreeInkUIDisplayTarget.h>
#include <InputManager.h>
#include <stdint.h>

#include "SettingsStore.h"
#include "SettingsUi.h"

namespace weather {

class WeatherClient;

struct SettingsFlowHooks {
  void (*applyLocation)() = nullptr;        // point the client at settings lat/lon
  void (*forgetLocationCache)() = nullptr;  // drop the RTC-geocoded coordinates
  void (*powerOff)() = nullptr;             // does not return
};

struct SettingsFlowContext {
  InputManager* input = nullptr;
  EInkDisplay* display = nullptr;
  freeink::ui::DisplayTarget* target = nullptr;
  WeatherClient* client = nullptr;
  Settings* settings = nullptr;
  WifiList* networks = nullptr;
  bool batteryKnown = true;
  uint8_t batteryPercent = 82;
  SettingsFlowHooks hooks;
};

// Runs the settings UI until the user leaves it or it idles out. On return the
// caller should repaint its own screens.
void runSettingsSession(SettingsFlowContext& context);

}  // namespace weather

#endif  // FREEINK_DEVICE_PAPERS3
