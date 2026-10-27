#pragma once

#include "esphome.h"
#include "battery_helpers.h"

#include <driver/gpio.h>
#include <esp_sleep.h>
#include <esp_ota_ops.h>


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

// Wake diagnostics, persisted for the whole session and
// re-logged at the first tick: the boot-moment log line
// fires at setup priority 600, before any API client can
// attach, so it is network-invisible by construction. It
// is also exposed to HA via the "E1001 Wake Info" template
// text sensor — the reliable post-hoc channel (the USB
// bridge cannot carry the app log on this framework
// version; see the README hardware notes)
inline char wake_info[64] = "reset (unknown reason)";

inline void wake_cause_read() {
  const esp_sleep_wakeup_cause_t cause =
      esp_sleep_get_wakeup_cause();

  if (cause == ESP_SLEEP_WAKEUP_TIMER) {
    ESP_LOGI("sleep", "Woken by the refresh timer");
    snprintf(wake_info, sizeof(wake_info), "refresh timer");
    return;
  }

  if (cause != ESP_SLEEP_WAKEUP_EXT1) {
    const char *reason = "other";
    switch (esp_reset_reason()) {
      case ESP_RST_POWERON:   reason = "power-on"; break;
      case ESP_RST_SW:       reason = "software (OTA/esp_restart)"; break;
      case ESP_RST_PANIC:    reason = "panic/crash"; break;
      case ESP_RST_INT_WDT:  reason = "interrupt watchdog"; break;
      case ESP_RST_TASK_WDT: reason = "task watchdog"; break;
      case ESP_RST_BROWNOUT: reason = "brownout"; break;
      case ESP_RST_DEEPSLEEP: reason = "deep sleep (unexpected path)"; break;
      default: break;
    }

    ESP_LOGI("sleep", "Woken by reset (%s)", reason);
    snprintf(
        wake_info, sizeof(wake_info),
        "reset (%s)", reason
    );
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
  snprintf(
      wake_info, sizeof(wake_info),
      "button mask %d", wake_button_mask
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
    // Visible in the logs: the connect ceiling at boot,
    // the real window at the anchor, and every button
    // press extending it — the sleep timing can be
    // attributed from the log alone
    ESP_LOGI("sleep", "Awake window armed for %d s", seconds);
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

// Deferred wake-button replay. The state pushes that
// trigger the anchor arrive as api messages BEFORE
// Home Assistant's device-entity subscription is
// processed — the connection does not accept entity
// sends yet, and everything published from inside that
// message handler is silently dropped (observed: the
// whole pulse swallowed, and HA's subscription burst
// afterwards delivered only the final state —
// unavailable -> off). The anchor therefore stamps an
// earliest-replay deadline, and the 5 s tick delivers
// the pulse once it passes (see battery_saver_tick):
// the tick context is outside any api message handler,
// past the message backlog, and if the fetch cycle's
// e-ink redraw is blocking the loop the queued sends
// flush right after it
constexpr int WAKE_REPLAY_DEFER_SECONDS = 2;

inline int wake_replay_deadline = 0;

inline void defer_wake_button_replay() {
  if (
      wake_button_mask == 0 ||
      wake_replay_deadline != 0
  ) {
    return;
  }

  const auto now = id(home_time).now();

  if (!now.is_valid()) {
    return;
  }

  wake_replay_deadline =
      static_cast<int>(now.timestamp) +
      WAKE_REPLAY_DEFER_SECONDS;

  ESP_LOGI(
      "sleep",
      "Wake button press pending replay, mask %d",
      wake_button_mask
  );
}

// Replay buttons pressed during sleep to Home Assistant
// as event-entity types: one fire-and-forget message per
// press — no state, no pulse, no on/off ordering, and
// therefore immune to the api's deferred, state-
// deduplicated sends (a binary-sensor on→off pair
// published within one loop pass collapses into a
// single off message — the failure mode that cost a
// day of debugging). Delivered from the tick via the
// deadline in defer_wake_button_replay(); presses
// replay even when HA was unreachable at the wake, and
// the natural awake presses fire the same types from
// the buttons' on_press (the YAML). Edge case: types
// triggered within one un-flushed pass are deduplicated
// to the last, so two buttons pressed during the SAME
// sleep deliver one event — vanishingly rare
inline void wake_button_replay() {
  if (wake_button_mask == 0) {
    return;
  }

  ESP_LOGI(
      "sleep",
      "Replaying button press(es) to HA, mask %d",
      wake_button_mask
  );

  if (wake_button_mask & (1 << 0)) {
    id(e1001_buttons).trigger("green");
  }

  if (wake_button_mask & (1 << 1)) {
    id(e1001_buttons).trigger("middle");
  }

  if (wake_button_mask & (1 << 2)) {
    id(e1001_buttons).trigger("left");
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

  // An OTA-updated app must confirm itself before any
  // reset, or the bootloader rolls the update back at
  // the next boot. Observed: a fresh OTA app whose
  // first awake window expired ~36 s after boot —
  // before the 60 s safe-mode mark — rolled back at the
  // button wake, and the OLD firmware answered the
  // press. Reaching a deliberate deep sleep is the
  // strongest health signal there is: mark the running
  // app valid first (a no-op when no update is pending)
  esp_ota_mark_app_valid_cancel_rollback();

  // The API log sender queues messages; give it a
  // moment to actually deliver the line before the
  // shutdown tears the connection down — otherwise
  // the device can disappear without a trace in the
  // logs
  delay(250);

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
//
// The very first tick of each boot also logs *why*
// the saver is or is not governing — every sleep/no-
// sleep decision must be attributable from the logs
// alone
inline bool saver_status_logged = false;

inline void log_saver_status() {
  if (saver_status_logged) {
    return;
  }
  saver_status_logged = true;

  // Re-log the wake cause here: the boot-moment line
  // fires before any API client can attach and is
  // network-invisible by construction (see wake_info)
  ESP_LOGI("sleep", "Wake cause: %s", wake_info);

  if (!id(battery_saver_enabled)) {
    ESP_LOGI("sleep", "Battery saver off (battery_saver: 'false')");
  } else if (external_power_state == 1) {
    ESP_LOGI("sleep", "Battery saver on, external power detected — staying awake");
  } else if (id(sleep_hold_active)) {
    ESP_LOGI("sleep", "Battery saver on, boot hatch holds the device awake");
  } else if (id(sleep_hold_ha).state) {
    ESP_LOGI("sleep", "Battery saver on, keep-awake helper holds the device awake");
  } else {
    ESP_LOGI(
        "sleep",
        "Battery saver active — on battery, pack %.2f V",
        id(battery_voltage).state
    );
  }
}

// Fast-path delivery for the deferred wake-button
// replay (a 500 ms interval in the YAML): the same
// delivery as the battery-saver tick's backstop below,
// at a finer granularity — a 5 s tick alone can leave
// the event up to 5 s past the earliest-replay
// deadline, which the HA automations waiting on a
// wake press feel as sluggish. Whichever of the two
// ticks sees the passed deadline first delivers; the
// other no-ops on the cleared deadline
inline void wake_replay_fast_tick() {
  if (wake_replay_deadline != 0) {
    const auto now = id(home_time).now();

    if (
        now.is_valid() &&
        now.timestamp >= wake_replay_deadline
    ) {
      wake_replay_deadline = 0;
      wake_button_replay();
    }
  }
}

inline void battery_saver_tick() {
  // Deliver the deferred wake-button replay once past
  // its deadline (see defer_wake_button_replay): the
  // tick context is outside any api message handler and
  // past Home Assistant's subscription, so the event
  // triggers cannot be dropped. Usually beaten to it by
  // the 500 ms wake_replay_fast_tick
  wake_replay_fast_tick();

  read_external_power();
  log_saver_status();

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
