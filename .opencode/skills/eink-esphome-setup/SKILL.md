---
name: E-Ink ESPHome environment
description: Set up, rebuild, or use the local ESPHome compile/test environment for this firmware — scratch build trees, the two toolchains, secrets, demo variants, the compile matrix, codegen verification, and OTA flashing the device. Use when /tmp/opencode trees are missing (e.g. after a reboot), before any local compile or test, when building demo variants, or when flashing the device locally.
---

# What lives where

The repo is the source of truth; everything under `/tmp/opencode` is rebuildable from it.

| Thing | Location | Notes |
|---|---|---|
| Firmware source | repo: `reterminal-e1001-entrance.yaml` + six `*_helpers.h` + `fonts/` | never edit repo files to build variants |
| Toolchain A | `esphome` on PATH (persistent uv tool) | pinned minimum, matches the CI matrix's `2026.4.5` |
| Toolchain B | `/tmp/opencode/esp210b` venv | **latest** ESPHome, rebuilt after each reboot, matches CI's `latest` job and the remote builder |
| Scratch tree A | `/tmp/opencode/eink-test` | compiles with toolchain A |
| Scratch tree B | `/tmp/opencode/eink-live` | compiles with toolchain B; also the OTA/live-log tree |
| Real secrets | never in the repo | user drops a real `secrets.yaml` into `/tmp/opencode/eink-live` for live sessions; delete after |

`/tmp` is wiped on every reboot — assume the trees are gone after any multi-day gap. Check first (`ls /tmp/opencode/eink-live`), then rebuild (~2 min plus a first-compile framework download).

# Setup from scratch

Run steps separately — `&&` chains have failed three times here (a missing `cp -r`, a skipped `uv venv`, a secrets copy that never ran because an earlier link died silently).

1. `mkdir -p /tmp/opencode/eink-test /tmp/opencode/eink-live /tmp/opencode/esp210b`
2. Toolchain B — **latest, unpinned**:
   ```
   uv venv /tmp/opencode/esp210b
   uv pip install --python /tmp/opencode/esp210b/bin/python esphome
   /tmp/opencode/esp210b/bin/esphome version
   ```
   Note the resolved version in the session and compare with the remote builder. (`uv venv` creates no `pip` binary — always `uv pip install --python ...`; a plain venv dir is not an environment.)
3. Populate **both** scratch trees with all four things — each of these being missing has caused a failed compile:
   - `reterminal-e1001-entrance.yaml`
   - all `*_helpers.h`
   - `fonts/` — **`cp -r`**, it is a directory (the most frequent miss)
   - `cp secrets.yaml.sample secrets.yaml` — compile-only credentials; **never upload with these**
4. First compile in each tree downloads the platformio framework — background it with a generous timeout.

# The compile matrix

Standard on both toolchains plus the demo variant (CI runs the same trio):

```
cd /tmp/opencode/eink-test && esphome compile reterminal-e1001-entrance.yaml
cd /tmp/opencode/eink-live && /tmp/opencode/esp210b/bin/esphome compile reterminal-e1001-entrance.yaml
sed "s/demo_mode: 'false'/demo_mode: 'true'/" reterminal-e1001-entrance.yaml > demo-standard.yaml
/tmp/opencode/esp210b/bin/esphome compile demo-standard.yaml
```

- Variants are sed copies in the scratch tree — the repo files stay untouched.
- Expected result: exit 0, `grep -c "warning:"` = 0. Anything else is investigated, not shipped.
- `demo-gap.yaml` (the full-gap test) additionally sets `demo_missing: 'uv,humidity,wind,low,high,uvmax,precipitation,calendars'`.

# Codegen verification (after header or substitution changes)

Do not trust that a header edit reached the binary: the config cache does not always rebuild on header-only changes, and `${substitutions}` silently do not expand inside headers (the bridge is the YAML globals — `demo_mode_enabled`, `demo_missing_list`). Verify the generated code directly:

```
grep -n "<expected symbol>" .esphome/build/reterminal-e1001-entrance/src/main.cpp
```

e.g. a global's initializer (`GlobalsComponent<bool>(false)` vs `(true)`), a call site, or a log line. This is also the freshness check before uploads.

# The build-dir trap (has shipped demo firmware as "production")

All configs sharing the same `esphome: name:` build into the **same** `.esphome/build/<name>/` directory. If a variant was the last thing built there, a later `upload` of another config can push the stale variant binary. Before a production upload that follows variant builds:

```
esphome clean reterminal-e1001-entrance.yaml   # or: rm -rf .esphome/build/reterminal-e1001-entrance
esphome compile reterminal-e1001-entrance.yaml # full rebuild, then verify main.cpp
```

# OTA flashing the device

- Device `17.12.1.230` (OTA 3232, API 6053); requires the **real** secrets in `/tmp/opencode/eink-live` (ask the user to drop them — never the sample).
- On battery the device deep-sleeps and is unreachable. Probe first:
  ```
  timeout 3 bash -c '</dev/tcp/17.12.1.230/3232' && echo awake || echo asleep
  ```
  Asleep → ask the user to press any button (opens the ~120 s window), then `esphome upload --device 17.12.1.230 <config>`.
- Logs (the only working app-log channel — the USB bridge carries ROM output only on this framework):
  ```
  /tmp/opencode/esp210b/bin/esphome logs --device 17.12.1.230 <config>
  ```
  Start a capture **immediately** after an upload to catch the first boot cycle (state pushes → fetch dispatch → parse logs → redraw summary); attaching late misses it, and a capture against a demo build shows no fetches at all — verify which firmware is running before reading its silence as a failure.
- Remote-builder alternative: the user's builder flashes too — remind them to upload **all files (YAML + every header) every time**; partial uploads produce mixed firmware.

# Facts that keep being relevant

- The battery pack sits near the 4.15 V powered/on-battery threshold — the saver engages and the device sleeps between interactions; the e-ink frame persists through sleep, so the screen is evidence, not liveness.
- CI on push builds `2026.4.5` + `latest` × standard/demo — local greens on the same trio mean CI will be green (note: `2026.10.0` had a `data_template` bug, esphome#21303; that is why the pair is a pinned minimum plus latest, not a specific middle version).
- After live sessions end, delete `/tmp/opencode/eink-live/secrets.yaml` (user re-drops when needed).
