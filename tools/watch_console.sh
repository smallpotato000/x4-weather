#!/bin/sh
# Read the X4's serial console without flashing anything.
#
#   tools/watch_console.sh [port] [seconds] [wait_seconds]
#
# Waits for the board's USB node to appear (it only exists while the firmware is
# awake — see the README's deep-sleep notes), opens the port *once* and prints
# whatever it says for `seconds`. One open, no reconnect loop: re-opening this
# USB-JTAG port resets the chip, which would restart the cycle you are trying to
# watch.
set -e
cd "$(dirname "$0")/.."

PORT="${1:-/dev/cu.usbmodem201301}"
WATCH_SECONDS="${2:-45}"
WAIT_SECONDS="${3:-2100}"
PY="${WATCH_PYTHON:-$HOME/.platformio/penv/bin/python}"

deadline=$(( $(date +%s) + WAIT_SECONDS ))
while [ "$(date +%s)" -lt "$deadline" ]; do
  if [ -c "$PORT" ]; then
    echo "[watch] $(date +%H:%M:%S) $PORT is up — reading for ${WATCH_SECONDS}s"
    "$PY" - "$PORT" "$WATCH_SECONDS" <<'EOF'
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
except Exception as exc:
    print(f"[watch] cannot open {port}: {exc}", file=sys.stderr)
    raise SystemExit(1)

deadline = time.time() + seconds
try:
    while time.time() < deadline:
        try:
            data = link.read(4096)
        except Exception as exc:
            print(f"\n[watch] port dropped ({exc}) — the board went to sleep", file=sys.stderr)
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
    exit 0
  fi
  sleep 0.5
done

echo "[watch] timed out after ${WAIT_SECONDS}s without a usable $PORT" >&2
exit 1
