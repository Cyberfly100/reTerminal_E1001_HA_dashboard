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

inline void load_demo_data() {
  // Hourly slots (+0/+3/+6/+9), mirroring the real
  // fetch shape; slot 0 doubles as the current values
  // via the display fallbacks
  forecast_slots.clear();

  const char *conditions[4] = {
      "partlycloudy",
      "rainy",
      "sunny",
      "clear-night"
  };

  const float temperatures[4] = {
      21.0f, 18.0f, 23.0f, 15.0f
  };

  const float precipitation[4] = {
      10.0f, 70.0f, 0.0f, 0.0f
  };

  for (int i = 0; i < 4; i++) {
    ForecastSlot slot;

    slot.time = demo_time(3 * i);
    slot.condition = conditions[i];
    slot.temperature = temperatures[i];
    slot.precipitation_probability = precipitation[i];
    slot.uv_index = (i == 0) ? 2.4f : NAN;
    slot.humidity = (i == 0) ? 74.0f : NAN;
    slot.wind_speed = (i == 0) ? 12.5f : NAN;

    forecast_slots.push_back(slot);
  }

  // Today's low/high and max UV
  daily_low = 5.0f;
  daily_high = 12.0f;
  daily_uv_max = 3.1f;

  // A spread of events for the calendar panel:
  // timed, all-day, across several days
  calendar_events.clear();

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
