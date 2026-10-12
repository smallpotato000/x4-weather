#!/bin/sh
# Flash the X4 and watch its serial console.
#
#   tools/flash_and_watch.sh [port] [watch_seconds] [env]
#
# Defaults: /dev/cu.usbmodem201301 (the ESP32-C3's built-in USB Serial/JTAG,
# USB VID:PID 303A:1001) and 45 s of console capture. The port drops when the
# firmware enters deep sleep, so the watcher reopens it until the deadline.
#
# Needs PlatformIO Core (PLATFORMIO_CORE_DIR) and a Python with pyserial
# (PlatformIO's own penv interpreter works: ~/.platformio/penv/bin/python).
set -e
cd "$(dirname "$0")/.."

PORT="${1:-/dev/cu.usbmodem201301}"
WATCH_SECONDS="${2:-45}"
ENV_NAME="${3:-x4weather}"
CORE="${PLATFORMIO_CORE_DIR:-$PWD/../.pio-core}"
PY="${WATCH_PYTHON:-$HOME/.platformio/penv/bin/python}"

echo "== flashing $PORT (env $ENV_NAME) =="
PLATFORMIO_CORE_DIR="$CORE" pio run -e "$ENV_NAME" -t upload --upload-port "$PORT"

echo
echo "== serial console (${WATCH_SECONDS}s) =="
"$PY" - "$PORT" "$WATCH_SECONDS" <<'EOF'
import sys
import time

import serial

port, seconds = sys.argv[1], float(sys.argv[2])
# One open, no reconnect loop: opening this USB-JTAG port resets the chip, so a
# watcher that reopens it every time the device sleeps would keep rebooting the
# board and never see a real sleep cycle.
deadline = time.time() + seconds
link = serial.Serial()
link.port = port
link.baudrate = 115200
link.timeout = 1
link.dtr = False
link.rts = False
try:
    link.open()
except Exception as exc:
    print(f"[watch] cannot open {port}: {exc}", file=sys.stderr)
    raise SystemExit(1)
print(f"[watch] reading {port} for {seconds:.0f}s (no reopen) - the port drops when the "
      "board deep-sleeps", file=sys.stderr)
try:
    while time.time() < deadline:
        try:
            data = link.read(4096)
        except Exception as exc:
            print(f"\n[watch] port dropped ({exc}) - the board went to sleep", file=sys.stderr)
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
