#pragma once

#include "esphome.h"


// ==================================================
// Refresh fetch bookkeeping
// ==================================================

// The refresh_dashboard script fires several
// homeassistant.action fetches per cycle (weather
// daily/hourly, one per calendar). The display redraws
// once all of them have answered — but the count must
// never be hardcoded: every fetch registers a ticket
// right before it runs and returns it from both its
// on_success and on_error, so adding, removing or
// gating a fetch needs no change anywhere else

// Seconds after cycle start after which the watchdog
// forces a redraw even if a fetch never answered; a
// single hung action must not freeze the dashboard
// until the next cycle
constexpr int FETCH_WATCHDOG_SECONDS = 20;

// Start of a refresh cycle: drop any tickets left over
// from a previous cycle (e.g. a response arriving after
// the watchdog already forced a redraw) and arm the
// fetch watchdog. Without a synced clock there is
// nothing to measure a deadline against; the first
// time sync then triggers a refresh anyway
inline void fetch_cycle_begin() {
  id(pending_fetches) = 0;

  // Every dispatch path funnels through here; logging
  // it makes the trigger of any given cycle visible in
  // the logs instead of having to be inferred from
  // timings
  ESP_LOGI("fetch", "Fetch cycle dispatched");

  const auto now = id(home_time).now();

  id(fetch_deadline) = now.is_valid()
      ? static_cast<int>(now.timestamp) +
            FETCH_WATCHDOG_SECONDS
      : 0;
}

// Register one fetch about to be dispatched
inline void fetch_begin() {
  id(pending_fetches) += 1;
}

// Return one registered fetch (from on_success or
// on_error alike — a failed fetch still counts as
// finished, the panel degrades to placeholders); the
// last ticket to return redraws the display. Stale
// returns (a very late response racing a new cycle)
// are ignored via the zero floor
inline void fetch_complete() {
  if (
      id(pending_fetches) > 0 &&
      --id(pending_fetches) == 0
  ) {
    id(fetch_deadline) = 0;
    id(epaper_display).update();
  }
}

// Watchdog tick (5 s interval): force the redraw when
// tickets are still outstanding past the deadline
inline void fetch_watchdog_tick() {
  if (
      id(fetch_deadline) == 0 ||
      id(pending_fetches) == 0
  ) {
    return;
  }

  const auto now = id(home_time).now();

  if (
      !now.is_valid() ||
      now.timestamp < id(fetch_deadline)
  ) {
    return;
  }

  ESP_LOGW(
      "fetch",
      "Watchdog: forcing redraw with %d fetch(es) "
      "outstanding",
      id(pending_fetches)
  );

  id(pending_fetches) = 0;
  id(fetch_deadline) = 0;
  id(epaper_display).update();
}
