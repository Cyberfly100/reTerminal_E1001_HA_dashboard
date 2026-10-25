# Weather data reference — `weather.forecast_home`

The dashboard's weather data comes from a single Home Assistant weather entity,
`weather.forecast_home` (Met.no integration, "Forecast Home"). This documents
which fields are actually available in each of the three data paths, based on
live inspection (Oct 2026, HA + Met.no integration) — relevant because the
standard HA weather/forecast schema does *not* include UV index; Met.no just
happens to provide it in some places.

## 1. Entity state attributes (live, "right now")

From `states.weather.forecast_home.attributes`:

| Field | Example | Used by dashboard |
|---|---|---|
| `temperature` | `10.7` | ✅ big current temperature |
| `humidity` | `79` | ✅ humidity row |
| `wind_speed` | `25.9` (km/h) | ✅ wind row |
| `uv_index` | `0.0` | — (superseded, see below) |
| `cloud_coverage` | `99.5` | — |
| `dew_point` | `7.3` | — |
| `pressure` | `1008.0` hPa | — |
| `wind_bearing` | `172.0` | — |
| `wind_gust_speed` | `47.5` | — |
| `wind_speed_unit` | `km/h` | ✅ feels-like unit normalization |
| `temperature_unit` | `°C` | ✅ °C/°F handling (feels-like conversion, display suffixes) |

Notes:

- `uv_index` here is the *live* attribute. The dashboard **no longer uses
  it for display**: the attribute push from HA is not tied to the dashboard's
  refresh cycle and could lag behind the first fetch-based redraws, leaving
  the current value empty while the rest of the panel was already filled.
  The current UV now comes from the first hourly forecast slot instead
  (same data source, same refresh cycle).
- `wind_speed_unit` depends on the HA unit system (km/h metric, mph US) —
  the feels-like calculation normalizes to km/h before applying the
  wind-chill formula.
- `temperature_unit` likewise follows the unit system (°C metric, °F US).
  The feels-like formulas are expressed in °C; a `temperature_unit`
  attribute sensor detects the entity's unit and the values are converted
  at the formula/display boundary, so °F entities work without changes
  (the exposed sensor's unit label is the one static piece — set via the
  `temperature_unit` substitution in the YAML).

## 2. `weather.get_forecasts` — type: daily

Used by the dashboard for today's low/high **and max UV**. Fields per entry:

`condition, precipitation_probability, datetime, wind_bearing, temperature
(day high), templow (day low), uv_index ✅ (first ~4 entries), wind_speed,
precipitation, humidity`

- **`uv_index` is present only for the next ~4 days** (entries 0–3); entries
  further out omit it entirely (the field disappears, not just nulls).
- Entry `[0]` is always *today*, so it always sits inside the UV horizon →
  today's max UV is reliably available from `forecast[0].uv_index`.
- `datetime` is UTC and points at roughly local noon
  (e.g. `2026-10-29T11:00:00+00:00`) — not parsed by the dashboard.

## 3. `weather.get_forecasts` — type: hourly

Used by the dashboard for the 4 hourly forecast slots (+0/+3/+6/+9 h)
**and the current UV index** (slot 0 is the current hour). Fields per entry:

`condition, precipitation_probability, datetime, cloud_coverage,
uv_index ✅, temperature, wind_gust_speed, wind_speed, precipitation, humidity`

- **`uv_index` is present in every entry** — including night hours, where it
  is correctly `0`. The horizon is ~48 hours.
- `precipitation_probability` can be absent with other providers; the
  dashboard treats *absent* as unknown (the `-1` sentinel becomes `NAN`
  in C++, distinct from a genuine `0` % chance) — with
  `hide_missing: 'true'` the row is hidden while no slot serves it.
- Entries are "next N hours from now" (not "today"), and `datetime` is UTC.
- Pre-existing quirk (unrelated to UV): at night the four display slots are
  all night hours.

## Consequence for the dashboard

| Value | Source |
|---|---|
| Current temp / condition / humidity / wind | live attribute, falling back to hourly slot `[0]` (the current hour) while the attribute has not arrived |
| Current feels like | template sensor (live attributes), falling back to a computation from hourly slot `[0]` |
| Current UV | hourly forecast slot `[0]` (the current hour) |
| Day's max UV | `forecast[0].uv_index` from the **daily** fetch |

Rationale: the live attribute pushes from HA are not tied to the dashboard's
refresh cycle and can lag behind the first fetch-based redraws after boot
(seen in practice with `uv_index` and `wind_speed`), leaving individual rows
empty while the rest of the panel was already filled. Everything the panel
shows is therefore anchored in the fetch cycle, with the attributes used as
the preferred (fresher) value when available.

Implementation (in place):

1. The hourly `response_template` extracts `uv_index`, `humidity` and
   `wind_speed` per slot (defaulting to `-1`, which becomes `NAN` in C++,
   when absent).
2. The daily `response_template` extracts `uv_max` from
   `forecast[0].uv_index`, also with the `-1` sentinel
   (NaN cannot travel through JSON).
3. The display shows `🕶 3.2/5.8` — current value, slash, day's max, all in
   the same font size ("3.2 out of 5.8"). Degraded states: only one value
   known → `3.2` or `--/5.8`; none → `--`.

If Met.no ever drops `uv_index` from either forecast, the fallback is a
template sensor in HA exposing the day's max from another source.
