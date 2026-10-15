#!/bin/sh
# Regenerate every generated asset: bitmap fonts + weather icons.
#
# Run this after editing any UI string (the CSP-/CJK-charset scan picks up new
# characters), the icon art in tools/gen_icons.py, or tools/charset_extra.txt.
#
#   tools/gen_all.sh
#
# Needs Pillow (the bundled workspace Python has it). The source TTF lives in
# tools/fonts/ — see tools/fonts/README.md.
set -e
cd "$(dirname "$0")/.."

PY="${PREVIEW_PYTHON:-python3}"
TTF="${WEATHER_TTF:-tools/fonts/NotoSansSC-VF.ttf}"

"$PY" tools/gen_fonts.py \
  --ttf "$TTF" \
  --outdir include/fonts \
  --scan src include tools/preview tools/sketch \
  --extra tools/charset_extra.txt \
  --sdk "${FREEINK_SDK:-../freeink-sdk}"

"$PY" tools/gen_icons.py --out include/fonts/WeatherIcons.h --ttf "$TTF"
