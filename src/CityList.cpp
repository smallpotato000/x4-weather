#include "CityList.h"

#include <cstring>

namespace weather {
namespace {

// The short list the settings picker offers first: the cities this device is
// most likely to be asked about, with coordinates resolved once through the
// geocoding API and baked in so the picker works before WiFi is configured.
constexpr BuiltinCity kCities[kBuiltinCityCount] = {
    {"武汉", "Wuhan", 30.5833f, 114.2667f},        // 湖北 · 中国
    {"上海", "Shanghai", 31.2222f, 121.4581f},     // 上海市 · 中国
    {"广州", "Guangzhou", 23.1167f, 113.2500f},    // 广东 · 中国
    {"深圳", "Shenzhen", 22.5455f, 114.0683f},     // 广东 · 中国
    {"香港", "Hong Kong", 22.2783f, 114.1747f},    // Asia/Hong_Kong (same UTC+8)
    {"成都", "Chengdu", 30.6667f, 104.0667f},      // 四川 · 中国
    {"重庆", "Chongqing", 29.5603f, 106.5577f},    // 重庆市 · 中国
};

}  // namespace

const BuiltinCity& builtinCity(uint8_t index) {
  return kCities[index < kBuiltinCityCount ? index : 0];
}

int builtinCityIndexFor(const char* name) {
  if (name == nullptr || name[0] == '\0') return -1;
  for (uint8_t i = 0; i < kBuiltinCityCount; ++i) {
    if (std::strcmp(kCities[i].label, name) == 0 || std::strcmp(kCities[i].searchName, name) == 0) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

}  // namespace weather
