#pragma once

// ---------------------------------------------------------------------------
// X4 weather firmware configuration.
//
// Put your credentials in include/secrets.h (git-ignored) to keep them out of
// this file:
//
//     #define WIFI_SSID     "my-network"
//     #define WIFI_PASSWORD "my-password"
//
// The defaults below are used when secrets.h is absent.
// ---------------------------------------------------------------------------

#if __has_include("secrets.h")
#include "secrets.h"
#endif

#ifndef WIFI_SSID
#define WIFI_SSID "your-wifi-ssid"
#endif

#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD "your-wifi-password"
#endif

// --- Location --------------------------------------------------------------
// Just a city name: WEATHER_CITY is resolved through Open-Meteo's geocoding API
// on the first boot and the coordinates are cached in RTC memory, so it costs
// one request once (and again only if geocoding failed or the name changed).
// It accepts UTF-8, so 武汉 / 東京 / Zürich all work.
#ifndef WEATHER_CITY
#define WEATHER_CITY "Wuhan"
#endif

// Header label. Leave it empty ("") to use the name geocoding returns.
#ifndef WEATHER_PLACE
// Firmware version, shown on the settings About screen.
#define WEATHER_VERSION "v1.0.0"

#define WEATHER_PLACE "武汉"
#endif

// Fallback coordinates, used when WEATHER_CITY is empty or geocoding fails.
#ifndef WEATHER_LATITUDE
#define WEATHER_LATITUDE 30.5928f
#endif
#ifndef WEATHER_LONGITUDE
#define WEATHER_LONGITUDE 114.3055f
#endif

// POSIX TZ string for the local clock (CST-8 = UTC+8, no DST). Used both for
// the SNTP-synced clock and for the "updated at HH:MM" footer.
#define WEATHER_TIMEZONE "CST-8"
#define WEATHER_NTP_1 "ntp.aliyun.com"
#define WEATHER_NTP_2 "cn.pool.ntp.org"
#define WEATHER_NTP_3 "pool.ntp.org"

// --- Refresh policy --------------------------------------------------------
// 深睡省电版：每次刷完屏就进入 deep sleep，30 分钟后由 RTC 定时器唤醒，
// 重新联网取数、刷屏、再睡。整机续航取决于这个间隔。
#ifndef WEATHER_REFRESH_MINUTES
#define WEATHER_REFRESH_MINUTES 30
#endif

// 取数失败时提前重试（分钟）——失败时屏幕保留上一次的画面，只是底栏提示失败。
#ifndef WEATHER_RETRY_MINUTES
#define WEATHER_RETRY_MINUTES 5
#endif

// 深睡期间按键唤醒：X4 的电源键（GPIO3）按下即醒，醒来立刻刷新一次。
// 若发现设备会莫名其妙地自己醒来（按键悬空/干扰），把它改成 0。
#ifndef WEATHER_WAKE_ON_BUTTON
#define WEATHER_WAKE_ON_BUTTON 1
#endif

// 交互会话：电源键唤醒后保持醒着这么久，期间左右键切页、确认键重新取数、
// 返回键立刻休眠；无操作自动休眠。设 0 就完全关闭（按一下只刷新一次就睡）。
// 注意：C3 X4 的导航键挂在 ADC 电阻梯上，不能作为深睡唤醒源，所以"切页"只能
// 发生在设备醒着的时候。
#ifndef WEATHER_INTERACTIVE_SECONDS
#define WEATHER_INTERACTIVE_SECONDS 25
#endif

// 长按电源键关机阈值（毫秒）。
#ifndef WEATHER_POWER_OFF_HOLD_MS
#define WEATHER_POWER_OFF_HOLD_MS 2500
#endif

// 逐小时页取回/显示多少小时。
#ifndef WEATHER_FORECAST_HOURS
#define WEATHER_FORECAST_HOURS 12
#endif

// 触屏机（PaperS3）画完之后的交互窗口：这段时间内保持清醒听触控（点击翻页、
// 上下滑换排序、长按关机），超时无操作就去睡下一轮。X4 用按键会话，不看这个值。
#ifndef WEATHER_TOUCH_SESSION_MS
// 5 minutes: the window sits in light sleep (~1-2 mA) between touches, so a long
// window is cheap; it reopens on every refresh cycle.
#define WEATHER_TOUCH_SESSION_MS 300000
#endif

// 冷启动（上电/复位/烧录后）先显示哪一页：0 数字货币 / 1 当前天气 / 2 逐小时 / 3 空气质量。
// 深睡唤醒不受影响——那会继续显示你上次看的那一页。
#ifndef WEATHER_DEFAULT_PAGE
#define WEATHER_DEFAULT_PAGE 0
#endif

#define WEATHER_WIFI_TIMEOUT_MS 25000
#define WEATHER_HTTP_TIMEOUT_MS 15000

// Number of forecast rows to request/render (the layout holds three).
#define WEATHER_FORECAST_DAYS 3
