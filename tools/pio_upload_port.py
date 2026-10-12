"""Aim PlatformIO's uploader at the X4's USB-CDC port (PlatformIO extra script).

The ESP32-C3's USB Serial/JTAG shows up as ``/dev/cu.usbmodem<serial>``. Without
this hook PlatformIO's own detection picks the *first* ``/dev/cu.*`` entry, which
on a Mac that also has Bluetooth audio is a headset endpoint — the upload then
fails against a port that has nothing to do with the board.

Behaviour:

* an explicit ``upload_port`` in platformio.ini or ``--upload-port`` on the
  command line always wins (the hook does nothing);
* otherwise the first ``/dev/cu.usbmodem*`` node is used;
* when the board is deeply asleep its USB peripheral is powered down and no node
  exists, so the hook stays quiet and PlatformIO falls back to its usual
  detection (which will fail — you cannot flash a sleeping board; wake it with
  the power key or use ``tools/flash_when_awake.sh``).

Not macOS-specific on purpose: on other systems the glob simply matches nothing.
"""

import glob

Import("env")  # noqa: F821 - injected by PlatformIO

if not env.subst("$UPLOAD_PORT"):
    ports = sorted(glob.glob("/dev/cu.usbmodem*"))
    if ports:
        env.Replace(UPLOAD_PORT=ports[0])
        print(f"[upload_port] using {ports[0]}")
