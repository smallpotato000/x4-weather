#!/bin/sh
# Host-side checks: parser/data-model tests + a rendered preview of every screen
# state. No device and no PlatformIO needed.
#
#   tools/test_host.sh
#
# Uses c++ and a Python with Pillow (the workspace bundled interpreter works:
#   PREVIEW_PYTHON=/path/to/python tools/test_host.sh).
set -e
cd "$(dirname "$0")/.."

SDK="${FREEINK_SDK:-../freeink-sdk}"
PY="${PREVIEW_PYTHON:-python3}"
OUT_DIR="build/host"
mkdir -p "$OUT_DIR"

CXXFLAGS="-std=c++17 -O1 -Wall -Wextra -Werror -I include -I $SDK/libs/network/JsonSax/include"

echo "== parser + data model tests =="
c++ $CXXFLAGS \
  "$SDK/libs/network/JsonSax/src/StreamingJsonParser.cpp" \
  src/WeatherParse.cpp src/WeatherData.cpp \
  tools/preview/test_parse.cpp \
  -o "$OUT_DIR/test_parse"
"$OUT_DIR/test_parse" tools/preview/sample_response.json

echo
echo "== rendered previews =="
for variant in ok hourly aqi busy error; do
  PREVIEW_PYTHON="$PY" sh tools/preview/render.sh "$variant"
done
