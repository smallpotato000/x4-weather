#!/bin/sh
# Flash the M5Stack PaperS3 (ESP32-S3, native USB CDC) the moment its port exists.
#
# Unlike the X4 there is no readable wake button: deep sleep powers the USB
# peripheral down, so /dev/cu.usbmodemXXXX only exists while the firmware is
# awake (the app's touch window, or every refresh cycle) or while the ROM
# bootloader is in download mode. Press the side button to reboot the board if
# you do not want to wait for the next timer wake; this script polls for the node
# and flashes the pre-built binary directly with esptool.
#
#   tools/flash_papers3.sh [port] [env] [wait_seconds] [watch_seconds]
set -e
cd "$(dirname "$0")/.."

PORT="${1:-/dev/cu.usbmodem21201}"
ENV_NAME="${2:-papers3}"
WAIT_SECONDS="${3:-1800}"
WATCH_SECONDS="${4:-45}"
CORE="${PLATFORMIO_CORE_DIR:-$PWD/../.pio-core}"
PY="${WATCH_PYTHON:-$HOME/.platformio/penv/bin/python}"
ESPTOOL="$CORE/packages/tool-esptoolpy/esptool.py"
BUILD=".pio/build/$ENV_NAME"
LOG="${FLASH_LOG:-build/flash_papers3.log}"
mkdir -p "$(dirname "$LOG")"

log() { echo "$@" | tee -a "$LOG"; }

if [ ! -f "$BUILD/firmware.bin" ]; then
  echo "[flash] $BUILD/firmware.bin missing — build $ENV_NAME first" >&2
  exit 1
fi

# boot_app0 lives in the framework and is only copied next to the images by a
# PlatformIO upload, so fall back to the framework's copy.
BOOT_APP0="$BUILD/boot_app0.bin"
if [ ! -f "$BOOT_APP0" ]; then
  BOOT_APP0="$CORE/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin"
fi
if [ ! -f "$BOOT_APP0" ]; then
  echo "[flash] boot_app0.bin not found (framework package missing?)" >&2
  exit 1
fi

log "[flash] waiting for $PORT (up to ${WAIT_SECONDS}s) to write $ENV_NAME"
deadline=$(( $(date +%s) + WAIT_SECONDS ))
while [ "$(date +%s)" -lt "$deadline" ]; do
  if [ -c "$PORT" ]; then
    log "[flash] $(date +%H:%M:%S) $PORT is up — flashing"
    # --after no-reset keeps the stub flasher in place so the write is not cut
    # short by the app booting into deep sleep mid-upload.
    # keep/keep/keep: preserve the flash mode, frequency and size baked into the
    # images. Forcing qio here (the SDK sample's build flag) while the built
    # bootloader header says DIO makes the ROM boot into a TG0 watchdog loop.
    # boot_app0 is written the way PlatformIO's own upload does it.
    "$PY" "$ESPTOOL" --chip esp32s3 --port "$PORT" --baud 921600 \
      --before default-reset --after no-reset write_flash -z \
      --flash_mode keep --flash_freq keep --flash_size keep \
      0x0 "$BUILD/bootloader.bin" \
      0x8000 "$BUILD/partitions.bin" \
      0xe000 "$BOOT_APP0" \
      0x10000 "$BUILD/firmware.bin" >>"$LOG" 2>&1
    log "[flash] $(date +%H:%M:%S) written — resetting and watching ${WATCH_SECONDS}s"
    "$PY" "$ESPTOOL" --chip esp32s3 --port "$PORT" --after hard-reset --no-stub read-mac \
      >>"$LOG" 2>&1 || true
    "$PY" - "$PORT" "$WATCH_SECONDS" <<'EOF' 2>&1 | tee -a "$LOG"
import sys
import time

import serial

port, seconds = sys.argv[1], float(sys.argv[2])
link = serial.Serial()
link.port = port
link.baudrate = 115200
link.timeout = 1
link.dtr = False
link.rts = False
try:
    link.open()
except Exception as exc:  # noqa: BLE001 - the node can vanish before we attach
    print(f"[flash] cannot open {port}: {exc}", file=sys.stderr)
    raise SystemExit(0)

deadline = time.time() + seconds
try:
    while time.time() < deadline:
        try:
            data = link.read(4096)
        except Exception as exc:  # noqa: BLE001 - deep sleep drops the port
            print(f"\n[flash] port dropped ({exc}) — the board went to sleep", file=sys.stderr)
            break
        if data:
            sys.stdout.write(data.decode("utf-8", "replace"))
            sys.stdout.flush()
finally:
    try:
        link.close()
    except Exception:  # noqa: BLE001
        pass
print("\n[flash] done")
EOF
    exit 0
  fi
  sleep 0.5
done

log "[flash] timed out after ${WAIT_SECONDS}s without a usable $PORT"
exit 1
