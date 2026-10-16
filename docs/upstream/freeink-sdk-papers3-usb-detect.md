# Issue: M5Stack PaperS3 — usbDetect polarity, external-power reporting, and a stale M5GFX pin

**Repo:** https://github.com/Free-Ink/freeink-sdk
**Base commit:** `1a5448d` (Merge pull request #138)
**Patch:** [`0001-papers3-usb-detect-and-m5gfx-version.patch`](0001-papers3-usb-detect-and-m5gfx-version.patch) — applies with
`git apply` on the base commit. Builds clean on all five of our PlatformIO envs (X4 ×2,
papers3, papers3_bench, papers3_bringup) and the SDK's own host suite passes
(`libs/hardware/BatteryMonitor/test/host/run.sh`: 54178 + 39 checks, 0 failures).

Found while porting a weather/crypto firmware from the Xteink X4 (ESP32-C3) to the
PaperS3 (ESP32-S3). Measured on real hardware; everything below has the raw reading
attached so it can be re-checked.

---

## 1. `usbDetect` polarity for PaperS3 is documented the wrong way round

`docs/m5stack-papers3-support.md` and the `M5PAPER_S3` profile both say
"GPIO5, HIGH = USB present". On the unit in hand the pin is **actively driven LOW while a
cable is attached**:

```
[weather] usbDetect GPIO5: floating=0 pullup=0 pulldown=0     ← cable in, battery full
```

All three pull configurations read 0, so something on the board pulls the line down when
USB is present; a floating/disconnected line then reads HIGH with the internal pull-up.
The 0 is not "pin floats low on this unit": with the cable removed and the same code the
device behaves as unplugged (light sleep engages, see below).

**Suggested fix (in the patch):** keep the documented default but make it explicit —
add `bool usbDetectActiveHigh = true;` as the last `BoardProfile` member (defaulted, so
no existing profile initializer changes) and set `false` for `M5PAPER_S3`; correct the
profile comment and the doc line.

## 2. PaperS3 cannot report "external power present" (and that is not the same as charging)

```
bool known = false;
battery.isExternalPowerPresent(&known);   // false, known == false
```

With no charger IC and no I²C gauge the call has no source to read, so it answers
"no opinion" — which is correct behaviour, but it leaves consumers with no way to tell
"cable in, battery full" from "on battery". `isCharging()` cannot cover it: the SDK
documents (and we reproduced) that a full battery stops charging while the cable stays
plugged in, so `isCharging()` goes false and a device that trusts it will happily switch
to its power-saving mode with a cable attached.

**Suggested fix (in the patch):** in `BatteryMonitor::isExternalPowerPresent()`, fall back
to the profile's `usbDetect` pin when there is no gauge/charger to ask — reading it with
the pull that puts a *disconnected* pin in the absent state (`INPUT_PULLUP` for
active-low, `INPUT_PULLDOWN` for active-high) and honouring the new polarity flag. That
makes the PaperS3 answer `true` while plugged in, full battery or not, and gives every
future board with a detect pin the same benefit.

## 3. `m5stack/M5GFX @ 0.2.20` no longer exists on the PlatformIO registry

`platformio.sample.ini` (line 228, and the commented mention at 171) and
`docs/m5stack-papers3-support.md` pin `0.2.20`, so a fresh `pio run -e papers3` stops at

```
UnknownPackageError: Could not find the package with 'm5stack/M5GFX @ 0.2.20' requirements
```

The registry currently offers `0.1.17` and `0.2.27 … 0.2.33`. We pinned **0.2.33** and the
LGFX EPD panel driver works as documented (see timings below) — the patch bumps both
files.

---

## Confirmed correct (the SDK's own "pending hardware validation" list)

Same hardware, same session, all three items came out green — worth recording since the
docs mark them unverified:

| Item | Result |
|---|---|
| Panel rotation | logical frame is **540×960 portrait**; the SDK's landscape convention with `DisplayTarget`'s default rotation puts "up" up |
| GT911 axes (`swapXY=true, flipX=false, flipY=true, gt911CoordsAtByte0=true`) | corner taps map to `(38,317)/(496,316)/(40,919)/(500,923)` for labels drawn at `(24,320)/(516,320)/(24,920)/(516,920)` — correct |
| Battery divider ×2.0 | **4178 mV → 100%** on a full cell; `charging=1` while charging, `charging=0` when full (see §2) |

Refresh timings on this panel (measured with `displayBuffer()`, 42-point sparkline
content), in case they are useful for the docs:

| Mode | Changed content | Unchanged content |
|---|---|---|
| `FULL_REFRESH` | 1098 ms (first paint 1988 ms) | — |
| `HALF_REFRESH` | 1098 ms | — |
| `FAST_REFRESH` | 562 ms | 440 ms |

Bonus finding for anyone doing the same port: **light sleep works with the GT911 IRQ**.
GPIO48 is not an RTC IO, so touch cannot be a deep-sleep wake source, but
`esp_sleep_enable_gpio_wakeup()` + `gpio_wakeup_enable(GPIO48, GPIO_INTR_HIGH_LEVEL)` wakes
the chip reliably — the same approach M5Unified uses. One caveat we hit: the
USB-Serial-JTAG link does not survive light sleep, so a session that wants to keep logging
has to stay awake while USB power is present (which is exactly what §2's fix makes easy to
detect).
