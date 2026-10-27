# Test plan

How to verify the dashboard's display paths — most scenarios need
no Home Assistant wiring at all thanks to demo mode. Scenario 1 is
the known-good reference; work down the matrix after any layout or
unit-handling change.

## Demo mode

Set `demo_mode: 'true'` in the substitutions, flash, and the display
fills from canned data (`demo_helpers.h`): 4 hourly slots, today's
low/high/max UV, and five calendar events spread over four days.
No `weather.*` entity, calendar, or API connection is needed — the
fetch cycle is bypassed entirely (its tickets and watchdog are unused
on this path). The `temperature_unit` substitution decides whether
the demo values read as °C or °F.

## Test matrix

| # | Scenario | Config | Expected |
|---|---|---|---|
| 1 | Reference | defaults (Met.no, °C, two calendars) | Known-good layout; every row populated; rows aligned across panels |
| 2 | Demo, no HA | `demo_mode: 'true'` | Full layout from canned data; header shows the canned status (`demo_status`); battery shows real level (ADC works standalone) |
| 2b | Demo, missing fields | `demo_mode: 'true'` + `demo_missing: 'uv,humidity,wind,low,high,uvmax,precipitation'` | The named fields are blank; with `hide_missing: 'true'` their rows collapse (dynamic layout), with `'false'` they show `--` placeholders — selective gaps without any HA wiring, unlike scenario 6 (a nonexistent entity blanks everything at once). Exceptions: the current temperature/icon are not blankable (not in the field list), feels-like degrades to the temperature when its inputs are blank (no formula applies in the 10–27° band), and the low/high row always renders (`--°`) as the row grid's spine |
| 2c | Demo, no calendars | `demo_mode: 'true'` + `demo_missing: 'calendars'` | Calendar panel absent; weather fills the full content width (aligned with the header box), current-weather group centered, hourly strip showing all eight 2-hour slots (same as all calendars disabled — or a genuinely quiet week — in production) |
| 3 | Demo, °F | `demo_mode: 'true'` + `temperature_unit: '°F'` | °F suffixes everywhere; feels-like plausible as a °F wind chill (demo values are treated as °F) |
| 3b | Substitution/entity unit mismatch | `temperature_unit: '°F'` with a °C entity (or vice versa) | Boot frame renders in the substitution's unit; once the entity's `temperature_unit` attribute arrives, the next redraw corrects values to the entity's unit — the attribute always wins over the substitution. Only the exposed Feels Like sensor's unit label keeps the wrong unit until the substitution matches. Validated (involuntarily) alongside scenario 3 |
| 4 | Real °F | HA unit system → US customary (Settings → System → General), wait for the weather entity to switch, refresh | °F suffixes; wind in mph; feels-like in °F; low/high in °F. Also exercises the mph → km/h normalization |
| 5 | No calendars | `calendar_1_enabled`/`calendar_2_enabled` → `'false'` | Weather panel across the full content width, current-weather group centered, hourly strip showing all eight 2-hour slots, no right panel; log shows no calendar fetch tickets |
| 6 | Missing weather entity | `weather_entity: weather.does_not_exist` | One fetch error log per cycle (expected); core placeholders (`--°`); humidity/wind/UV rows hidden (`hide_missing: 'true'`); hourly strip absent; calendar unaffected; display still redraws |
| 6b | Missing fields, placeholder mode | as 6, `hide_missing: 'false'` | Same, but `--` placeholder rows drawn instead of collapsing |
| 7 | Missing calendar entity | `calendar_1_entity: calendar.does_not_exist` (enabled) | One fetch error log per cycle; other calendar still fills the panel; the split layout is kept (a failed fetch is not a quiet week) |
| 7b | All calendar fetches failing | both calendar entities → nonexistent (enabled) | Fetch error logs each cycle; the calendar panel stays in place, empty, and the layout does **not** flip to full-width weather — the flip requires a settled cycle where every dispatched calendar answered (the demo loader marks its canned data as settled, so scenario 2c still flips) |

> **Building the demo variants:** if you keep the demo builds as separate YAML copies (rather than flipping `demo_mode` in place), make sure they do not share the `esphome: name:` with the production build you intend to flash next — ESPHome keys its build directory by the device name, so the variants overwrite each other's binaries, and a stale build cache can then upload the previously built variant's firmware as the other config. Run `esphome clean <config>` (or delete `.esphome/build/<name>/`) before rebuilding a config after another same-name build, and verify the generated `src/main.cpp` (e.g. the `demo_mode_enabled` global's initializer) when in doubt. This bit us on real hardware — a "production" flash that happily rendered canned data.
| 8 | Third/fourth calendar | point `calendar_3_entity` at a real calendar, enable | Events merged into the panel; duplicates of events shared with calendar 1/2 collapse |
| 9 | Boot sequence | power-cycle, watch the first ~60 s | First draw shows placeholders (no data yet); everything filled from the first successful cycle; no `--` lingers past it |
| 10 | Degree symbols | any scenario | Exactly one `°` per temperature value, exactly one unit suffix per row (regression: a doubled `°°` shipped once) |
| 11 | Row alignment per condition | clear-night vs sunny vs cloudy (icon heights differ 74–99 px) | Low/high, feels-like/UV, humidity, wind stay on the shared row grid; left/right panels' rows line up |
| 12 | Watchdog | not routinely testable without a hung fetch | Forced redraw 20 s after a cycle start with tickets outstanding — verify via logs if a fetch ever hangs in practice |
| 13 | Battery saver, powered | device plugged in (saver is on by default) | Never sleeps (External Power sensor on); everything behaves as before — a wall-powered dashboard should not notice the saver at all |
| 14 | Battery saver, on battery | unplugged (saver is on by default) | Refresh cycle completes, device sleeps after the 30 s window; wakes every ~15 min and redraws; HA sees it offline between wakes; image persists on the e-ink |
| 15 | Button wake | press any button while asleep | Device wakes; `Wake button press pending replay` logs when HA's states arrive, then a tick later `Replaying button press(es)` fires the matching type on the "E1001 Buttons" event entity — HA's *Event received* automation trigger (with the type) fires, the entity's timestamp updates; then the refresh + the longer interactive window. The old binary-sensor entities are gone (internal); rewiring note: use the Event received trigger, not a state trigger on the event entity (state triggers miss consecutive same-type events) |
| 16 | Boot hatch | hold green ~5 s after a wake | Log shows the hatch; device stays awake for the session (OTA possible) |
| 17 | Charger detection | watch the External Power sensor while (un)plugging | Flips within ~5 s on boards where the charger answers on I2C. On the observed silent batch (no ACK even powered), power is detected only via the ≥ 4.15 V pack-voltage tie-break — with USB attached and a partially charged pack the sensor legitimately stays off and the device sleep-cycles; use the keep-awake helper or `battery_saver: 'false'` for interactive sessions (see the README caveat) |
| 18 | Sleep current | multimeter in series while asleep | Low enough to be worth it (see the checklist in [battery-saver-plan.md](battery-saver-plan.md)) |
| 19 | Keep-awake helper, while awake | create `input_boolean.e1001_keep_awake` (or your `keep_awake_entity`), set it on while on battery | Device does not sleep at the deadline; turning it off lets the next deadline apply |
| 20 | Keep-awake helper, while asleep | set the helper while the device is asleep | Takes effect at the next wake (deep sleep is unreachable); that wake stays awake until it is turned off |
| 21 | HA unreachable on battery | battery saver on, router/HA down, watch the log | Device sleeps after ~2 minutes with a "no clock sync" warning instead of staying awake forever; boot hatch overrides for debugging |

Scenarios 2/3/5/6/6b/7 are pure device-side (flash + watch);
scenario 4 needs HA; scenario 1/8/9/10/11 are best eyeballed against
the reference.

## What demo mode does not cover

- Real attribute pushes (the slot-0 fallbacks exist precisely for
  their arrival timing — see the README data-flow section).
- Wind units other than km/h (demo data assumes km/h; the mph path
  is covered by scenario 4).
- The live attribute/forecast consistency over time.

## CI

`.github/workflows/ci.yml` compiles the standard **and** the demo
variant against a pinned ESPHome version and `latest` — the demo
variant compile is how scenario 2's code path stays green without
hardware.
