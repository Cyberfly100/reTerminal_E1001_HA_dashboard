#pragma once

#include "esphome.h"
#include "display_helpers.h"

// ==================================================
// External power detection (SY6974B charger)
// ==================================================

// The board's Silergy SY6974B battery charger sits on
// I2C1 (SDA GPIO39, SCL GPIO40) at address 0x6B per
// the schematic and the Zephyr board files. Its
// "VINDPM/IINDPM and interrupt mask" register (0x0A)
// reports a valid input source in bit 7 (BUS_GD) —
// the same bit the Zephyr silergy,sy6974b driver uses
// for its "online" property.
//
// Empirically (E1001, on battery): the charger does
// NOT answer on that bus while running from the
// battery — the I2C scan finds nothing unplugged —
// so a clean NACK itself is treated as "on battery",
// with the pack voltage as tie-break for boards where
// the charger never answers at all
constexpr uint8_t CHARGER_I2C_ADDRESS = 0x6B;
constexpr uint8_t CHARGER_REG_BUS = 0x0A;
constexpr uint8_t CHARGER_BUS_GD = 0x80;

// Pack voltage at or above which a silent charger is
// assumed to be sitting on external power. Empirical
// calibration point (E1001, charging at 87 % SOC): the
// pack reads 4.02 V on USB; a resting pack only reads
// >= 4.15 at essentially full charge, so false
// "powered" only delays the first sleep after a full
// charge — the safe direction. The honest limitation:
// charging an emptier pack reads below this threshold,
// so the device may sleep-cycle early in a charge
// (it still wakes every battery_sleep_seconds, and
// the keep-awake helper or the boot hatch override
// for interactive use while charging)
constexpr float CHARGER_SILENT_POWERED_VOLTS = 4.15f;

// Cached input-source state: 1 = external power,
// 0 = battery, -1 = not determined (or bus error).
// Unknown deliberately counts as powered: a flaky
// I2C bus must never put a plugged-in device to sleep
inline int external_power_state = -1;

// One charger status read attempt on a bus. Returns
// 0 = the charger answered (powered set from BUS_GD),
// 1 = clean NACK (nothing at the address), -1 = bus
// error (treated as unknown)
inline int charger_read_once(
    i2c::I2CBus &bus,
    bool &powered
) {
  uint8_t value = 0;
  const uint8_t reg = CHARGER_REG_BUS;

  const i2c::ErrorCode error =
      bus.write_readv(
          CHARGER_I2C_ADDRESS,
          &reg,
          1,
          &value,
          1
      );

  if (error == i2c::ERROR_OK) {
    powered = (value & CHARGER_BUS_GD) != 0;
    return 0;
  }

  if (error == i2c::ERROR_NOT_ACKNOWLEDGED) {
    return 1;
  }

  return -1;
}

// Refresh the cached input-source state; call this
// from a slow interval (5 s), not from a loop-rate
// template lambda. Tries the charger bus first, then
// the sensor bus (the charger might be routed there
// on other board revisions), and interprets silence:
// - answered → BUS_GD decides
// - NACK everywhere → on battery (the charger's
//   digital side appears unpowered while running
//   unplugged), unless the pack sits at charging
//   voltage — then assume powered
// - bus error → unknown, fail-safe to powered
inline void read_external_power() {
  bool powered = false;

  const int charger_bus =
      charger_read_once(id(charger_i2c), powered);

  if (charger_bus == 0) {
    external_power_state = powered ? 1 : 0;
    return;
  }

  const int sensor_bus =
      charger_read_once(id(sensor_i2c), powered);

  if (sensor_bus == 0) {
    external_power_state = powered ? 1 : 0;
    return;
  }

  if (
      charger_bus == 1 &&
      sensor_bus == 1
  ) {
    const float voltage = id(battery_voltage).state;

    external_power_state =
        (!isnan(voltage) &&
         voltage >= CHARGER_SILENT_POWERED_VOLTS)
            ? 1
            : 0;

    return;
  }

  static bool logged = false;

  if (!logged) {
    ESP_LOGW(
        "power",
        "Charger read failed; assuming "
        "external power"
    );

    logged = true;
  }

  external_power_state = -1;
}

// True while an input source is present; unknown
// (never read, or the charger does not answer) also
// counts as present — the fail-safe direction for
// the battery saver's sleep decision
inline bool external_power_present() {
  return external_power_state != 0;
}

// ==================================================
// Battery level
// ==================================================

// Battery percentage at or below which the
// warning screen replaces the dashboard
constexpr float BATTERY_LOW_THRESHOLD = 20.0f;

inline bool battery_is_low() {
  const float level = id(battery_level).state;

  // No reading yet (boot) - assume healthy
  if (isnan(level)) {
    return false;
  }

  return level <= BATTERY_LOW_THRESHOLD;
}

inline void draw_battery_warning(display::Display &it) {
  const int w = it.get_width();

  // Large empty battery icon
  it.print(
      w / 2,
      20,
      &id(font_mdi_battery),
      display::TextAlign::TOP_CENTER,
      "\U000F008E"
  );

  it.print(
      w / 2,
      255,
      &id(Montserrat_large),
      display::TextAlign::TOP_CENTER,
      "Battery low"
  );

  it.print(
      w / 2,
      400,
      &id(Inter_medium),
      display::TextAlign::TOP_CENTER,
      "Please charge the device"
  );

  char stats[24];

  snprintf(
      stats,
      sizeof(stats),
      "%.0f%% (%.2f V)",
      id(battery_level).state,
      id(battery_voltage).state
  );

  it.print(
      w / 2,
      437,
      &id(Inter_medium),
      display::TextAlign::TOP_CENTER,
      stats
  );
}

// Battery icon matched to the current level
inline const char *battery_glyph(float level) {
  if (level <= 10.0f) {
    return "\U000F007A";        // battery-10
  } else if (level <= 20.0f) {
    return "\U000F007B";        // battery-20
  } else if (level <= 30.0f) {
    return "\U000F007C";        // battery-30
  } else if (level <= 40.0f) {
    return "\U000F007D";        // battery-40
  } else if (level <= 50.0f) {
    return "\U000F007E";        // battery-50
  } else if (level <= 60.0f) {
    return "\U000F007F";        // battery-60
  } else if (level <= 70.0f) {
    return "\U000F0080";        // battery-70
  } else if (level <= 80.0f) {
    return "\U000F0081";        // battery-80
  } else if (level <= 90.0f) {
    return "\U000F0082";        // battery-90
  }

  return "\U000F0079";          // battery (100)
}

// Width of the battery status group in the header,
// for balancing the title between it and the refresh
// indicator; 0 while the group is hidden
inline int battery_status_width(display::Display &it) {
  const float level = id(battery_level).state;

  // Hidden at full charge or before the first reading
  if (isnan(level) || level >= 100.0f) {
    return 0;
  }

  char text[8];

  snprintf(
      text,
      sizeof(text),
      "%.0f%%",
      level
  );

  return
      measure_text(
          it,
          battery_glyph(level),
          &id(font_mdi_small)
      ).width +
      2 +
      measure_text(
          it,
          text,
          &id(Inter_medium)
      ).width;
}

// Small battery icon matched to the current level,
// plus the percentage, right-aligned in the header
// bar; hidden while the level is 100% or unknown
inline void draw_battery_status(
    display::Display &it,
    int right_x,
    int bar_y,
    int bar_height
) {
  const float level = id(battery_level).state;

  // Hidden at full charge or before the first reading
  if (isnan(level) || level >= 100.0f) {
    return;
  }

  // Icon showing the current battery level
  const char *glyph = battery_glyph(level);

  char text[8];

  snprintf(
      text,
      sizeof(text),
      "%.0f%%",
      level
  );

  const Size icon_size =
      measure_text(
          it,
          glyph,
          &id(font_mdi_small)
      );

  const Size text_size =
      measure_text(
          it,
          text,
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
      right_x -
          icon_size.width - 2 -
          text_size.width,
      y,
      glyph,
      &id(font_mdi_small),
      text,
      &id(Inter_medium),
      2,
      false
  );
}
