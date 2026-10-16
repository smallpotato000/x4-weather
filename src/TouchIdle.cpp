#include "TouchIdle.h"

#if FREEINK_DEVICE_PAPERS3

#include <Arduino.h>
#include <BatteryMonitor.h>
#include <BoardConfig.h>
#include <driver/gpio.h>

namespace weather {

bool usbPowerPresent() {
  const int8_t pin = BoardConfig::ACTIVE.usbDetect;
  static bool logged = false;
  bool pinSaysYes = false;
  if (pin >= 0) {
    pinMode(pin, INPUT);
    const int floating = digitalRead(pin);
    pinMode(pin, INPUT_PULLUP);
    const int pulledUp = digitalRead(pin);
    pinMode(pin, INPUT_PULLDOWN);
    const int pulledDown = digitalRead(pin);
    if (!logged) {
      logged = true;
      Serial.printf("[weather] usbDetect GPIO%d: floating=%d pullup=%d pulldown=%d\\n", pin, floating,
                    pulledUp, pulledDown);
    }
    pinMode(pin, INPUT_PULLUP);
    pinSaysYes = digitalRead(pin) == LOW;
  }
  // Charging is a second vote: it cannot be the only one, because a full battery
  // stops charging while the cable stays plugged in.
  static BatteryMonitor monitor;
  return pinSaysYes || monitor.isCharging();
}

esp_sleep_wakeup_cause_t lightSleepSlice(uint32_t ms) {
  if (ms == 0) return ESP_SLEEP_WAKEUP_UNDEFINED;
  esp_sleep_enable_timer_wakeup(static_cast<uint64_t>(ms) * 1000ULL);

  const int8_t irq = BoardConfig::ACTIVE.touch.irq;
  if (irq >= 0) {
    const gpio_num_t pin = static_cast<gpio_num_t>(irq);
    gpio_wakeup_enable(pin, BoardConfig::ACTIVE.touch.irqActiveLow ? GPIO_INTR_LOW_LEVEL
                                                                  : GPIO_INTR_HIGH_LEVEL);
    esp_sleep_enable_gpio_wakeup();
  }
  esp_light_sleep_start();
  if (irq >= 0) {
    gpio_wakeup_disable(static_cast<gpio_num_t>(irq));
  }
  return esp_sleep_get_wakeup_cause();
}

void idleSlice(bool& sleeping, uint32_t sliceMs) {
  const bool usb = usbPowerPresent();
  if (usb != !sleeping) {
    sleeping = !usb;
    Serial.printf("[weather] session idle mode: %s\\n",
                  sleeping ? "light sleep between touches" : "awake (USB present)");
    Serial.flush();
  }
  if (!sleeping) {
    delay(10);
    return;
  }
  lightSleepSlice(sliceMs);
}

}  // namespace weather

#endif  // FREEINK_DEVICE_PAPERS3
