# Community roadmap

Plan for turning this personal dashboard into something the wider Home
Assistant community can adopt — without turning it into a universal
framework that we end up maintaining forever.

## Scope decision

There are two products hiding in "make it useful for the community":

1. **A polished dashboard for the reTerminal E1001** — clone, point at
   your entities, flash. Low support burden, clear value.
2. **A universal e-ink HA dashboard framework** — every weather
   provider × every unit system × every display model × every calendar
   setup. An unpaid second job that dilutes what is actually good here.

We build **#1 with the cheap generalizations from #2**: entity
substitutions, dynamic calendar count, graceful degradation, °F
support, CI, demo mode. The layout is inherently device-specific
(800×480 panel grid) — that is a feature, not a liability.

## Distribution model (decided)

**Template repo**: clone → fill `secrets.yaml` → `esphome run`.

- **Not** a Home Assistant blueprint — blueprints are for HA automations
  and scripts; there is no blueprint mechanism for ESPHome firmware.
  The ESPHome equivalent of blueprint inputs is `substitutions:`.
- **Not** a `packages:` include — the `esphome: includes:` helper
  headers are local files and cannot be pulled in remotely; a repo
  users clone is the honest shape of this project.

## Phase 1 — decoupling (done)

Make the config follow the user's setup instead of assuming ours.

1. **Dynamic fetch bookkeeping** — replace the hardcoded
   `pending_fetches = 4` with per-action registration:
   - every fetch action calls `fetch_begin()` right before it runs and
     `fetch_complete()` from both its `on_success` and `on_error`,
   - the ticket count therefore always matches the actions actually
     executed; adding, removing or gating a fetch needs no changes
     anywhere else,
   - the last ticket to finish triggers the redraw.
2. **Fetch watchdog** — a single hung action must not freeze the
   dashboard until the next 300 s cycle: a deadline (20 s) is armed at
   each cycle start; a 5 s interval forces the redraw (with a warning
   log) if tickets are still outstanding past it.
3. **Substitutions** for everything a new user must point at their own
   HA instance: `weather_entity`, `calendar_1_entity` /
   `calendar_2_entity` with `calendar_1_enabled` / `calendar_2_enabled`
   toggles, `dashboard_status_entity`.

**Acceptance:** compiles on ESPHome 2026.4.5 and 2026.10.0; behavior
unchanged for the current setup.

**Status:** done — `fetch_helpers.h` (tickets + watchdog),
`fetch_cycle_begin()` / `fetch_begin()` / `fetch_complete()` /
`fetch_watchdog_tick()` wired into `refresh_dashboard`, substitutions
in place, verified on both ESPHome versions.

## Phase 2 — display configurability (done)

1. **Auto-hide missing fields** — done. Rows whose field the provider
   does not serve collapse instead of showing `--` placeholders, gated
   by the `hide_missing` substitution (`'true'` default):
   - UV missing (neither current nor day's max) → feels-like pair alone
     in its row,
   - humidity/wind missing → those pairs under the condition icon
     vanish; low/high and feels-like stay anchored to the shared row
     grid, so alignment is untouched,
   - precipitation probability: `default(0)` in the response template
     became the `-1` → `NAN` sentinel, so *unknown* is now distinct
     from a genuine 0 % chance of rain; the row is hidden strip-wide
     when no slot serves it (a per-slot decision would break the four
     slots' shared grid).
2. **°F support** — done. Auto-detected from the entity's `temperature_unit`
   attribute (a `met_temperature_unit` text sensor sets a
   `temp_is_fahrenheit` global, same pattern as `wind_speed_unit`), with
   conversion at the formula/display boundary (`temp_entity_to_c` /
   `temp_c_to_entity`) rather than the °F-native formula variants
   originally considered — one conversion point instead of four formula
   duplications, and the display suffixes follow automatically. The one
   static piece is the exposed Feels Like sensor's unit label, driven by
   a `temperature_unit` substitution ('°C' default, US users set '°F');
   ESPHome entity units are fixed at compile time, so a runtime label
   change would not propagate to HA.
3. **Calendar count beyond 2** — done. `calendar_3_entity`/
   `calendar_4_entity` (+ enable toggles) follow the same mechanical
   fetch-block pattern — deliberate duplication, since the
   `response_template` keys can't be parameterized in a single reusable
   action block; all merge/dedup/ticket logic is shared. With all four
   calendars disabled, the weather panel widens to a centered 55 % box
   and the hourly strip spans the full content width.

## Phase 3 — packaging (mostly done)

1. **Adoption README pass** — done, except the photo: quickstart,
   `demo_mode` pointer, [testing matrix](testing.md) link, provider
   field matrix link, credits (formula sources, font licenses).
   **Remaining:** a photo of the physical device — the #1 adoption
   driver; and the quickstart needs the repo URL once published.
2. **`secrets.yaml.sample`** — done; throwaway but format-valid
   values, so CI (and new users) build with it directly.
3. **Demo mode** — done. `demo_mode: 'true'` fills the display from
   canned data (`demo_helpers.h`) with the fetch cycle bypassed
   entirely; date-aware calendar events when the clock is synced.
   The `temperature_unit` substitution now also seeds the °C/°F
   handling, so demo (and the pre-attribute window after boot) is
   unit-correct without any entity.
4. **CI** — done. GitHub Actions compiles the standard **and**
   the demo variant against a pinned ESPHome (2026.4.5) and
   `latest`, with toolchain caching. Note: the project directory is
   not a git repo yet — init and push to activate the workflow.
5. **License** — done (MIT, `LICENSE`); adjust the holder line to
   your preferred name/handle before publishing. Formula
   attributions live in the README credits.
6. **Announce** — pending: HA community forum post,
   r/homeassistant, Seeed wiki / reTerminal E1001 channels.

## Features added beyond the original phases

- **°F support** — auto-detected, converted at the formula/display
  boundary (Phase 2 item, done).
- **Battery saver** — opt-in deep sleep between refreshes on
  battery, EXT1 button wake with HA replay, charger-based external
  power detection, boot hatch; design and verification checklist in
  [battery-saver-plan.md](battery-saver-plan.md) (done).

## Ideas parked (not committed)

- i18n label tables (weekday names, headers) — maintenance treadmill;
  document as a limitation first.
- ESPWebTools browser flashing — only if the device's USB-CDC boot
   mode cooperates; investigate in packaging phase.
- Configurable forecast horizon / slot spacing (`[0, 3, 6, 9]`).
- `docs/weather-data.md` grown into a tested provider matrix (which
  integrations serve UV, hourly data, precipitation probability).

## Non-goals

- Home Assistant blueprint (wrong mechanism — see above).
- A universal display-model framework; the 800×480 panel grid is
  this project's design.
- Abstracting weather providers; the `weather.*` entity **is** the
  abstraction — we document the field matrix instead of writing
  provider integrations.
