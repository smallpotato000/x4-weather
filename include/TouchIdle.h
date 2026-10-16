#pragma once

// Idle policy for the touch boards' sessions: sleep in slices with the GT911
// interrupt armed, but stay awake (and keep the log alive) while USB power is
// present, because the USB-Serial-JTAG link does not survive light sleep.
//
// Shared by the data session and the settings session in main.cpp.

#if FREEINK_DEVICE_PAPERS3

#include <esp_sleep.h>
#include <stdint.h>

namespace weather {

// Longest single light-sleep slice; the safety timer fires this often even if the
// touch IRQ never asserts.
constexpr uint32_t kWakeSliceMs = 1000;

// True when the board is on USB power. The profile's usbDetect pin is actively
// driven LOW while a cable is attached on this hardware (measured: floating,
// pull-up and pull-down all read 0), and isCharging() alone is not enough — a
// full battery stops charging with the cable still plugged in.
bool usbPowerPresent();

// Sleeps one slice with the touch IRQ (plus a timer) armed; returns the wake
// cause so callers can report irq-vs-timer wakes.
esp_sleep_wakeup_cause_t lightSleepSlice(uint32_t ms);

// One idle step: sleeps when on battery, delays when on USB, and logs the mode
// when it changes. `sleeping` carries the current mode across calls.
void idleSlice(bool& sleeping, uint32_t sliceMs = kWakeSliceMs);

}  // namespace weather

#endif  // FREEINK_DEVICE_PAPERS3
