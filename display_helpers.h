#pragma once

#include "esphome.h"

#include <algorithm>


// ==================================================
// Date names
// ==================================================

// ISO weekday, 1 = Monday .. 7 = Sunday
inline const char *weekday_name(int weekday) {
  static const char *names[7] = {
      "Monday",
      "Tuesday",
      "Wednesday",
      "Thursday",
      "Friday",
      "Saturday",
      "Sunday"
  };

  return names[weekday - 1];
}

inline const char *month_name(int month) {
  static const char *names[12] = {
      "Jan", "Feb", "Mar", "Apr",
      "May", "Jun", "Jul", "Aug",
      "Sep", "Oct", "Nov", "Dec"
  };

  return names[month - 1];
}


// ==================================================
// Size
// ==================================================

struct Size {
  int width;
  int height;
};


// ==================================================
// Measure text
// ==================================================

inline Size measure_text(
    display::Display &it,
    const char *text,
    display::BaseFont *font
) {
  int x1;
  int y1;
  int width;
  int height;

  it.get_text_bounds(
      0,
      0,
      text,
      font,
      display::TextAlign::TOP_LEFT,
      &x1,
      &y1,
      &width,
      &height
  );

  return {width, height};
}


// ==================================================
// Largest icon size
// ==================================================

inline Size get_largest_icon_size(
    display::Display &it,
    display::BaseFont *font,
    const std::vector<const char *> &icons
) {
  Size largest = {0, 0};

  for (const char *icon : icons) {
    const Size size =
        measure_text(it, icon, font);

    if (size.width > largest.width) {
      largest.width = size.width;
    }

    if (size.height > largest.height) {
      largest.height = size.height;
    }
  }

  return largest;
}


// ==================================================
// Dithered (grey) rectangle for 1-bit displays
// ==================================================

inline void grey_rectangle(
    display::Display &it,
    int x,
    int y,
    int width,
    int height,
    int radius = 0,
    int density = 2
) {
  int w = width;
  int h = height;

  if (radius > 0) {
    if (radius > w / 2) {
      radius = w / 2;
    }

    if (radius > h / 2) {
      radius = h / 2;
    }

    // The dot lattice samples the corner circle
    // boundary. With odd dimensions the right/bottom
    // corner centers share the lattice phase of the
    // left/top ones, so all four corners render
    // identically instead of being shifted by a pixel
    if ((w & 1) == 0) {
      w--;
    }

    if ((h & 1) == 0) {
      h--;
    }
  }

  const int right = x + w - 1;
  const int bottom = y + h - 1;

  for (int py = y; py <= bottom; py++) {
    // Aligned dot grid: one dot every density pixels
    // in both axes, anchored to the rectangle origin,
    // so the dots don't form diagonals
    if ((py - y) % density != 0) {
      continue;
    }

    for (int px = x; px <= right; px++) {
      if ((px - x) % density != 0) {
        continue;
      }

      // Skip pixels outside the rounded corners: the
      // nearest corner center is the clamped position
      if (radius > 0) {
        const int cx =
            std::clamp(
                px,
                x + radius,
                right - radius
            );

        const int cy =
            std::clamp(
                py,
                y + radius,
                bottom - radius
            );

        const int dx = px - cx;
        const int dy = py - cy;

        if (
            dx * dx + dy * dy > radius * radius
        ) {
          continue;
        }
      }

      it.draw_pixel_at(px, py);
    }
  }
}


// ==================================================
// Rounded rectangle
// ==================================================

inline void rounded_rectangle(
    display::Display &it,
    int x,
    int y,
    int width,
    int height,
    int radius
) {
  if (radius < 1) {
    radius = 1;
  }

  if (radius > width / 2) {
    radius = width / 2;
  }

  if (radius > height / 2) {
    radius = height / 2;
  }

  const int right = x + width;
  const int bottom = y + height;

  // Straight edges
  it.line(
      x + radius,
      y,
      right - radius,
      y
  );

  it.line(
      right,
      y + radius,
      right,
      bottom - radius
  );

  it.line(
      right - radius,
      bottom,
      x + radius,
      bottom
  );

  it.line(
      x,
      bottom - radius,
      x,
      y + radius
  );

  // Top-left
  it.start_clipping(
      x,
      y,
      x + radius + 1,
      y + radius + 1
  );

  it.circle(
      x + radius,
      y + radius,
      radius
  );

  it.end_clipping();

  // Top-right
  it.start_clipping(
      right - radius,
      y,
      right + 1,
      y + radius + 1
  );

  it.circle(
      right - radius,
      y + radius,
      radius
  );

  it.end_clipping();

  // Bottom-right
  it.start_clipping(
      right - radius,
      bottom - radius,
      right + 1,
      bottom + 1
  );

  it.circle(
      right - radius,
      bottom - radius,
      radius
  );

  it.end_clipping();

  // Bottom-left
  it.start_clipping(
      x,
      bottom - radius,
      x + radius + 1,
      bottom + 1
  );

  it.circle(
      x + radius,
      bottom - radius,
      radius
  );

  it.end_clipping();
}


// ==================================================
// Debug box
// ==================================================

inline void draw_icon_box(
    display::Display &it,
    int x,
    int y,
    int width,
    int height,
    bool debug = false
) {
  if (debug) {
    it.rectangle(
        x - width / 2,
        y,
        width,
        height
    );
  }
}


// ==================================================
// Icon + text pair (centered or left-aligned)
// ==================================================

inline void draw_icon_text_pair(
    display::Display &it,
    int x,
    int y,
    const char *icon,
    display::BaseFont *icon_font,
    const char *text,
    display::BaseFont *text_font,
    int gap = 2,
    bool centered = true
) {
  const Size icon_size =
      measure_text(it, icon, icon_font);

  const Size text_size =
      measure_text(it, text, text_font);

  const int total_width =
      icon_size.width + gap + text_size.width;

  const int start_x =
      centered ? x - total_width / 2 : x;

  it.printf(
      start_x,
      y,
      icon_font,
      display::TextAlign::TOP_LEFT,
      "%s",
      icon
  );

  it.print(
      start_x + icon_size.width + gap,
      y,
      text_font,
      display::TextAlign::TOP_LEFT,
      text
  );
}


// ==================================================
// Fit text (truncate with ellipsis if too wide)
// ==================================================

inline std::string fit_text(
    display::Display &it,
    const std::string &text,
    display::BaseFont *font,
    int max_width
) {
  if (
      measure_text(
          it,
          text.c_str(),
          font
      ).width <= max_width
  ) {
    return text;
  }

  std::string fitted = text;

  while (fitted.size() > 1) {
    // Drop the last UTF-8 code point
    size_t drop = 1;

    while (
        drop < fitted.size() &&
        (static_cast<unsigned char>(
             fitted[fitted.size() - drop]
         ) & 0xC0) == 0x80
    ) {
      drop++;
    }

    fitted.resize(fitted.size() - drop);

    if (
        measure_text(
            it,
            (fitted + "…").c_str(),
            font
        ).width <= max_width
    ) {
      return fitted + "…";
    }
  }

  return "…";
}
