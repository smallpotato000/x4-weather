#!/usr/bin/env python3
"""Watch the X4's deep-sleep cycle without touching the device.

The firmware spends almost all its time in deep sleep, and the ESP32-C3 powers
its USB Serial/JTAG peripheral down there — so /dev/cu.usbmodemXXXX only exists
during the short awake window of each cycle. Watching that node appear and
disappear is a completely passive way to measure the refresh interval, the awake
window, and whether the timer wake works at all.

Do NOT open the port from a tool that runs in a loop: opening the USB-JTAG
serial port resets the chip (`rst:0x15 USB_UART_CHIP_RESET`), which would make
the device look like it never sleeps. This script only stats the node.

  tools/watch_sleep_cycles.py [--port PATH] [--seconds N]

Prints one line per transition plus a summary of the awake/asleep windows.
"""

from __future__ import annotations

import argparse
import os
import statistics
import sys
import time

DEFAULT_PORT = "/dev/cu.usbmodem201301"


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--port", default=DEFAULT_PORT)
    parser.add_argument("--seconds", type=float, default=240.0)
    parser.add_argument("--poll", type=float, default=0.25)
    args = parser.parse_args()

    if not os.path.exists("/dev"):
        sys.exit("error: no /dev")

    start = time.time()
    previous = None
    transitions: list[tuple[float, bool]] = []
    print(f"watching {args.port} for {args.seconds:.0f}s (passive: never opens the port)")
    try:
        while time.time() - start < args.seconds:
            present = os.path.exists(args.port)
            if present != previous:
                elapsed = time.time() - start
                transitions.append((elapsed, present))
                print(f"t+{elapsed:7.1f}s  {'UP  ' if present else 'down'}", flush=True)
                previous = present
            time.sleep(args.poll)
    except KeyboardInterrupt:
        pass

    awake = [b[0] - a[0] for a, b in zip(transitions, transitions[1:]) if a[1]]
    asleep = [b[0] - a[0] for a, b in zip(transitions, transitions[1:]) if not a[1]]
    ups = [t for t, present in transitions if present]
    print()
    if awake:
        print(f"awake windows : {len(awake)}   median {statistics.median(awake):.1f}s")
    if asleep:
        print(f"asleep gaps   : {len(asleep)}  median {statistics.median(asleep):.1f}s")
    if len(ups) >= 2:
        periods = [b - a for a, b in zip(ups, ups[1:])]
        print(f"cycle period  : median {statistics.median(periods):.1f}s "
              f"(sleep + awake; a 30-minute refresh shows ~1800s)")


if __name__ == "__main__":
    main()
