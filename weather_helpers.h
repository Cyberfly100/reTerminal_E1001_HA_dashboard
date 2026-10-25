#pragma once

#include "display_helpers.h"

#include <algorithm>
#include <cmath>
#include <cstring>


// ==================================================
// Weather condition icons
// ==================================================

inline const std::vector<const char *> &weather_condition_icons() {
  static const std::vector<const char *> icons = {
    "\U000F0594", // clear-night
    "\U000F0590", // cloudy
    "\U000F0591", // fog
    "\U000F0592", // hail
    "\U000F0593", // lightning
    "\U000F067E", // lightning-rainy
    "\U000F0595", // partlycloudy
    "\U000F0596", // pouring
    "\U000F0597", // rainy
    "\U000F0598", // snowy
    "\U000F067F", // snowy-rainy
    "\U000F0599", // sunny
    "\U000F059D", // windy
    "\U000F059E"  // windy-variant
  };

  return icons;
}


// ==================================================
// Weather icon size
// ==================================================

inline Size get_weather_icon_size(
    display::Display &it,
    display::BaseFont *font
) {
  return get_largest_icon_size(
      it,
      font,
      weather_condition_icons()
  );
}


// ==================================================
// Weather condition → MDI icon
// ==================================================

struct WeatherIconEntry {
  const char *condition;
  const char *icon;
};

inline const char *weather_icon_for_condition(
    const std::string &condition
) {
  static const WeatherIconEntry entries[] = {
    {"clear-night",     "\U000F0594"},
    {"cloudy",          "\U000F0590"},
    {"fog",             "\U000F0591"},
    {"hail",            "\U000F0592"},
    {"lightning",       "\U000F0593"},
    {"lightning-rainy", "\U000F067E"},
    {"partlycloudy",    "\U000F0595"},
    {"pouring",         "\U000F0596"},
    {"rainy",           "\U000F0597"},
    {"snowy",           "\U000F0598"},
    {"snowy-rainy",     "\U000F067F"},
    {"sunny",           "\U000F0599"},
    {"windy",           "\U000F059D"},
    {"windy-variant",   "\U000F059E"}
  };

  for (const WeatherIconEntry &entry : entries) {
    if (condition == entry.condition) {
      return entry.icon;
    }
  }

  // Fallback
  return "\U000F0590";
}


// ==================================================
// Forecast data
// ==================================================

struct ForecastSlot {
  std::string time;
  std::string condition;
  float temperature;
  // NAN while the forecast entry carried no such
  // field — unknown, distinct from a genuine 0 %
  // chance of rain
  float precipitation_probability;
  // UV index of the slot; slot 0 (the current hour)
  // doubles as the "current" UV value. NAN while the
  // forecast entry carried no uv_index
  float uv_index;
  // Humidity and wind speed (in the entity's unit) of
  // the slot; only slot 0 is used, as the fallback for
  // the corresponding live attributes before those
  // have arrived. NAN while the forecast entry
  // carried no such field
  float humidity;
  float wind_speed;
};

// Forecast slots, filled by fetch_hourly_forecast in the
// YAML. Lives here as a plain C++ global because ESPHome
// generates globals: storage before including this file,
// so a custom type cannot be used in a globals: entry.
inline std::vector<ForecastSlot> forecast_slots;

// Time of the last successful forecast refresh
// ("--:--" until then), set by fetch_hourly_forecast
inline std::string last_refresh_time = "--:--";

// Records the refresh time with the date in the
// same format as the calendar day headers,
// e.g. "Tuesday, Oct 12, 14:32"
inline void set_last_refresh(
    const esphome::ESPTime &now
) {
  // ESPTime day_of_week is 1 = Sunday .. 7 =
  // Saturday; weekday_name expects ISO 1 = Monday
  // .. 7 = Sunday
  const int weekday =
      now.day_of_week == 1 ? 7 : now.day_of_week - 1;

  char buffer[32];

  snprintf(
      buffer,
      sizeof(buffer),
      "%s, %s %d, %02d:%02d",
      weekday_name(weekday),
      month_name(now.month),
      now.day_of_month,
      now.hour,
      now.minute
  );

  last_refresh_time = buffer;
}

// Today's low/high temperatures from the daily
// forecast, set by fetch_hourly_forecast in the YAML
inline float daily_low = NAN;
inline float daily_high = NAN;

// Today's max UV index from the daily forecast; NAN
// while the forecast entry carries no uv_index
// (Met.no provides it only ~4 days ahead — always
// including today's entry)
inline float daily_uv_max = NAN;


// ==================================================
// Feels like temperature
// ==================================================

// Multiplier from the weather entity's wind speed
// unit to km/h; the unit depends on the Home
// Assistant unit system (km/h in metric, mph in US
// customary). An unknown or not-yet-arrived unit is
// treated as km/h, the metric default
inline float wind_unit_factor() {
  const std::string &unit =
      id(met_wind_speed_unit).state;

  if (unit == "m/s") {
    return 3.6f;
  }

  if (unit == "mph") {
    return 1.609344f;
  }

  return 1.0f;
}

// Live wind speed normalized to km/h
inline float wind_speed_kmh() {
  const float speed = id(met_wind_speed).state;

  if (isnan(speed)) {
    return NAN;
  }

  return speed * wind_unit_factor();
}

// True for the °F unit string; used for the initial
// value of the temp_is_fahrenheit global. A strcmp,
// not a literal == literal comparison — comparing
// two string literals has unspecified results (and
// GCC rightly warns about it)
inline bool temp_unit_is_f(const char *unit) {
  return strcmp(unit, "°F") == 0;
}

// True while the weather entity reports temperatures
// in °F. Mirrors its temperature_unit attribute via
// the met_temperature_unit text sensor; the metric
// default applies until that attribute has arrived
// (the same brief post-boot window as the wind unit)
inline bool temperature_is_fahrenheit() {
  return id(temp_is_fahrenheit);
}

// Entity-unit temperature to °C, the unit the feels
// like formulas are expressed in
inline float temp_entity_to_c(float temp) {
  if (isnan(temp)) {
    return NAN;
  }

  return temperature_is_fahrenheit()
      ? (temp - 32.0f) * (5.0f / 9.0f)
      : temp;
}

// °C back to the entity's unit for display
inline float temp_c_to_entity(float temp) {
  if (isnan(temp)) {
    return NAN;
  }

  return temperature_is_fahrenheit()
      ? temp * (9.0f / 5.0f) + 32.0f
      : temp;
}

// Unit suffix for temperature text on the display
inline const char *temperature_unit_suffix() {
  return temperature_is_fahrenheit() ? "°F" : "°C";
}

// Whether values the provider does not serve are
// hidden (rows collapse) instead of drawn as
// placeholders; set via the hide_missing substitution
inline bool hide_missing_fields() {
  return id(display_hide_missing);
}

// Apparent temperature from the two commonly used
// approximations: the Environment Canada wind chill
// formula for T <= 10 °C with wind >= 4.8 km/h, and
// the Rothfusz heat index regression for
// T >= 27 °C with humidity >= 40 %. In between (or
// with an input missing) it falls back to the air
// temperature, where neither effect is significant
inline float feels_like_from(
    float temp,
    float humidity,
    float wind_kmh
) {
  if (isnan(temp)) {
    return NAN;
  }

  const float wind = wind_kmh;

  // Environment Canada wind chill, metric form
  if (
      temp <= 10.0f &&
      !isnan(wind) &&
      wind >= 4.8f
  ) {
    const float v = powf(wind, 0.16f);

    return 13.12f +
        0.6215f * temp -
        11.37f * v +
        0.3965f * temp * v;
  }

  // Rothfusz heat index (humidity in % RH)
  if (
      temp >= 27.0f &&
      !isnan(humidity) &&
      humidity >= 40.0f
  ) {
    return -8.78469475556f +
        1.61139411f * temp +
        2.33854883889f * humidity -
        0.14611605f * temp * humidity -
        0.012308094f * temp * temp -
        0.0164248277778f * humidity * humidity +
        0.002211732f * temp * temp * humidity +
        0.00072546f * temp * humidity * humidity -
        0.000003582f *
            temp * temp * humidity * humidity;
  }

  return temp;
}

// Feels like from entity-unit inputs, in the entity's
// unit: converted to °C for the formulas above and
// back for the result, so no °F-native formula
// variants are needed
inline float feels_like_entity(
    float temp,
    float humidity,
    float wind_kmh
) {
  return temp_c_to_entity(
      feels_like_from(
          temp_entity_to_c(temp),
          humidity,
          wind_kmh
      )
  );
}

// Feels like from the live entity attributes; the
// display additionally falls back to a computation
// from the first hourly forecast slot while the
// attributes have not arrived yet
inline float feels_like_temperature() {
  return feels_like_entity(
      id(met_temperature).state,
      id(met_humidity).state,
      wind_speed_kmh()
  );
}


// ==================================================
// Current weather
// ==================================================

inline int draw_current_weather(
    display::Display &it,
    int box_x,
    int box_y,
    int box_width
) {
  constexpr int PANEL_PADDING = 8;
  constexpr int SECTION_GAP = 10;
  constexpr int ROW_PADDING = 5;
  constexpr int EDGE_MARGIN = 30;


  // ------------------------------------------------
  // Current values: the live entity attributes once
  // they have arrived, otherwise the first hourly
  // forecast slot (the current hour). The attribute
  // pushes are not tied to the refresh cycle and can
  // lag behind the first fetch-based redraws after
  // boot; the fetched slot fills the whole panel
  // from the very first successful refresh instead
  // ------------------------------------------------

  const bool have_slots = !forecast_slots.empty();

  const float current_temperature =
      !isnan(id(met_temperature).state)
          ? id(met_temperature).state
          : (have_slots
                 ? forecast_slots.front().temperature
                 : NAN);

  const float current_humidity =
      !isnan(id(met_humidity).state)
          ? id(met_humidity).state
          : (have_slots
                 ? forecast_slots.front().humidity
                 : NAN);

  const float current_wind =
      !isnan(id(met_wind_speed).state)
          ? id(met_wind_speed).state
          : (have_slots
                 ? forecast_slots.front().wind_speed
                 : NAN);

  const std::string current_condition =
      !id(met_condition).state.empty()
          ? id(met_condition).state
          : (have_slots
                 ? forecast_slots.front().condition
                 : "");

  const float current_feels_like =
      !isnan(id(met_feels_like).state)
          ? id(met_feels_like).state
          : feels_like_entity(
                current_temperature,
                current_humidity,
                isnan(current_wind)
                    ? NAN
                    : current_wind * wind_unit_factor());

  // Display unit for wind: the entity's unit, or the
  // metric default before it has arrived
  const std::string wind_unit =
      id(met_wind_speed_unit).state.empty()
          ? "km/h"
          : id(met_wind_speed_unit).state;


  // ------------------------------------------------
  // Measure the fonts, so the layout follows the
  // font sizes defined in the YAML
  // ------------------------------------------------

  const Size icon_size =
      get_weather_icon_size(
          it,
          &id(weather_icon_large)
      );

  // Formatted current temperature; measured so the
  // layout follows its actual width
  char temperature_text[8];
  char temperature_digits[8];

  if (isnan(current_temperature)) {
    snprintf(
        temperature_text,
        sizeof(temperature_text),
        "--°"
    );
    snprintf(
        temperature_digits,
        sizeof(temperature_digits),
        "--"
    );
  } else {
    snprintf(
        temperature_text,
        sizeof(temperature_text),
        "%.0f°",
        current_temperature
    );
    snprintf(
        temperature_digits,
        sizeof(temperature_digits),
        "%.0f",
        current_temperature
    );
  }

  const Size temperature_size =
      measure_text(
          it,
          temperature_text,
          &id(Montserrat_large)
      );

  const Size row_icon_size =
      get_largest_icon_size(
          it,
          &id(font_mdi_small),
          {
              "\U000F058E", // water-percent
              "\U000F059D", // weather-windy
              "\U000F0A4F", // hand (feels like)
              "\U000F04E0"  // sunglasses (UV)
          }
      );

  const Size row_text_size =
      measure_text(
          it,
          "00.0 km/h",
          &id(Inter_medium)
      );

  const int row_height =
      std::max(
          row_icon_size.height,
          row_text_size.height
      ) +
      ROW_PADDING;


  // ------------------------------------------------
  // Layout: anchored at the right edge — weather
  // icon group first, then the temperature group
  // to its left. A wider (negative) temperature
  // grows into the free space on the left instead
  // of squeezing the gap between the groups
  // ------------------------------------------------

  constexpr int GROUP_GAP = 30;

  const int icon_group_width =
      icon_size.width + 2 * PANEL_PADDING;

  const int icon_group_x =
      box_x + box_width - EDGE_MARGIN - icon_group_width;

  const int icon_center_x =
      icon_group_x + icon_group_width / 2;

  const int temperature_right_x =
      icon_group_x - GROUP_GAP;

  const int temperature_left_x =
      std::max(
          box_x + PANEL_PADDING,
          temperature_right_x - temperature_size.width
      );

  const char *weather_icon =
      weather_icon_for_condition(
          current_condition
      );

  // Measured before drawing: the row grid below is
  // anchored at the drawn icon's bottom edge, not at
  // the largest possible icon, so the gap below the
  // icon is the same for every condition
  const Size drawn_icon_size =
      measure_text(
          it,
          weather_icon,
          &id(weather_icon_large)
      );

  const int icon_y = box_y + PANEL_PADDING;

  // ------------------------------------------------
  // Shared row grid: both columns' first row (low/
  // high left, humidity right) starts one section gap
  // below the taller of the drawn weather icon and
  // the big temperature, and the temperature ends on
  // that same bottom line. The rows of the two
  // columns therefore line up for every condition
  // and temperature width, instead of each column
  // spacing from its own object above
  // ------------------------------------------------

  const int rows_y =
      std::max(
          icon_y + drawn_icon_size.height,
          box_y + PANEL_PADDING +
              temperature_size.height
      ) +
      SECTION_GAP;

  const int humidity_y = rows_y;

  const int wind_y = rows_y + row_height;

  const int content_height =
      wind_y + row_height - box_y;

  it.printf(
      icon_center_x,
      icon_y,
      &id(weather_icon_large),
      display::TextAlign::TOP_CENTER,
      "%s",
      weather_icon
  );

  // Left edge of the drawn icon, to visually
  // align the rows underneath with it
  const int icon_left_x =
      icon_center_x - drawn_icon_size.width / 2;


  // ------------------------------------------------
  // Humidity; hidden entirely (auto-hide mode) while
  // no value has arrived. Low/high and feels like
  // anchor to the shared row grid, so hiding this
  // pair leaves the grid — and with it the row
  // alignment — untouched
  // ------------------------------------------------

  const bool show_humidity =
      !hide_missing_fields() ||
      !isnan(current_humidity);

  char humidity_text[8];

  if (isnan(current_humidity)) {
    snprintf(
        humidity_text,
        sizeof(humidity_text),
        "-- %%"
    );
  } else {
    snprintf(
        humidity_text,
        sizeof(humidity_text),
        "%.0f %%",
        current_humidity
    );
  }

  if (show_humidity) {
    draw_icon_text_pair(
        it,
        icon_left_x,
        humidity_y,
        "\U000F058E", // water-percent
        &id(font_mdi_small),
        humidity_text,
        &id(Inter_medium),
        2,
        false
    );
  }


  // ------------------------------------------------
  // Wind speed; hidden entirely (auto-hide mode)
  // while no value has arrived
  // ------------------------------------------------

  const bool show_wind =
      !hide_missing_fields() ||
      !isnan(current_wind);

  char wind_text[24];

  if (isnan(current_wind)) {
    snprintf(
        wind_text,
        sizeof(wind_text),
        "--"
    );
  } else {
    snprintf(
        wind_text,
        sizeof(wind_text),
        "%.1f %s",
        current_wind,
        wind_unit.c_str()
    );
  }

  if (show_wind) {
    draw_icon_text_pair(
        it,
        icon_left_x,
        wind_y,
        "\U000F059D", // weather-windy
        &id(font_mdi_small),
        wind_text,
        &id(Inter_medium),
        2,
        false
    );
  }


  // ------------------------------------------------
  // Left group: current temperature with today's
  // low/high, feels like, and UV index
  // ------------------------------------------------

  const Size digits_size =
      measure_text(
          it,
          temperature_digits,
          &id(Montserrat_large)
      );

  // Optical center of the number: the trailing
  // degree sign is insignificant next to the huge
  // digits, so the low/high row is centered on
  // the digits instead of the full string
  const int temperature_optical_center_x =
      temperature_left_x + digits_size.width / 2;

  char low_text[8];
  char high_text[8];

  if (isnan(daily_low)) {
    snprintf(
        low_text,
        sizeof(low_text),
        "--°"
    );
  } else {
    snprintf(
        low_text,
        sizeof(low_text),
        "%.0f°",
        daily_low
    );
  }

  if (isnan(daily_high)) {
    snprintf(
        high_text,
        sizeof(high_text),
        "--°"
    );
  } else {
    snprintf(
        high_text,
        sizeof(high_text),
        "%.0f°",
        daily_high
    );
  }

  const Size arrow_size =
      get_largest_icon_size(
          it,
          &id(font_mdi_small),
          {
              "\U000F0045", // arrow-down
              "\U000F005D"  // arrow-up
          }
      );

  const Size low_text_size =
      measure_text(
          it,
          low_text,
          &id(Inter_medium)
      );

  const Size high_text_size =
      measure_text(
          it,
          high_text,
          &id(Inter_medium)
      );

  constexpr int LOWHIGH_GAP = 10;

  const int pair_width_low =
      arrow_size.width + 2 + low_text_size.width;

  const int pair_width_high =
      arrow_size.width + 2 + high_text_size.width;

  // The big temperature ends one section gap above
  // the shared row grid, on the same bottom line as
  // the drawn weather icon
  const int temperature_top_y =
      rows_y - SECTION_GAP - temperature_size.height;

  it.print(
      temperature_left_x,
      temperature_top_y,
      &id(Montserrat_large),
      display::TextAlign::TOP_LEFT,
      temperature_text
  );

  // First grid row, in line with the humidity row
  // on the right
  const int lowhigh_y = rows_y;

  const int lowhigh_x =
      temperature_optical_center_x -
      (pair_width_low + LOWHIGH_GAP + pair_width_high) / 2;

  draw_icon_text_pair(
      it,
      lowhigh_x,
      lowhigh_y,
      "\U000F0045", // arrow-down (low)
      &id(font_mdi_small),
      low_text,
      &id(Inter_medium),
      2,
      false
  );

  draw_icon_text_pair(
      it,
      lowhigh_x + pair_width_low + LOWHIGH_GAP,
      lowhigh_y,
      "\U000F005D", // arrow-up (high)
      &id(font_mdi_small),
      high_text,
      &id(Inter_medium),
      2,
      false
  );


  // ------------------------------------------------
  // Feels like and UV index, side by side in one
  // row centered under the low/high pair
  // ------------------------------------------------

  char feels_text[12];

  if (isnan(current_feels_like)) {
    snprintf(
        feels_text,
        sizeof(feels_text),
        "--%s",
        temperature_unit_suffix()
    );
  } else {
    snprintf(
        feels_text,
        sizeof(feels_text),
        "%.0f%s",
        current_feels_like,
        temperature_unit_suffix()
    );
  }

  // UV as "now/max" in one text: the current value
  // from the first hourly forecast slot (the current
  // hour) and the day's max from the daily forecast,
  // so both arrive with the same refresh cycle instead
  // of depending on the separately pushed live
  // attribute, which can lag the first redraws
  char uv_text[16];

  const float uv_now =
      forecast_slots.empty()
          ? NAN
          : forecast_slots.front().uv_index;

  if (isnan(uv_now) && isnan(daily_uv_max)) {
    snprintf(
        uv_text,
        sizeof(uv_text),
        "--"
    );
  } else if (isnan(daily_uv_max)) {
    snprintf(
        uv_text,
        sizeof(uv_text),
        "%.1f",
        uv_now
    );
  } else if (isnan(uv_now)) {
    snprintf(
        uv_text,
        sizeof(uv_text),
        "--/%.1f",
        daily_uv_max
    );
  } else {
    snprintf(
        uv_text,
        sizeof(uv_text),
        "%.1f/%.1f",
        uv_now,
        daily_uv_max
    );
  }

  const Size feels_icon_size =
      measure_text(
          it,
          "\U000F0A4F", // hand
          &id(font_mdi_small)
      );

  const Size uv_icon_size =
      measure_text(
          it,
          "\U000F04E0", // sunglasses (UV)
          &id(font_mdi_small)
      );

  const Size feels_text_size =
      measure_text(
          it,
          feels_text,
          &id(Inter_medium)
      );

  const Size uv_text_size =
      measure_text(
          it,
          uv_text,
          &id(Inter_medium)
      );

  const int feels_pair_width =
      feels_icon_size.width + 2 + feels_text_size.width;

  const int uv_pair_width =
      uv_icon_size.width + 2 + uv_text_size.width;

  // UV hidden entirely (auto-hide mode) while neither
  // the current value nor the day's max is known;
  // feels like stays as the row's only pair then
  const bool show_uv =
      !hide_missing_fields() ||
      !isnan(uv_now) ||
      !isnan(daily_uv_max);

  const int details_row_width =
      show_uv
          ? feels_pair_width +
                LOWHIGH_GAP + uv_pair_width
          : feels_pair_width;

  const int details_y = lowhigh_y + row_height;

  const int details_x =
      temperature_optical_center_x - details_row_width / 2;

  draw_icon_text_pair(
      it,
      details_x,
      details_y,
      "\U000F0A4F", // hand
      &id(font_mdi_small),
      feels_text,
      &id(Inter_medium),
      2,
      false
  );

  if (show_uv) {
    // The sunglasses glyph is much shorter than its
    // text; center it vertically against the text
    // instead of top-aligning it like the other rows
    const int uv_icon_x =
        details_x + feels_pair_width + LOWHIGH_GAP;

    const int uv_icon_offset_y =
        std::max(
            0,
            (uv_text_size.height - uv_icon_size.height) / 2
        );

    it.printf(
        uv_icon_x,
        details_y + uv_icon_offset_y,
        &id(font_mdi_small),
        display::TextAlign::TOP_LEFT,
        "%s",
        "\U000F04E0" // sunglasses (UV)
    );

    it.print(
        uv_icon_x + uv_icon_size.width + 2,
        details_y,
        &id(Inter_medium),
        display::TextAlign::TOP_LEFT,
        uv_text
    );
  }

  return box_y + content_height;
}


// ==================================================
// Hourly forecast (anchored to the given bottom y)
// ==================================================

inline void draw_hourly_forecast(
    display::Display &it,
    int x,
    int bottom_y,
    int width
) {
  constexpr int ROW_GAP = 4;

  const std::vector<ForecastSlot> &slots =
      forecast_slots;

  if (slots.empty()) {
    return;
  }


  // ------------------------------------------------
  // Measure the fonts, so the layout follows the
  // font sizes defined in the YAML
  // ------------------------------------------------

  const Size time_size =
      measure_text(
          it,
          "00:00",
          &id(Inter_medium)
      );

  const Size icon_size =
      get_weather_icon_size(
          it,
          &id(font_mdi_medium)
      );

  const Size temperature_size =
      measure_text(
          it,
          // Probe with the actual unit suffix: °F can
          // be wider than °C in Inter
          (std::string("00") + temperature_unit_suffix())
              .c_str(),
          &id(Inter_medium)
      );

  const Size precipitation_icon_size =
      measure_text(
          it,
          "\U000F054A", // umbrella
          &id(font_mdi_small)
      );

  const Size precipitation_text_size =
      measure_text(
          it,
          "100 %",
          &id(Inter_medium)
      );

  const int precipitation_height =
      std::max(
          precipitation_icon_size.height,
          precipitation_text_size.height
      );

  // The precipitation rows are hidden entirely
  // (auto-hide mode) when no slot serves the field.
  // All slots share one row grid, so the decision is
  // made for the whole strip: a lone missing value
  // keeps the row with a placeholder for that slot
  const bool show_precipitation =
      !hide_missing_fields() ||
      std::any_of(
          slots.begin(),
          slots.end(),
          [](const ForecastSlot &slot) {
            return !isnan(
                slot.precipitation_probability
            );
          }
      );

  const int total_height =
      time_size.height + ROW_GAP +
      icon_size.height + ROW_GAP +
      temperature_size.height +
      (show_precipitation
           ? ROW_GAP + precipitation_height
           : 0);

  const int time_y = bottom_y - total_height;
  const int icon_y =
      time_y + time_size.height + ROW_GAP;
  const int temperature_y =
      icon_y + icon_size.height + ROW_GAP;
  const int precipitation_y =
      temperature_y + temperature_size.height + ROW_GAP;

  const int column_width =
      width / static_cast<int>(slots.size());

  for (size_t i = 0; i < slots.size(); i++) {
    const ForecastSlot &slot = slots[i];

    const int center_x =
        x + static_cast<int>(i) * column_width +
        column_width / 2;

    // Time
    it.print(
        center_x,
        time_y,
        &id(Inter_medium),
        display::TextAlign::TOP_CENTER,
        slot.time.c_str()
    );

    // Weather icon
    it.printf(
        center_x,
        icon_y,
        &id(font_mdi_medium),
        display::TextAlign::TOP_CENTER,
        "%s",
        weather_icon_for_condition(slot.condition)
    );

    // Temperature
    char temperature_text[8];

    if (isnan(slot.temperature)) {
      snprintf(
          temperature_text,
          sizeof(temperature_text),
          "--.-%s",
          temperature_unit_suffix()
      );
    } else {
      snprintf(
          temperature_text,
          sizeof(temperature_text),
          "%.0f%s",
          slot.temperature,
          temperature_unit_suffix()
      );
    }

    it.print(
        center_x,
        temperature_y,
        &id(Inter_medium),
        display::TextAlign::TOP_CENTER,
        temperature_text
    );

    // Precipitation probability
    if (show_precipitation) {
      char probability_text[8];

      if (isnan(slot.precipitation_probability)) {
        snprintf(
            probability_text,
            sizeof(probability_text),
            "-- %%"
        );
      } else {
        snprintf(
            probability_text,
            sizeof(probability_text),
            "%.0f %%",
            slot.precipitation_probability
        );
      }

      draw_icon_text_pair(
          it,
          center_x,
          precipitation_y,
          "\U000F054A", // umbrella
          &id(font_mdi_small),
          probability_text,
          &id(Inter_medium)
      );
    }
  }
}


// ==================================================
// Last refresh indicator (for the header bar)
// ==================================================

inline void draw_last_refresh(
    display::Display &it,
    int x,
    int bar_y,
    int bar_height
) {
  const Size icon_size =
      measure_text(
          it,
          "\U000F0450",
          &id(font_mdi_small)
      );

  const Size text_size =
      measure_text(
          it,
          last_refresh_time.c_str(),
          &id(Inter_medium)
      );

  const int row_height =
      std::max(
          icon_size.height,
          text_size.height
      );

  const int y =
      bar_y + (bar_height - row_height) / 2;

  draw_icon_text_pair(
      it,
      x,
      y,
      "\U000F0450", // refresh
      &id(font_mdi_small),
      last_refresh_time.c_str(),
      &id(Inter_medium),
      2,
      false
  );
}
