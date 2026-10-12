#include "WeatherData.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

namespace weather {

// WMO 4677 weather codes as used by Open-Meteo's `weather_code`.
const char* conditionText(uint8_t code) {
  switch (code) {
    case 0: return "晴";
    case 1: return "少云";
    case 2: return "多云";
    case 3: return "阴";
    case 45: return "雾";
    case 48: return "冻雾";
    case 51: return "毛毛雨";
    case 53: return "小雨";
    case 55: return "中雨";
    case 56: return "冻雨";
    case 57: return "冻雨";
    case 61: return "小雨";
    case 63: return "中雨";
    case 65: return "大雨";
    case 66: return "冻雨";
    case 67: return "冻雨";
    case 71: return "小雪";
    case 73: return "中雪";
    case 75: return "大雪";
    case 77: return "雪粒";
    case 80: return "阵雨";
    case 81: return "强阵雨";
    case 82: return "暴雨";
    case 85: return "阵雪";
    case 86: return "强阵雪";
    case 95: return "雷阵雨";
    case 96: return "雷暴冰雹";
    case 99: return "雷暴冰雹";
    default: return "未知";
  }
}

Icon conditionIcon(uint8_t code, bool isDay) {
  switch (code) {
    case 0: return isDay ? Icon::ClearDay : Icon::ClearNight;
    case 1:
    case 2: return isDay ? Icon::PartlyDay : Icon::PartlyNight;
    case 3: return Icon::Cloudy;
    case 45:
    case 48: return Icon::Fog;
    case 51:
    case 53:
    case 55:
    case 56:
    case 57: return Icon::Drizzle;
    case 61:
    case 63:
    case 65:
    case 66:
    case 67:
    case 80:
    case 81:
    case 82: return Icon::Rain;
    case 71:
    case 73:
    case 75:
    case 77:
    case 85:
    case 86: return Icon::Snow;
    case 95:
    case 96:
    case 99: return Icon::Thunder;
    default: return Icon::Unknown;
  }
}

const char* dayLabel(uint8_t index) {
  switch (index) {
    case 0: return "今天";
    case 1: return "明天";
    case 2: return "后天";
    default: return "";
  }
}

// European AQI bands (Open-Meteo's EAQI): 0-20 优, 20-40 良, 40-60 轻度,
// 60-80 中度, 80-100 重度, above 100 严重.
uint8_t aqiLevelIndex(int europeanAqi) {
  if (europeanAqi <= 20) return 0;
  if (europeanAqi <= 40) return 1;
  if (europeanAqi <= 60) return 2;
  if (europeanAqi <= 80) return 3;
  if (europeanAqi <= 100) return 4;
  return 5;
}

const char* aqiLevelText(int europeanAqi) {
  switch (aqiLevelIndex(europeanAqi)) {
    case 0: return "优";
    case 1: return "良";
    case 2: return "轻度污染";
    case 3: return "中度污染";
    case 4: return "重度污染";
    default: return "严重污染";
  }
}

const char* formatHourLabel(const char* iso, char* out, size_t outLen) {
  if (out == nullptr || outLen == 0) return out;
  out[0] = '\0';
  if (iso == nullptr || strlen(iso) < 13) return out;
  snprintf(out, outLen, "%c%c:%c%c", iso[11], iso[12], iso[14], iso[15]);
  return out;
}

const char* weekdayName(int wday) {
  switch (wday) {
    case 0: return "周日";
    case 1: return "周一";
    case 2: return "周二";
    case 3: return "周三";
    case 4: return "周四";
    case 5: return "周五";
    case 6: return "周六";
    default: return "";
  }
}

int weekdayFromIsoDate(const char* iso) {
  if (iso == nullptr || strlen(iso) < 10) return -1;
  for (int i = 0; i < 10; ++i) {
    if (i == 4 || i == 7) {
      if (iso[i] != '-') return -1;
    } else if (iso[i] < '0' || iso[i] > '9') {
      return -1;
    }
  }
  const int year = (iso[0] - '0') * 1000 + (iso[1] - '0') * 100 + (iso[2] - '0') * 10 + (iso[3] - '0');
  const int month = (iso[5] - '0') * 10 + (iso[6] - '0');
  const int day = (iso[8] - '0') * 10 + (iso[9] - '0');
  if (month < 1 || month > 12 || day < 1 || day > 31) return -1;
  // Zeller's congruence; h = 0 means Saturday.
  int m = month;
  int y = year;
  if (m < 3) {
    m += 12;
    y -= 1;
  }
  const int k = y % 100;
  const int j = y / 100;
  const int h = (day + (13 * (m + 1)) / 5 + k + k / 4 + j / 4 + 5 * j) % 7;
  return (h + 6) % 7;  // 0 = Sunday
}

const char* formatClock(uint32_t epoch, char* out, size_t outLen) {
  if (out == nullptr || outLen == 0) return out;
  if (epoch == 0) {
    snprintf(out, outLen, "--:--");
    return out;
  }
  const time_t t = static_cast<time_t>(epoch);
  struct tm local {};
  localtime_r(&t, &local);
  snprintf(out, outLen, "%02d:%02d", local.tm_hour, local.tm_min);
  return out;
}

const char* formatTemp1(float celsius, char* out, size_t outLen) {
  if (out == nullptr || outLen == 0) return out;
  const bool negative = celsius < -0.05f;
  const float magnitude = negative ? -celsius : celsius;
  const int scaled = static_cast<int>(magnitude * 10.0f + 0.5f);
  const int whole = scaled / 10;
  const int frac = scaled % 10;
  snprintf(out, outLen, negative ? "-%d.%d" : "%d.%d", whole, frac);
  return out;
}

const char* formatWhole(float value, char* out, size_t outLen) {
  if (out == nullptr || outLen == 0) return out;
  const bool negative = value < -0.5f;
  const float magnitude = negative ? -value : value;
  snprintf(out, outLen, negative ? "-%d" : "%d", static_cast<int>(magnitude + 0.5f));
  return out;
}

const char* formatShortDate(const char* iso, char* out, size_t outLen) {
  if (out == nullptr || outLen == 0) return out;
  out[0] = '\0';
  if (iso == nullptr || strlen(iso) < 10) return out;
  snprintf(out, outLen, "%c%c-%c%c", iso[5], iso[6], iso[8], iso[9]);
  return out;
}

}  // namespace weather
