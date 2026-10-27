#pragma once

#include "weather_helpers.h"
#include "calendar_helpers.h"

#include <algorithm>
#include <string>


// ==================================================
// Demo mode
// ==================================================

// Fills the dashboard with canned data instead of the
// Home Assistant fetches, so the layout can be tried
// with no HA wiring at all (see docs/testing.md).
// Enabled via the demo_mode substitution in the YAML;
// the values are in "entity units" (°C, km/h) unless
// the temperature_unit substitution says °F — the
// display and the feels-like formulas then treat them
// as such

// Local "YYYY-MM-DD" of today plus the given offset;
// fixed dates when the clock is not yet synced
inline std::string demo_date(int days_ahead) {
  const auto now = id(home_time).now();

  // Sized for the theoretical worst case of each
  // directive (GCC checks format truncation against
  // the int argument ranges, not the calendar-valid
  // ones)
  char text[16];

  if (now.is_valid()) {
    const auto local = esphome::ESPTime::from_epoch_local(
        now.timestamp +
        static_cast<time_t>(days_ahead) * 24 * 3600
    );

    snprintf(
        text,
        sizeof(text),
        "%04d-%02d-%02d",
        local.year,
        local.month,
        local.day_of_month
    );
  } else {
    snprintf(
        text,
        sizeof(text),
        "2026-06-%02d",
        std::clamp(1 + days_ahead, 1, 28)
    );
  }

  return text;
}

// Local "HH:MM" of now plus the given hours
inline std::string demo_time(int hours_ahead) {
  const auto now = id(home_time).now();

  // Hour/minute are single bytes; 9 covers the
  // 3-digit worst case plus the colon
  char text[9];

  if (now.is_valid()) {
    const auto local = esphome::ESPTime::from_epoch_local(
        now.timestamp +
        static_cast<time_t>(hours_ahead) * 3600
    );

    snprintf(
        text,
        sizeof(text),
        "%02d:%02d",
        local.hour,
        local.minute
    );
  } else {
    snprintf(
        text,
        sizeof(text),
        "12:00"
    );
  }

  return text;
}

// Whether the demo_missing substitution (a comma-
// separated list, no spaces) names this field — the
// loader then blanks it so the dynamic layout (hidden
// rows with hide_missing, placeholders otherwise) can
// be tested with no HA wiring at all. Exact token
// matching, so "uv" does not match "uvmax". The list is
// read from the demo_missing_list YAML global —
// substitutions do not expand inside include files, so
// the header cannot see ${demo_missing} directly (the
// same bridge as demo_mode_enabled)
inline bool demo_field_missing(const char *field) {
  static const std::string list =
      id(demo_missing_list);

  size_t pos = 0;

  while (pos <= list.size()) {
    const size_t comma =
        list.find(',', pos);
    const size_t end =
        (comma == std::string::npos)
            ? list.size()
            : comma;

    if (
        list.compare(
            pos, end - pos, field
        ) == 0
    ) {
      return true;
    }

    if (comma == std::string::npos) {
      break;
    }

    pos = comma + 1;
  }

  return false;
}

inline void load_demo_data() {
  // Hourly slots, mirroring the real fetch shape: the
  // union of both strip grids (+0/+3/+6/+9 for the
  // split column, +0/+2/+4/…/+14 for the wide
  // no-calendar layout), each tagged with its hour
  // offset like the fetch template does. Slot +0
  // doubles as the current values via the display
  // fallbacks. Fields named in the demo_missing
  // substitution are left NAN/empty, so the dynamic
  // layout reacts exactly as it would to a provider
  // that lacks them
  forecast_slots.clear();

  const bool no_uv = demo_field_missing("uv");
  const bool no_humidity =
      demo_field_missing("humidity");
  const bool no_wind = demo_field_missing("wind");
  const bool no_precipitation =
      demo_field_missing("precipitation");

  const int hours[10] = {
      0, 2, 3, 4, 6, 8, 9, 10, 12, 14
    };

  const char *conditions[10] = {
      // +0        +2      +3     +4
      "partlycloudy", "cloudy", "rainy", "rainy",
      // +6     +8     +9           +10
      "sunny", "sunny", "clear-night", "clear-night",
      // +12          +14
      "clear-night", "clear-night"
    };

  const float temperatures[10] = {
      // +0  +2  +3  +4  +6  +8  +9  +10 +12 +14
      21.0f, 20.0f, 18.0f, 19.0f, 23.0f, 22.0f,
      15.0f, 14.0f, 13.0f, 12.0f
    };

  const float precipitation[10] = {
      10.0f, 20.0f, 70.0f, 40.0f,
      0.0f, 0.0f, 0.0f, 5.0f, 0.0f, 0.0f
    };

  for (int i = 0; i < 10; i++) {
    ForecastSlot slot;

    slot.hours_ahead = hours[i];
    slot.time = demo_time(hours[i]);
    slot.condition = conditions[i];
    slot.temperature = temperatures[i];
    slot.precipitation_probability =
        no_precipitation
            ? NAN
            : precipitation[i];
    slot.uv_index =
        (hours[i] == 0 && !no_uv) ? 2.4f : NAN;
    slot.humidity =
        (hours[i] == 0 && !no_humidity) ? 74.0f : NAN;
    slot.wind_speed =
        (hours[i] == 0 && !no_wind) ? 12.5f : NAN;

    forecast_slots.push_back(slot);
  }

  // Today's low/high and max UV
  daily_low =
      demo_field_missing("low") ? NAN : 5.0f;
  daily_high =
      demo_field_missing("high") ? NAN : 12.0f;
  daily_uv_max =
      demo_field_missing("uvmax") ? NAN : 3.1f;

  // A spread of events for the calendar panel:
  // timed, all-day, across several days; "calendars"
  // in demo_missing empties the list entirely, which
  // switches the layout to full-width weather (same as
  // disabling all calendars in production)
  calendar_events.clear();

  if (!demo_field_missing("calendars")) {
    CalendarEvent event;

    event.date = demo_date(0);
    event.all_day = false;
    event.start_time = "09:00";
    event.end_time = "09:30";
    event.summary = "Standup";
    calendar_events.push_back(event);

    event.date = demo_date(0);
    event.all_day = true;
    event.start_time = "";
    event.end_time = "";
    event.summary = "Maria's birthday";
    calendar_events.push_back(event);

    event.date = demo_date(1);
    event.all_day = false;
    event.start_time = "18:00";
    event.end_time = "21:00";
    event.summary = "Dinner with the neighbours";
    calendar_events.push_back(event);

    event.date = demo_date(3);
    event.all_day = false;
    event.start_time = "07:15";
    event.end_time = "08:00";
    event.summary = "Dentist";
    calendar_events.push_back(event);

    event.date = demo_date(3);
    event.all_day = true;
    event.start_time = "";
    event.end_time = "";
    event.summary = "Trip to Gothenburg";
    calendar_events.push_back(event);
  }

  // The demo loader acts as one settled, successful
  // calendar fetch cycle for the layout switch (no real
  // fetches run in demo mode, so the counters would
  // otherwise read "nothing ran" and pin the split
  // layout): with "calendars" blanked, the emptiness is
  // the settled kind and the layout flips to full-width
  // weather — see calendar_cycle_settled()
  calendar_fetches_dispatched = 1;
  calendar_fetches_succeeded = 1;

  const auto now = id(home_time).now();

  if (now.is_valid()) {
    set_last_refresh(now);
  }

  ESP_LOGI(
      "demo",
      "Loaded demo data: %d slots, %d events",
      static_cast<int>(forecast_slots.size()),
      static_cast<int>(calendar_events.size())
  );
}
