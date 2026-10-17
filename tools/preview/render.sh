#!/bin/sh
# Render the weather screen on the host — no device, no PlatformIO.
#
#   tools/preview/render.sh [variant] [response.json] [air.json]
#     variant: ok (default) | hourly | aqi | crypto | crypto-gainers |
#              crypto-losers | busy | error
#     response.json / air.json: recorded Open-Meteo responses to render
#     (default to the recorded Wuhan ones in tools/preview/)
#
# Compiles the real UI code (FreeInkUI + src/WeatherUi.cpp + src/CjkGlyphSource.cpp
# + the generated fonts) against a simulated 800x480 framebuffer and writes
# preview/<variant>_native.png (panel RAM as the panel scans it) plus
# preview/<variant>_portrait.png (the same frame rotated upright, i.e. what the
# user sees holding the X4 tall).
set -e
cd "$(dirname "$0")/../.."

VARIANT="${1:-ok}"
RESPONSE="${2:-tools/preview/sample_response.json}"
AIR="${3:-tools/preview/sample_air_quality.json}"
CRYPTO="${4:-tools/preview/sample_crypto.json}"
BATTERY="${5:-82}"   # 预览用的电量百分比（负数 = 不显示电量）
SDK="${FREEINK_SDK:-../freeink-sdk}"
PY="${PREVIEW_PYTHON:-python3}"
OUT_DIR="build/preview"
mkdir -p "$OUT_DIR"

# Two binaries: the default one matches the X4 build, the other carries
# -DFREEINK_DEVICE_PAPERS3 so the PaperS3-only pages (the home page) exist.
SOURCES="$SDK/libs/ui/FreeInkUI/src/FreeInkUI.cpp
  src/WeatherUi.cpp src/WeatherData.cpp src/WeatherParse.cpp src/CityList.cpp src/SettingsUi.cpp src/CjkGlyphSource.cpp
  $SDK/libs/network/JsonSax/src/StreamingJsonParser.cpp
  tools/preview/render_preview.cpp"
c++ -std=c++17 -O1 -Wall -Wextra \
  -I include -I "$SDK/libs/ui/FreeInkUI/include" -I "$SDK/libs/network/JsonSax/include" \
  $SOURCES -o "$OUT_DIR/render_preview"
case "$VARIANT" in
  paper-*|settings*)
    c++ -std=c++17 -O1 -Wall -Wextra -DFREEINK_DEVICE_PAPERS3=1 \
      -I include -I "$SDK/libs/ui/FreeInkUI/include" -I "$SDK/libs/network/JsonSax/include" \
      $SOURCES -o "$OUT_DIR/render_preview_s3"
    BIN="$OUT_DIR/render_preview_s3"
    ;;
  *)
    BIN="$OUT_DIR/render_preview"
    ;;
esac

"$BIN" "$OUT_DIR/$VARIANT.pbm" "$VARIANT" "$RESPONSE" "$AIR" "$CRYPTO" "$BATTERY"

"$PY" tools/preview/to_png.py "$OUT_DIR/$VARIANT.pbm" "$OUT_DIR/${VARIANT}_native.png" "$OUT_DIR/${VARIANT}_portrait.png"
