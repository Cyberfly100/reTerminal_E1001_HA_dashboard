#pragma once

#include "esphome.h"
#include "battery_helpers.h"

#include <driver/gpio.h>
#include <esp_sleep.h>


// ==================================================
// Battery saver deep sleep
// ==================================================

// Deep sleep between refreshes while running on
// battery (see docs/battery-saver-plan.md). ESPHome's
// stock deep_sleep component supports only a single
// wakeup_pin on the ESP32, but all three buttons
// should wake the device, so the sleep entry uses the
// ESP32-S3 EXT1 wake source directly (masked RTC
// GPIOs, any low)

// The three user buttons; RTC-capable (GPIO 0-21 on
// the S3) and active-low
constexpr uint8_t WAKE_BUTTON_GPIO[3] = {3, 4, 5};

// Buttons pressed at the wake instant (bit 0 = green,
// 1 = middle, 2 = left); set once at boot from the RTC
// GPIO wake status and consumed by the HA replay and
// the awake-window choice
inline uint8_t wake_button_mask = 0;

// Sticky for this awake period: the wake was caused
// by a button (wake_button_mask is cleared by the HA
// replay, but the longer interactive window applies
// to the whole period)
inline bool wake_was_button = false;

inline void wake_cause_read() {
  if (
      esp_sleep_get_wakeup_cause() !=
      ESP_SLEEP_WAKEUP_EXT1
  ) {
    return;
  }

  const uint64_t gpio_mask =
      esp_sleep_get_ext1_wakeup_status();

  for (int i = 0; i < 3; i++) {
    if (gpio_mask & (1ULL << WAKE_BUTTON_GPIO[i])) {
      wake_button_mask |= (1 << i);
    }
  }

  wake_was_button = wake_button_mask != 0;

  ESP_LOGI(
      "sleep",
      "Woken by button press, mask %d",
      wake_button_mask
  );
}

// Whether the battery saver currently governs the
// device: the mode flag on, no external power, and
// neither awake hold engaged — the boot hatch (green
// held at boot) or the Home Assistant keep-awake
// helper (its mirror sensor arrives with the API
// connection, before the first sleep decision)
inline bool battery_saver_active() {
  return id(battery_saver_enabled) &&
      !external_power_present() &&
      !id(sleep_hold_active) &&
      !id(sleep_hold_ha).state;
}

// Epoch second after which the device may enter deep
// sleep; 0 = not armed yet (clock not synced since
// the wake, or the saver is inactive)
inline int battery_sleep_deadline = 0;

inline void arm_awake_window(int seconds) {
  const auto now = id(home_time).now();

  if (now.is_valid()) {
    battery_sleep_deadline =
        static_cast<int>(now.timestamp) + seconds;
  }
}

// Extend the awake window from a button interaction
inline void extend_awake_window() {
  if (
      battery_saver_active() &&
      battery_sleep_deadline != 0
  ) {
    arm_awake_window(
        id(battery_button_awake_seconds)
    );
  }
}

// Ceiling for the awake period before Home Assistant
// (re)connects after a wake: HA's reconnect backoff
// can take a while (observed: 45 s after a reboot),
// and the short awake window must not expire while
// the sync refresh — the first thing that actually
// fetches — is still waiting for it. The time sync
// re-arms the real window via
// arm_awake_window_for_wake()
constexpr int BATTERY_CONNECT_CEILING_SECONDS = 150;

// (Re-)arm the real awake window; called when HA
// connects and the time syncs, i.e. at the moment
// data can actually be fetched — the fetches and the
// ~8 s e-ink redraw that follow are safely inside it
inline void arm_awake_window_for_wake() {
  if (battery_saver_active()) {
    arm_awake_window(
        wake_was_button
            ? id(battery_button_awake_seconds)
            : id(battery_awake_seconds)
    );
  }
}

// Replay buttons pressed during sleep to Home
// Assistant: the automation listening on the button
// entity sees the on edge here and the off edge once
// the GPIO loop reports the released state. If the
// button is still held, the natural on_press already
// fired and the extra publish is a same-state no-op
inline void wake_button_replay() {
  if (wake_button_mask == 0) {
    return;
  }

  if (wake_button_mask & (1 << 0)) {
    id(button_green).publish_state(true);
  }

  if (wake_button_mask & (1 << 1)) {
    id(button_middle).publish_state(true);
  }

  if (wake_button_mask & (1 << 2)) {
    id(button_left).publish_state(true);
  }

  // One shot: a later reconnect must not replay again
  wake_button_mask = 0;
}

// The sleep entry itself; only called with all
// fetches answered and the awake deadline passed (the
// deadline being well past the last redraw also
// guarantees the e-ink frame is settled)
inline void enter_battery_sleep() {
  ESP_LOGI(
      "sleep",
      "Entering deep sleep for %d s",
      id(battery_sleep_seconds)
  );

  // The battery measurement divider must not drain
  // during sleep
  id(bsp_battery_enable).turn_off();

  // Keep the button pull-ups alive during sleep so
  // the pins read high until actually pressed
  for (int i = 0; i < 3; i++) {
    gpio_sleep_set_pull_mode(
        static_cast<gpio_num_t>(WAKE_BUTTON_GPIO[i]),
        GPIO_PULLUP_ONLY
    );
  }

  // Wake on any button (all active-low) via EXT1 ...
  esp_sleep_enable_ext1_wakeup(
      (1ULL << WAKE_BUTTON_GPIO[0]) |
          (1ULL << WAKE_BUTTON_GPIO[1]) |
          (1ULL << WAKE_BUTTON_GPIO[2]),
      ESP_EXT1_WAKEUP_ANY_LOW
  );

  // ... and on the refresh timer
  esp_sleep_enable_timer_wakeup(
      static_cast<uint64_t>(
          id(battery_sleep_seconds)
      ) * 1000000ULL
  );

  esp_deep_sleep_start();
}

// 5 s ticks spent without a synced clock since this
// boot: a wake with HA unreachable (router down, HA
// down) would otherwise never arm the awake window
// and keep the device awake forever on battery —
// with nothing to fetch or interact with. It sleeps
// after ~2 minutes instead (the boot hatch covers
// anyone deliberately debugging without HA)
constexpr int MAX_INVALID_CLOCK_TICKS = 24;

inline int invalid_clock_ticks = 0;

// 5 s tick (the same interval as the fetch watchdog):
// refresh the power state, arm the awake window once
// the clock has synced after the wake, and enter deep
// sleep once the deadline passed with no fetch
// outstanding
inline void battery_saver_tick() {
  read_external_power();

  if (!battery_saver_active()) {
    battery_sleep_deadline = 0;
    return;
  }

  const auto now = id(home_time).now();

  // Arm the connect ceiling once the clock is known —
  // which, thanks to the S3's RTC persisting across
  // reboots and deep sleep, is right at boot; the
  // ceiling covers waiting for HA to reconnect, and
  // the time sync re-arms the real, shorter window
  // (arm_awake_window_for_wake). Cold boots without a
  // valid clock (first power-up, dead RTC) fall
  // through to the invalid-clock counter below
  if (battery_sleep_deadline == 0) {
    if (now.is_valid()) {
      invalid_clock_ticks = 0;
      arm_awake_window(
          BATTERY_CONNECT_CEILING_SECONDS
      );
    } else if (
        ++invalid_clock_ticks >
        MAX_INVALID_CLOCK_TICKS
    ) {
      ESP_LOGW(
          "sleep",
          "No clock sync — HA unreachable; "
          "sleeping with nothing left to do"
      );

      enter_battery_sleep();
    }

    return;
  }

  if (
      !now.is_valid() ||
      now.timestamp < battery_sleep_deadline ||
      id(pending_fetches) != 0
  ) {
    return;
  }

  enter_battery_sleep();
}
