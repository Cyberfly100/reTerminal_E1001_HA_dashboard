# Battery saver mode — implementation plan (final)

Deep sleep between refreshes when running on battery, fully
transparent when on external power. Questions Q1–Q4 answered;
hardware findings verified against the Seeed schematic and the
Zephyr board support files.

## Design decisions (verified)

- **Power detection (Q1: investigated)** — the board has a
  **Silergy SY6974B battery charger on I2C1 (SDA GPIO39, SCL GPIO40,
  address 0x6B)**. Its REG0A bit 7 (`BUS_GD`) reads "valid input
  source present" — the same bit the Zephyr `silergy,sy6974b` driver
  uses for its online property. Read raw via the public
  `I2CBus::write_readv()` — no custom component needed. Exposed as an
  **"External Power" binary sensor** (also nice for HA automations).
  Fail-safe: if the charger does not answer, treat the device as
  *powered* — a flaky I2C bus must never put a plugged-in device to
  sleep (cost: no savings, never the wrong sleep).
- **Wake buttons (Q4: all three)** — GPIO3/4/5, active-low, all
  RTC-capable on the ESP32-S3 (RTC GPIOs 0–21 map one to one onto
  GPIO numbers). ESPHome's stock `deep_sleep` component supports
  only **one** wakeup pin on ESP32 (multi-pin is BK72XX-only), so
  sleep entry is done manually with the S3's EXT1 wake source —
  `esp_sleep_enable_ext1_wakeup(mask, ESP_EXT1_WAKEUP_ANY_LOW)` +
  `esp_sleep_enable_timer_wakeup()` + `esp_deep_sleep_start()`,
  with sleep-mode pull-ups (`gpio_sleep_set_pull_mode`) on the three
  pins. (The `esp_deep_sleep_enable_gpio_wakeup` API the draft
  mentioned turned out to be C-series/S2-only — not compiled for
  the S3; EXT1 is the S3's multi-pin wake source.)
- **Button actions are HA automations (Q4 detail)** — a press during
  sleep happens while the SoC is off, so no `on_press` fires. The
  firmware reads the wake mask (`esp_deep_sleep_get_gpio_wakeup_status()`)
  at boot and **replays** the press to HA after its states arrive:
  one event-entity trigger (`id(e1001_buttons).trigger("middle")`)
  per pressed button — stateless, fire-and-forget, no on/off
  ordering. (The original design replayed a binary-sensor on→off
  pulse; the api's deferred, state-deduplicated sends collapsed
  any on→off pair published within one loop pass into a single
  "off" message, so HA never saw the press. Events sidestep the
  entire mechanism — see the README battery-saver notes.)
  The replay lands ~2 s after the state pushes: deferred by ~2 s
  (past HA's subscription burst — anything published earlier has
  no subscriber and is dropped) and delivered by a 500 ms
  `wake_replay_fast_tick` interval, with the 5 s battery-saver
  tick as a backstop. Wifi joins with `fast_connect` (no scan),
  which removes another 3-8 s between the press and HA.
- **Green button** — its local function (refresh) runs via the
  normal boot refresh after any wake; no special casing needed.
- **Awake window (Q2: 30 s)** — armed once the clock syncs after a
  wake (the fetches and the e-ink redraw are done by then); the
  device sleeps at the deadline. A button press while awake extends
  the deadline to the interactive window (120 s).
- **Cadence on battery (Q3: 15 min)** — the sleep timer is the
  cadence; the awake-window machinery drives it. On external power
  nothing changes: the 300 s `interval:` keeps refreshing as today.
- **Boot hatch** — holding the green button ~5 s after a
  wake disables sleeping for that session (OTA, logs, debugging).
- **Home Assistant keep-awake helper** — an `input_boolean`
  (substitution `keep_awake_entity`, default
  `input_boolean.e1001_keep_awake`) that latches the device
  awake while on. Its mirror sensor arrives with the API
  connection, before the first sleep decision, so it is always
  known by the time it matters. Set it *while the device is
  asleep* and it takes effect at the next wake — deep sleep is
  unreachable by definition.
- **HA-unreachable fallback** — a wake with no HA (router down)
  never syncs the clock, so the awake window would never arm and
  the device would stay awake forever on battery; after ~2
  minutes without a clock sync it sleeps with nothing left to
  do (the boot hatch covers deliberate no-HA debugging).
- **Never sleep mid-flight** — sleep only happens when
  `pending_fetches == 0` and the deadline is ≥ 30 s past the last
  redraw (an e-ink frame settles in ~2–4 s).
- **Battery bridge off before sleep** — GPIO21 (measurement divider
  enable) is turned off explicitly, so it cannot drain during sleep;
  verified further by a current measurement (below).

Substitutions: `battery_saver` (default `'true'` — on external
power the saver stays completely inert), `battery_sleep_seconds`
(`'900'`), `battery_awake_seconds` (`'30'`),
`battery_button_awake_seconds` (`'120'`).

## Known constraints (unchanged from draft)

While asleep the device is unreachable from HA — buzzer silent,
status/refresh land at the next wake, sensors show unavailable, OTA
impossible (use the hatch). All interactions work during the awake
window. Plugging in while asleep takes effect at the next wake.

## Findings from the first battery run (log-verified)

Four behaviors surfaced that reshaped the design:

1. **The charger does not answer on I2C — on either bus, even
   powered.** The GPIO39/40 wiring matches the V1.2 schematic
   (with 0R series + ESD + 10K pull-up), and the sensor bus
   (GPIO19/20) works (SHT4x at 0x44, PCF8563 at 0x51 appear in
   the scan), but nothing ACKs at any address on 39/40 with USB
   attached. The schematic carries a design-change note
   ("U2 ETA6003 to SY6974B"), suggesting production batches
   differ. The read keeps trying both buses (correct wherever
   the charger does answer) and falls back to the pack voltage:
   a silent charger with ≥ 4.15 V reads as powered (a charging
   87 % pack measures 4.02 V — below the threshold, so early
   charging may sleep-cycle; the keep-awake helper covers
   interactive use while charging).
2. **The S3's RTC persists across reboots and deep sleep** —
   the clock is valid from t=0, so "valid clock" no longer
   implies "HA connected" (and the invalid-clock fallback only
   ever applies to a genuine cold boot).
3. **Dropped `homeassistant.action` calls never error** —
   dispatching fetches before HA connects leaves the tickets
   hanging until the watchdog (observed: forced placeholder
   redraw 20 s after boot, all four outstanding). The boot
   refresh was removed; the first refresh of every boot is
   anchored on HA states arriving.
4. **The interval first tick fires immediately, and any API client can answer the device's time request.** ESPHome's scheduler runs an interval's *first* execution right after setup ("first execution happens immediately after a random smallish offset", up to 5 s — not after one full period), so the 300 s refresh interval dispatched fetches into a not-yet-reconnected HA on **every** boot: the `homeassistant.action` calls are silently dropped and the watchdog forces the placeholder redraw at +20 s — the recurring "blank first redraw" in all three debug logs. Separately, the `esphome` CLI also synced the clock within a second of boot (any authenticated client answers the device's time request) and `RealTimeClock` re-fires the sync trigger on *every* periodic time response ("already synchronized, but still call callbacks"), so anything anchored per-sync would repeat forever. Both paths are now gated on `met_temperature.has_state()` — a state push only Home Assistant performs — with the initial refresh latched once per boot and anchored on the sensor's own `on_value` (the state push itself) as well as the time sync; failed first attempts fall back to the gated 300 s interval.

Also observed: HA can take ~45–80 s to reconnect after a device
reboot (its reconnect backoff) — within the 150 s connect
ceiling; and a full e-ink redraw blocks the loop for ~8 s —
the ≥30 s margins around it are comfortable.

## Hardware verification checklist (on-device)

1. Sleep current with a multimeter (expect mA-level or below; if
   the divider or rails drain, the savings evaporate).
2. GPIO3 wake actually works (it is a strapping pin — JTAG select;
   holding it low at the wake-reset should be harmless, confirm).
3. Charger `BUS_GD` reads 1 on USB, 0 on battery.
4. Seeed's own claim (datasheet): 2000 mAh for ~3 months at a
   6 h refresh — our 15 min cadence should land around 2 weeks
   with the ~40 s awake per cycle (boot + sync + 30 s window).

## Implementation files (done)

- `battery_helpers.h` — charger read (answered → BUS_GD;
  silent on both buses → pack-voltage tie-break, NACK and
  bus errors deliberately merged — the ESP32-S3 does not
  report them reliably) + cached state with transition
  logging + `external_power_present()` (undetermined
  counts as present — fail-safe)
- `sleep_helpers.h` (new) — wake-cause read (EXT1 mask,
  timer and reset causes logged too), button replay,
  awake-window deadline, `battery_saver_tick()` (on the
  existing 5 s interval, one-shot status log at the
  first tick), `enter_battery_sleep()` (log line flushed
  to connected clients before the shutdown)
- YAML — substitutions, globals, `i2c:` charger bus, External Power
  binary sensor, boot hatch + wake-cause read, `api:
  on_client_connected` replay, button window extension, bridge off
  before sleep

Verified compiling on ESPHome 2026.4.5 and 2026.10.0 (zero
warnings). A side find: ESPHome 2026.10+ rejects the all-zeros
API encryption key — `secrets.yaml.sample` now carries a real
throwaway key (the CI `latest` job would have caught this too).

## Parked improvements (not now)

- **PCF8563 RTC (i2c0 @0x51)** — keeping time across sleeps would
  let the calendar fetch run on the *first* post-wake draw instead
  of waiting for HA time sync; saves seconds of awake time per
  cycle and one redraw.
- Green LED (GPIO6) as an awake indicator.
