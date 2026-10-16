#pragma once

#include <stdint.h>

namespace weather {

// A built-in city: the label the UI shows, the name geocoding would search for,
// and the coordinates, so picking one of these needs no network at all.
struct BuiltinCity {
  const char* label;       // "武汉"
  const char* searchName;  // "Wuhan"
  float latitude;
  float longitude;
};

static constexpr uint8_t kBuiltinCityCount = 7;

const BuiltinCity& builtinCity(uint8_t index);

// Index of the built-in city whose label or search name matches, or -1. Used to
// tick the current selection in the picker.
int builtinCityIndexFor(const char* name);

}  // namespace weather
