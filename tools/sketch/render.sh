#!/bin/sh
# Render the touch settings sketches (PaperS3 port design) on the host.
#
#   tools/sketch/render.sh [screen ...]
#     screen: settings (default) | wifi | password | city | coins
#
# Draws through the same FreeInkUI DisplayTarget and the same baked fonts the
# firmware uses, into a 960x540 panel framebuffer with the portrait orientation
# (logical 540x960, i.e. PaperS3 held tall). Writes build/sketch/<screen>.pbm
# plus <screen>_native.png (panel RAM) and <screen>_portrait.png (upright).
set -e
cd "$(dirname "$0")/../.."

SDK="${FREEINK_SDK:-../freeink-sdk}"
PY="${PREVIEW_PYTHON:-python3}"
OUT_DIR="build/sketch"
mkdir -p "$OUT_DIR"

c++ -std=c++17 -O1 -Wall -Wextra \
  -I include -I "$SDK/libs/ui/FreeInkUI/include" -I "$SDK/libs/network/JsonSax/include" \
  "$SDK/libs/ui/FreeInkUI/src/FreeInkUI.cpp" \
  src/WeatherUi.cpp src/WeatherData.cpp src/WeatherParse.cpp src/CjkGlyphSource.cpp \
  "$SDK/libs/network/JsonSax/src/StreamingJsonParser.cpp" \
  tools/sketch/render_settings.cpp \
  -o "$OUT_DIR/render_settings"

SCREENS="${*:-settings wifi password city coins}"
for screen in $SCREENS; do
  "$OUT_DIR/render_settings" "$OUT_DIR/$screen.pbm" "$screen"
  "$PY" tools/preview/to_png.py "$OUT_DIR/$screen.pbm" "$OUT_DIR/${screen}_native.png" \
    "$OUT_DIR/${screen}_portrait.png"
done
