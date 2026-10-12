#!/bin/sh
# End-to-end on-device check of the weather firmware.
#
#   1. flash the 1-minute diagnostic build and watch a few sleep/wake cycles
#      (proves: deep sleep, the latch hold, and the RTC timer wake)
#   2. flash the production 30-minute build
#   3. passively watch the port node: the board should now stay asleep
#
#   tools/verify_on_device.sh [port]
#
# The board must be reachable: it wakes on its own timer (up to
# WEATHER_REFRESH_MINUTES of waiting) or immediately when the power key is
# pressed / USB is replugged. Everything lands in build/verify_on_device.log.
set -e
cd "$(dirname "$0")/.."

PORT="${1:-/dev/cu.usbmodem201301}"
LOG="${VERIFY_LOG:-build/verify_on_device.log}"
PY="${WATCH_PYTHON:-$HOME/.platformio/penv/bin/python}"
mkdir -p build
: > "$LOG"

echo "== 1/3  1-minute diagnostic build: flash + watch the sleep/wake cycles ==" | tee -a "$LOG"
FLASH_LOG="$LOG" sh tools/flash_when_awake.sh "$PORT" x4weather_1min 1800 170 2>&1 | tee -a "$LOG"

echo | tee -a "$LOG"
echo "== 2/3  production 30-minute build: flash + watch the boot cycle ==" | tee -a "$LOG"
FLASH_LOG="$LOG" sh tools/flash_when_awake.sh "$PORT" x4weather 300 70 2>&1 | tee -a "$LOG"

echo | tee -a "$LOG"
echo "== 3/3  passive watch: the board should stay asleep now (~30 min interval) ==" | tee -a "$LOG"
"$PY" tools/watch_sleep_cycles.py --port "$PORT" --seconds 120 2>&1 | tee -a "$LOG"
