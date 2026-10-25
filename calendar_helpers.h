#pragma once

#include "display_helpers.h"

#include <algorithm>
#include <string>


// ==================================================
// Calendar event data
// ==================================================

struct CalendarEvent {
  std::string date;        // "YYYY-MM-DD", local start date
  bool all_day;            // true for all-day events
  std::string start_time;  // "HH:MM", empty for all-day events
  std::string end_time;    // "HH:MM", empty for all-day events
  std::string summary;
};

// Calendar events, filled by the calendar actions in
// refresh_dashboard in the YAML. Lives here as a plain
// C++ global because ESPHome generates globals: storage
// before including this file, so a custom type cannot
// be used in a globals: entry.
inline std::vector<CalendarEvent> calendar_events;


// ==================================================
// Calendar event parsing (one merged calendar fetch)
// ==================================================

// Parses the response of one calendar.get_events call
// and appends the events; the caller clears the list
// once per refresh before any calendar action runs.
// start/end are either ISO 8601 strings
// ("2026-09-17T16:00:00+02:00", or "2026-09-18" for
// all-day) or {"dateTime": ...}/{"date": ...} objects,
// depending on the HA version
inline void parse_calendar_events(JsonArrayConst events) {
  int count = 0;

  for (JsonVariantConst item : events) {
    CalendarEvent event;

    std::string start;
    std::string end;

    if (item["start"].is<const char *>()) {
      start =
          item["start"].as<std::string>();
    } else if (
        item["start"]["date"].is<const char *>()
    ) {
      start =
          item["start"]["date"].as<std::string>();
    } else if (
        item["start"]["dateTime"].is<const char *>()
    ) {
      start =
          item["start"]["dateTime"].as<std::string>();
    }

    if (item["end"].is<const char *>()) {
      end =
          item["end"].as<std::string>();
    } else if (
        item["end"]["date"].is<const char *>()
    ) {
      end =
          item["end"]["date"].as<std::string>();
    } else if (
        item["end"]["dateTime"].is<const char *>()
    ) {
      end =
          item["end"]["dateTime"].as<std::string>();
    }

    if (start.empty()) {
      continue;
    }

    event.date = start.substr(0, 10);

    event.all_day = start.size() <= 10;

    if (start.size() >= 16) {
      event.start_time =
          start.substr(11, 5);
    }

    if (end.size() >= 16) {
      event.end_time =
          end.substr(11, 5);
    }

    event.summary =
        item["summary"].as<std::string>();

    calendar_events.push_back(event);
    count++;
  }

  ESP_LOGI(
      "calendar",
      "Retrieved %d events",
      count
  );
}


// ==================================================
// Merged, deduplicated event list
// ==================================================

inline bool calendar_event_before(
    const CalendarEvent &a,
    const CalendarEvent &b
) {
  if (a.date != b.date) {
    return a.date < b.date;
  }

  if (a.all_day != b.all_day) {
    return a.all_day;
  }

  return a.start_time < b.start_time;
}

// Same event in two merged calendars: identical in
// every field (same person in two birthday calendars
// etc.); only exact duplicates are removed
inline bool calendar_event_equal(
    const CalendarEvent &a,
    const CalendarEvent &b
) {
  return (
      a.date == b.date &&
      a.all_day == b.all_day &&
      a.start_time == b.start_time &&
      a.end_time == b.end_time &&
      a.summary == b.summary
  );
}

// Sorted copy of all fetched events with duplicates
// removed; sorting and deduplication happen here at
// draw time, independent of the order in which the
// individual calendar responses arrive
inline std::vector<CalendarEvent> unique_calendar_events() {
  std::vector<CalendarEvent> events = calendar_events;

  std::sort(
      events.begin(),
      events.end(),
      calendar_event_before
  );

  events.erase(
      std::unique(
          events.begin(),
          events.end(),
          calendar_event_equal
      ),
      events.end()
  );

  return events;
}


// ==================================================
// Date helpers
// ==================================================

inline bool is_leap_year(int year) {
  return (
      (year % 4 == 0 && year % 100 != 0) ||
      (year % 400 == 0)
  );
}

inline int day_of_year(
    int year,
    int month,
    int day
) {
  static const int cumulative[12] = {
      0, 31, 59, 90, 120, 151,
      181, 212, 243, 273, 304, 334
  };

  int doy = cumulative[month - 1] + day;

  if (month > 2 && is_leap_year(year)) {
    doy += 1;
  }

  return doy;
}

// ISO weekday, 1 = Monday .. 7 = Sunday
// (Sakamoto's algorithm)
inline int iso_weekday(
    int year,
    int month,
    int day
) {
  static const int offsets[12] = {
      0, 3, 2, 5, 0, 3,
      5, 1, 4, 6, 2, 4
  };

  const int y =
      month < 3 ? year - 1 : year;

  const int weekday =
      (
          y + y / 4 - y / 100 + y / 400 +
          offsets[month - 1] + day
      ) % 7;

  return weekday == 0 ? 7 : weekday;
}

// ISO 8601 week number
inline int iso_week_number(
    int year,
    int month,
    int day
) {
  const int week =
      (
          day_of_year(year, month, day) -
          iso_weekday(year, month, day) + 10
      ) / 7;

  if (week < 1) {
    // Early January dates that belong to the
    // last week of the previous year
    return iso_week_number(year - 1, 12, 31);
  }

  if (week == 53) {
    // Week 53 only exists if the year starts on a
    // Thursday, or on a Wednesday in a leap year
    const int jan_first = iso_weekday(year, 1, 1);

    if (
        jan_first != 4 &&
        !(is_leap_year(year) && jan_first == 3)
    ) {
      return 1;
    }
  }

  return week;
}



// ==================================================
// Calendar fetch window (ISO 8601, local + offset)
// ==================================================

// Formats an instant as a local ISO 8601 datetime with
// the device clock's UTC offset, e.g. 2026-09-15T14:30:00+02:00
inline std::string iso_local_with_offset(time_t epoch) {
  const esphome::ESPTime local =
      esphome::ESPTime::from_epoch_local(epoch);

  const esphome::ESPTime utc =
      esphome::ESPTime::from_epoch_utc(epoch);

  // Offset between the local and the UTC wall clock
  // of the same instant, clamped across a day boundary
  int offset =
      (local.hour - utc.hour) * 3600 +
      (local.minute - utc.minute) * 60 +
      (local.second - utc.second);

  if (offset > 12 * 3600) {
    offset -= 24 * 3600;
  }

  if (offset < -12 * 3600) {
    offset += 24 * 3600;
  }

  const int offset_abs = std::abs(offset);

  char text[40];

  snprintf(
      text,
      sizeof(text),
      "%04d-%02d-%02dT%02d:%02d:%02d%c%02d:%02d",
      local.year,
      local.month,
      local.day_of_month,
      local.hour,
      local.minute,
      local.second,
      offset < 0 ? '-' : '+',
      offset_abs / 3600,
      (offset_abs % 3600) / 60
  );

  return text;
}

// Start of the calendar fetch window: now
inline std::string calendar_window_start() {
  return iso_local_with_offset(
      id(home_time).now().timestamp
  );
}

// End of the calendar fetch window: now + 14 days
inline std::string calendar_window_end() {
  return iso_local_with_offset(
      id(home_time).now().timestamp +
      14LL * 24 * 3600
  );
}


// ==================================================
// Calendar
// ==================================================

inline void draw_calendar(
    display::Display &it,
    int box_x,
    int box_y,
    int box_width,
    int box_height
) {
  constexpr int SIDE_INSET = 12;
  constexpr int TOP_INSET = 10;
  constexpr int DAY_GAP = 12;
  constexpr int EVENT_ROW_GAP = 6;
  constexpr int TIME_GAP = 10;

  const std::vector<CalendarEvent> events =
      unique_calendar_events();

  if (events.empty()) {
    return;
  }


  // ------------------------------------------------
  // Measure the fonts, so the layout follows the
  // font sizes defined in the YAML
  // ------------------------------------------------

  const Size header_size =
      measure_text(
          it,
          "Tuesday, Oct 12",
          &id(Inter_medium)
      );

  const Size time_size =
      measure_text(
          it,
          "09:00 - 10:30",
          &id(Inter_tiny)
      );

  const Size summary_size =
      measure_text(
          it,
          "A",
          &id(Inter_tiny)
      );

  const int row_height =
      std::max(
          time_size.height,
          summary_size.height
      );

  const int time_column_width =
      time_size.width + TIME_GAP;

  const int left_x = box_x + SIDE_INSET;

  const int right_x =
      box_x + box_width - SIDE_INSET;

  const int bottom_y =
      box_y + box_height - TOP_INSET;

  int y = box_y + TOP_INSET;

  std::string current_date;

  for (const CalendarEvent &event : events) {
    // ------------------------------------------------
    // Day header before the first event of a day
    // ------------------------------------------------

    if (event.date != current_date) {
      // Vertical gap between days
      if (!current_date.empty()) {
        y += DAY_GAP;
      }

      // Only draw a header if at least one of its
      // events still fits on the panel
      if (
          y + header_size.height + EVENT_ROW_GAP +
              row_height >
          bottom_y
      ) {
        break;
      }

      int year;
      int month;
      int day;

      sscanf(
          event.date.c_str(),
          "%d-%d-%d",
          &year,
          &month,
          &day
      );

      // Dithered background to visually separate
      // the day headers from the event rows
      constexpr int BAND_INSET = 4;
      constexpr int BAND_PADDING = 4;
      constexpr int BAND_RADIUS = 10;

      grey_rectangle(
          it,
          box_x + BAND_INSET,
          y - BAND_PADDING,
          box_width - 2 * BAND_INSET,
          header_size.height + 2 * BAND_PADDING,
          BAND_RADIUS
      );

      char header_text[32];

      snprintf(
          header_text,
          sizeof(header_text),
          "%s, %s %d",
          weekday_name(
              iso_weekday(year, month, day)
          ),
          month_name(month),
          day
      );

      it.print(
          left_x,
          y,
          &id(Inter_medium),
          display::TextAlign::TOP_LEFT,
          header_text
      );

      char week_text[16];

      snprintf(
          week_text,
          sizeof(week_text),
          "Week %d",
          iso_week_number(year, month, day)
      );

      it.print(
          right_x,
          y,
          &id(Inter_medium),
          display::TextAlign::TOP_RIGHT,
          week_text
      );

      y += header_size.height + EVENT_ROW_GAP;

      current_date = event.date;
    }

    if (y + row_height > bottom_y) {
      break;
    }


    // ------------------------------------------------
    // Event row: time column with the summary, or
    // just the summary for all-day events
    // ------------------------------------------------

    const bool timed =
        !event.start_time.empty() ||
        !event.end_time.empty();

    const int summary_x =
        timed ? left_x + time_column_width : left_x;

    const std::string summary =
        fit_text(
            it,
            event.summary,
            &id(Inter_tiny),
            right_x - summary_x
        );

    if (timed) {
      char time_text[16];

      if (event.start_time.empty()) {
        snprintf(
            time_text,
            sizeof(time_text),
            "%s",
            event.end_time.c_str()
        );
      } else if (event.end_time.empty()) {
        snprintf(
            time_text,
            sizeof(time_text),
            "%s",
            event.start_time.c_str()
        );
      } else {
        snprintf(
            time_text,
            sizeof(time_text),
            "%s - %s",
            event.start_time.c_str(),
            event.end_time.c_str()
        );
      }

      it.print(
          left_x,
          y,
          &id(Inter_tiny),
          display::TextAlign::TOP_LEFT,
          time_text
      );
    }

    it.print(
        summary_x,
        y,
        &id(Inter_tiny),
        display::TextAlign::TOP_LEFT,
        summary.c_str()
    );

    y += row_height + EVENT_ROW_GAP;
  }
}
