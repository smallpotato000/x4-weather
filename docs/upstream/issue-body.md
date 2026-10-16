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

---

<details><summary>Full patch (also applies with <code>git apply</code>)</summary>

```diff
diff --git a/docs/m5stack-papers3-support.md b/docs/m5stack-papers3-support.md
index 7b7177d..f94642e 100644
--- a/docs/m5stack-papers3-support.md
+++ b/docs/m5stack-papers3-support.md
@@ -69,7 +69,11 @@ touch paging). Firmware without touch page-turn support is unusable here.
   power-off path). Idle level is LOW.
 - **Wake-from-off via RTC**: the BM8563's INT line feeds the PMS150G, not an
   ESP32 GPIO — set an RTC alarm before powering off.
-- **USB detect** — GPIO5, HIGH = USB present (`BoardProfile.usbDetect`).
+- **USB detect** — GPIO5, **active-LOW**: the pin is driven low while a cable is
+  attached (measured on hardware: floating, pull-up and pull-down all read 0 with
+  USB connected and the battery full). `BoardProfile.usbDetectActiveHigh = false`
+  carries that, and `BatteryMonitor::isExternalPowerPresent()` answers from it —
+  which also covers "cable in, battery full", where `isCharging()` is false.
 
 ## Peripherals
 
@@ -98,7 +102,7 @@ board_build.arduino.memory_type = qio_opi
 build_flags = -DBOARD_HAS_PSRAM -DFREEINK_DEVICE_PAPERS3=1
 lib_deps =
   BoardPaperS3=symlink://freeink-sdk/libs/hardware/BoardPaperS3
-  m5stack/M5GFX @ 0.2.20
+  m5stack/M5GFX @ 0.2.33
 ```
 
 No `-DFREEINK_LGFX_EPD_CONFIG` is needed: `FREEINK_DEVICE_PAPERS3` selects
diff --git a/libs/hardware/BatteryMonitor/src/BatteryMonitor.cpp b/libs/hardware/BatteryMonitor/src/BatteryMonitor.cpp
index c521317..b4b524a 100644
--- a/libs/hardware/BatteryMonitor/src/BatteryMonitor.cpp
+++ b/libs/hardware/BatteryMonitor/src/BatteryMonitor.cpp
@@ -802,6 +802,19 @@ bool BatteryMonitor::isExternalPowerPresent(bool* known) const {
       return status.externalPower;
     }
   }
+  // Boards with a dedicated detect pin but no charger IC can still answer: the
+  // pin is the input rail's presence signal. Read it with the pull that puts a
+  // disconnected pin in the "absent" state, so a floating line cannot report a
+  // cable that is not there.
+  const int8_t pin = BoardConfig::ACTIVE.usbDetect;
+  if (pin >= 0) {
+    const bool activeHigh = BoardConfig::ACTIVE.usbDetectActiveHigh;
+    pinMode(pin, activeHigh ? INPUT_PULLDOWN : INPUT_PULLUP);
+    const bool level = digitalRead(pin) == HIGH;
+    if (known) *known = true;
+    return activeHigh ? level : !level;
+  }
+
   if (known) *known = false;
   return false;
 }
diff --git a/libs/hardware/BatteryMonitor/test/host/stubs/Arduino.h b/libs/hardware/BatteryMonitor/test/host/stubs/Arduino.h
index 4541d45..e1c8176 100644
--- a/libs/hardware/BatteryMonitor/test/host/stubs/Arduino.h
+++ b/libs/hardware/BatteryMonitor/test/host/stubs/Arduino.h
@@ -1,6 +1,6 @@
 #pragma once
 #include <cstdint>
-constexpr int HIGH = 1, LOW = 0, INPUT = 1, OUTPUT = 3, INPUT_PULLUP = 5;
+constexpr int HIGH = 1, LOW = 0, INPUT = 1, OUTPUT = 3, INPUT_PULLUP = 5, INPUT_PULLDOWN = 9;
 inline unsigned long hostMillis = 0;  // the test's clock; delay() advances it
 inline unsigned long millis() { return hostMillis; }
 inline void delay(unsigned long ms) { hostMillis += ms; }
diff --git a/libs/hardware/BoardConfig/include/BoardConfig.h b/libs/hardware/BoardConfig/include/BoardConfig.h
index fb10c17..33eba1b 100644
--- a/libs/hardware/BoardConfig/include/BoardConfig.h
+++ b/libs/hardware/BoardConfig/include/BoardConfig.h
@@ -820,6 +820,11 @@ struct BoardProfile {
   // a board with one sets it (EEGO A4).
   I2cFrontlightConfig i2cFrontlight = NO_I2C_FRONTLIGHT;
   HapticConfig haptic = {};
+  // Polarity of `usbDetect`. Default matches the documented "HIGH = USB present".
+  // False = the pin is driven LOW while a cable is attached (M5Stack PaperS3:
+  // measured floating/pull-up/pull-down all read 0 with USB connected), so
+  // consumers must read it with a pull-up and invert.
+  bool usbDetectActiveHigh = true;
 };
 
 constexpr TouchConfig NO_TOUCH = {TouchController::None,
@@ -1406,7 +1411,7 @@ constexpr BoardProfile M5PAPER_S3 = {
     3,     // batteryAdc GPIO3 (ADC1_CH2)
     4,     // batteryChargeStatus GPIO4, LOW = charging (LGS4056H STAT)
     2.0f,  // divider 2:1 (M5Unified's _adc_ratio; pending hardware validation)
-    5,     // usbDetect GPIO5, HIGH = USB present
+    5,     // usbDetect GPIO5, active-LOW: driven low while a cable is attached
     // GT911 on the internal I2C bus SDA41/SCL42 (shared with the BM8563 RTC +
     // BMI270 IMU), INT=GPIO48, no reset wired (self-loads its config), 0x5D alt
     // 0x14. Portrait digitizer (540x960) on the landscape panel -> swapXY, rawMax
@@ -1429,7 +1434,8 @@ constexpr BoardProfile M5PAPER_S3 = {
     // via Rtc alarm), not an ESP32 GPIO. BMI270 IMU (0x68) unsupported — omitted.
     {41, 42, 400000, 0x51, 0, 0, 0, RtcType::Pcf8563, ImuType::None},
     1.2f,  // uiScale: 4.7" 960x540 touch (~234 PPI) — finger-sized chrome, like LilyGo/M5Paper
-    {}};   // no power latch: the PMS150G self-latches; off = GPIO44 pulse (BoardPaperS3::powerOff)
+    {},
+    false};  // no power latch (PMS150G self-latches; off = GPIO44 pulse), usbDetect active-low
 
 // --- EEGO Reader A4 — ESP32-S3 N16R8, UC8279C (768x552) + GSLX680 touch -------
 // CrossPoint fork device; pins/resolution/calibration from stock-firmware RE,
diff --git a/platformio.sample.ini b/platformio.sample.ini
index 94c2c21..b6f869e 100644
--- a/platformio.sample.ini
+++ b/platformio.sample.ini
@@ -168,7 +168,7 @@ build_flags =
 ;   -DFREEINK_LGFX_EPD_CONFIG=lilygoT5S3LgfxConfig
 ; lib_deps =
 ;   ${base.lib_deps}
-;   m5stack/M5GFX @ 0.2.20
+;   m5stack/M5GFX @ 0.2.33
 
 ; --- M5Paper v1.1 (4.7" ED047TC1 / IT8951E) — classic ESP32 -------------------
 ; A third MCU family: the original ESP32 (ESP32-D0WDQ6), not C3 or S3, so this is
@@ -225,7 +225,7 @@ build_flags =
 lib_deps =
   ${base.lib_deps}
   BoardPaperS3=symlink://freeink-sdk/libs/hardware/BoardPaperS3
-  m5stack/M5GFX @ 0.2.20
+  m5stack/M5GFX @ 0.2.33
 
 ; --- EEGO Reader A4 — ESP32-S3 N16R8, UC8279C (768x552) + GSLX680 touch --------
 ; UC8279C SPI panel (Uc8279cA4Driver), GSLX680 touch + PCF8563 RTC on the shared
```

</details>
