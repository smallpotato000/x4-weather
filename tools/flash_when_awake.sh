#!/bin/sh
# Flash the X4 the moment its USB port reappears, then watch the console.
#
# The weather firmware spends almost all its time in deep sleep, and the
# ESP32-C3's USB Serial/JTAG peripheral is powered down there — so
# /dev/cu.usbmodemXXXX only exists during the ~15-20 s awake window of each
# cycle (or while the ROM bootloader is in download mode). This script polls for
# the port node, flashes it *directly with esptool* (no PlatformIO build check,
# so the connect fits comfortably inside the window; once the stub flasher is
# running the chip stays in download mode and cannot fall back asleep), and then
# captures the serial log of the following cycles.
#
#   tools/flash_when_awake.sh [port] [env] [wait_seconds] [watch_seconds]
#
# Pressing the power key wakes the board early if you don't want to wait for the
# next timer wake.
set -e
cd "$(dirname "$0")/.."

PORT="${1:-/dev/cu.usbmodem201301}"
ENV_NAME="${2:-x4weather_1min}"
WAIT_SECONDS="${3:-2700}"
WATCH_SECONDS="${4:-200}"
CORE="${PLATFORMIO_CORE_DIR:-$PWD/../.pio-core}"
PY="${WATCH_PYTHON:-$HOME/.platformio/penv/bin/python}"
ESPTOOL="$CORE/packages/tool-esptoolpy/esptool.py"
BUILD=".pio/build/$ENV_NAME"
LOG="${FLASH_LOG:-build/flash_when_awake.log}"
mkdir -p "$(dirname "$LOG")"

log() {
  echo "$@" | tee -a "$LOG"
}

watch_console() {
  "$PY" - "$PORT" "$WATCH_SECONDS" <<'EOF' | tee -a "$LOG"
import sys
import time

import serial

port, seconds = sys.argv[1], float(sys.argv[2])
deadline = time.time() + seconds
while time.time() < deadline:
    try:
        link = serial.Serial()
        link.port = port
        link.baudrate = 115200
        link.timeout = 1
        link.dtr = False
        link.rts = False
        link.open()
    except Exception:
        time.sleep(1)
        continue
    try:
        while time.time() < deadline:
            try:
                data = link.read(4096)
            except Exception:
                break
            if data:
                sys.stdout.write(data.decode("utf-8", "replace"))
                sys.stdout.flush()
    finally:
        try:
            link.close()
        except Exception:
            pass
print("\n[watch] done")
EOF
}

flash() {
  # The coredump partition lands on flash the OEM firmware used to occupy, so
  # erase it once: the boot-time core-dump probe then finds an empty (0xFF)
  # header instead of a stale OEM dump ("Incorrect size of core dump image").
  # --after no-reset keeps the chip in the stub flasher, so the write below
  # connects instantly and the board cannot fall back asleep in between.
  "$PY" "$ESPTOOL" --chip esp32c3 --port "$PORT" --baud 921600 \
    --before default-reset --after no-reset \
    erase-region 0x410000 0x10000
  "$PY" "$ESPTOOL" --chip esp32c3 --port "$PORT" --baud 921600 \
    --before default-reset --after hard-reset \
    write-flash --flash-mode dio --flash-freq 80m --flash-size 16MB \
    0x0 "$BUILD/bootloader.bin" \
    0x8000 "$BUILD/partitions.bin" \
    0x10000 "$BUILD/firmware.bin"
}

: > "$LOG"
log "[wait] watching $PORT for up to ${WAIT_SECONDS}s to flash $ENV_NAME"
deadline=$(( $(date +%s) + WAIT_SECONDS ))
attempt=0
while [ "$(date +%s)" -lt "$deadline" ]; do
  if [ -c "$PORT" ]; then
    attempt=$((attempt + 1))
    log "[wait] $(date +%H:%M:%S) $PORT is up (attempt $attempt) — flashing"
    sleep 1  # let the boot-time panel refresh finish before resetting the chip
    if flash >>"$LOG" 2>&1; then
      log "[wait] flashed at $(date +%H:%M:%S); watching the console for ${WATCH_SECONDS}s"
      watch_console
      exit 0
    fi
    log "[wait] esptool attempt failed; falling back to a PlatformIO upload"
    if PLATFORMIO_CORE_DIR="$CORE" pio run -e "$ENV_NAME" -t upload --upload-port "$PORT" >>"$LOG" 2>&1; then
      log "[wait] flashed via PlatformIO at $(date +%H:%M:%S)"
      watch_console
      exit 0
    fi
    log "[wait] upload failed again; waiting for the next window"
    sleep 2
  else
    sleep 0.5
  fi
done

log "[wait] timed out after ${WAIT_SECONDS}s without a usable $PORT"
exit 1
