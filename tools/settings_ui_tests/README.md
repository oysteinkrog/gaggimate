# settings_ui_tests

Shared Python rig for the on-display settings UI epic (gm-flw): HTTP access
to the debug routes, synthetic taps, an active-screen touchmap dump with
effective hit-rectangle arithmetic and tag-based lookup, a geometry audit,
a WiFi-password-filtered settings reader with wire-format parsers, and a
simulator launcher. Category scenario scripts and the end-to-end runner
(gm-flw.13) are built on this; it carries no scenario logic of its own.

`rig.py` is the module and `test_rig.py` its own runnable check. Beside them
sit the seven category scenarios (`test_<name>.py`), the end-to-end runner's
page inventory and audit (`audit_pages.py`), its preflight and simulator
seeding (`fixtures.py`), and the seeded NVS fixture
(`fixtures/controller.json`). The runner itself is one directory up, at
`tools/settings_ui_test.py`; see "The end-to-end runner" at the bottom of
this file.

## Rig(host)

One HTTP client per venue. `host` is `"ip[:port]"`: `"192.168.1.121"` for
the bench device (`display-loadtest`, default port 80), or
`"127.0.0.1:8084"` for a simulator started with `GM_SIM_PORT=8084` (or just
use the `Sim` context manager below, which builds this for you).

- `get_json(path)` / `get_bytes(path)`: GET path, parsed as JSON or raw
  bytes. Every HTTP failure (bad status, connection refused, or a 200 whose
  body is not JSON, which happens when a route this build did not compile
  falls through to the static/SPA handler) raises `RigHTTPError` with the
  URL and the reason in the message, never a bare `json`/`urllib` exception.
- `tap(x, y, ms=80)`: queues one synthetic tap (`/api/debug/tap`), polls the
  tap state until it reports the release, then waits 150 ms for the UI task
  to act on it. `tap_target(target, ms=80)` taps the centre of a target
  dict's `"hit"` rect (from `touchmap()`/`targets()`/`find_tag()`).
- `touchmap(screen=0, load=False, timeout=8)`: dumps a screen's object tree
  (`/api/debug/touchmap`) and annotates every object with `"hit"` (its
  effective hit rectangle: coords grown by the ext click pad, clipped by
  every ancestor without the overflow-visible flag) and `"hidden"`.
  `screen=0` is the active screen; other ids load that screen first unless
  `load=False`. The poll loop always sleeps before reading, including the
  first read: see the "simulator crash" note below.
  `wait_dump_change(prev_seq, timeout=8, screen=0, load=False)` polls until
  the dump's `"seq"` differs from `prev_seq`.
- `find_tag(dump, row, role)`: the object tagged `"row/role"`, or `None`.
  `row_value(dump, row)`: the canonical value of a settings row (the `"val"`
  of its `"row/value"` object, falling back to `"t"` when `"val"` was not
  emitted); raises `ValueError` naming the visible rows when the row has no
  value object. `rows_on_page(dump)`: settings row names on the page, top to
  bottom (a row's outer container carries role `row`, or `toggle`/`action`/
  `confirm` when the whole row is the tap target; the shell's five slot
  containers `row0`..`row4` are role `slot` and are not rows). `targets(dump, include_hidden=False)`: clickable objects with an
  event callback, excluding the settings shell's cover and any object
  covering at least 90 percent of the panel (a generated screen's own
  tap-anywhere-to-standby background).

  These four are `@staticmethod`s on `Rig` as well as module-level
  functions in `rig.py`, and all take `dump` explicitly (`find_tag(dump,
  row, role)`, `rig.row_value(dump, row)`, and so on): the bead text lists
  `row_value(row)` without `dump`, but every sibling lookup needs a dump to
  operate on, so this uses one consistent signature rather than hidden
  last-dump state on `Rig`.
- `audit(dump, exempt_roles=("exit",), exempt_names=("standby_btn",))`: the
  shared contract's geometry rules for every tappable target (56x56 px
  floor, no overlap, 12 px clearance from the panel's edge circle), with
  the exit chevron (by role) and the generated screens' `standby_btn` (by
  EEZ name) exempted from the size/edge rules by name, never from the
  overlap rule (which has no named exemption). Returns
  `{"violations": [...], "exempt": [...]}`; an entry is
  `{"target", "tag", "reason", "detail"}` (`"reason"` is `"size"`, `"edge"`
  or `"overlap"`; an `"overlap"` entry also carries `"other"`).

  **The generated (EEZ-designed) screens are not held to these rules**,
  per the bead text's own parenthetical, and in practice cannot be: the
  brew and menu screens both have real violations that predate this epic
  (see `test_rig.py`'s `brew_audit_informational`/`menu_audit_informational`
  log lines; measured 2026-09-06, e.g. `brew_dials__menu_icon`'s hit rect
  reaches about 248 px from the panel centre, past the round panel's
  240 px visible radius). Only the settings shell's own pages, once later
  beads build them, are expected to pass `audit()` clean.
- `settings()`: GET `/api/settings` with the WiFi password key deleted
  before the dict is returned; the raw body is never logged.
  `settings_value(key)` reads one field's raw wire-format value.
  Module-level wire-format parsers: `seconds(v)` (already seconds over the
  wire), `num(v)` (a numeric string to `float`), `color_hex(v)` (`"#rrggbb"`
  to `(r, g, b)`), `schedules(s)` (`"HH:MM|ddddddd;..."`, days Mon..Sun, to
  a list of `{"time", "days"}` dicts).
- `settingsui(**args)` / `settingsui_state()`: `/api/debug/settingsui`
  (`open=1`, `close=1`, `cat=N`, `page=N`, `pop=1`; GET with no arguments
  returns the shell's state). The route landed with gm-flw.6 and is compiled
  for the simulator and the bench builds alike. It reports that a command was
  queued, not that it ran, and answers 409 while one is still in flight, so a
  caller waits on the published `seq` before sending the next one:
  `audit_pages.command` does both, and every scenario here has its own
  equivalent.
- `heap()`, `anim(**args)`, `synth(brew=None)`: `/api/debug/heap`,
  `/api/debug/anim`, `/api/debug/synth`. **`anim()` and `synth()` do not
  exist on the simulator**: both routes are compiled only for the real
  panel (`WebUIPlugin.cpp` guards that whole block with
  `!GAGGIMATE_HEADLESS && !GAGGIMATE_SIM`), so calling them against a `Sim`
  raises `RigHTTPError` (a 200 with the web UI bundle, not JSON, same as
  `settingsui`). Device-only.
- `fb_png(path, step=2, hit_rects=None)`: writes `/api/debug/fb` (RGB565)
  to `path` as a PNG at `1/step` resolution, optionally drawing hit
  rectangles.
- `wait_until(pred, timeout, every=0.2)`, `log(step, **kv)` (one line:
  ISO-8601 UTC timestamp, step, `key=value` pairs).

## Sim(program_path, data_dir, port=8080, log_path=None)

Context manager that launches `pio run -e display-sim`'s
`.pio/build/display-sim/program` headless
(`SDL_VIDEODRIVER=dummy`, `GM_SIM_PORT=<port>`) and tears it down on exit.

- `data_dir` must be a directory literally named `sim_data` (the sim's
  `preferences_shim.cpp`/`fs_shim.cpp` hard-code that relative path); the
  program's working directory is set to `data_dir`'s parent so that path
  resolves correctly, and `program_path` is made absolute before that
  working-directory change (a relative path resolves against the child's
  new cwd, not the caller's, once `cwd=` is set on `Popen`).
- Refuses a port already in use at once, with a clear message. This checks
  by *connecting*, not binding: on WSL1 (this dev machine; see the repo's
  `CLAUDE.md` WSL1/wslfs note), a plain `bind()` with `SO_REUSEADDR`
  succeeds even when another process already has that exact address:port
  bound and listening (verified directly: two plain Python sockets, both
  `SO_REUSEADDR`, both bind `127.0.0.1:<port>` with no error; real Linux
  refuses the second). A bind-based free-port check is silently useless on
  this platform; connecting to the port is also simply the more direct
  question (is something already answering there).
- Waits for `"Started webserver"` in the combined stdout+stderr log (30 s
  timeout, raising `SimError` with the captured output on failure or an
  early exit), then confirms `GET /api/settings` answers, then waits an
  additional settle margin (`Sim.BOOT_SETTLE_S`, currently 1.5 s) before
  returning. See "simulator crash" below for why the margin exists.
- `restart()`: kills and relaunches with the same `data_dir`, for
  persistence checks (`Sim.restart()` must preserve a setting written
  before the call).
- `sim.rig` is a ready `Rig("127.0.0.1:<port>")`.

```python
with Sim(program_path, os.path.join(workdir, "sim_data"), port=8084) as sim:
    sim.rig.settings()
```

## Two things this rig works around, not fixes

Both were found by running this module against the real simulator, not
predicted; neither is in this bead's file list (`sim/`, `src/display/`) to
fix in-place, so they are worked around here and reported to the epic's
lead:

1. **A screen-changing touchmap request too soon after boot kills the
   simulator outright** (no log line; the process is simply gone, and the
   next request gets connection-refused). Reproduced down to timing alone:
   the identical `/api/debug/touchmap?screen=N&load=1` request, sent under
   about 1 s after the process starts, kills it every time; past about
   1.2 s it is safe every time (10/10 trials at 1.0 s clean, 10/10 at
   0.5 s dead). `touchmap()`'s poll loop sleeps before every read,
   including the first, and `Sim`'s extra settle margin after the ready
   banner covers the rest; a caller using `Rig`/`Sim` as documented here
   never sends the request early enough to hit it. Root cause (something
   the UI task's first few passes have not finished) is unconfirmed.
2. **A route this build does not compile answers 200 with the gzip-
   compressed web UI bundle, not a 404.** `/api/debug/anim`, `/api/debug/
   synth` (device-only; `GAGGIMATE_SIM` excludes both) and
   `/api/debug/settingsui` (not landed yet) all fall through to the sim's
   static/SPA handler this way. `get_json()` turns that into a clean
   `RigHTTPError` naming the Content-Type, rather than a raw
   `UnicodeDecodeError` from trying to parse the gzip bytes as JSON text.

## test_rig.py

This module's own test and gm-flw.16's acceptance check, runnable alone:

```
python3 tools/settings_ui_tests/test_rig.py [--program PATH] [--workdir DIR] [--port N]
```

Exits 0 if every check passes, 1 otherwise (`FAILURES`), logging each step
through `Rig.log`. Drives the desktop simulator only; the bench device
checks the bead text also calls for (`Rig("192.168.1.121")`, `synth(0)`)
are not run here (see "Not verified against the device" below).

The brew screen's target-name baseline (`BREW_TARGETS`) was dumped against
a clean sim boot on 2026-09-06 (commit c4d4edc5):
`brew_dials__menu_icon`, `profile_name`, `profile_select_button`,
`settings_button`, `start_button`. A mismatch means either the brew
screen's layout changed (re-baseline) or `touchmap()`/`targets()`
regressed (it should not).

## Not verified against the device

Everything above is checked against the simulator only. The bead text also
calls for the same checks against the bench device
(`Rig("192.168.1.121")`, `synth(0)` included); the device is shared and
flashed only by the swarm lead, so those runs are not this report's to
make. `settingsui()`/`settingsui_state()` are untested everywhere (the
route does not exist until gm-flw.6 lands); `anim()`/`synth()` are
confirmed unavailable on the simulator (see above) but not exercised
against the device build (`display-loadtest`) where they do exist.

## The end-to-end runner: `tools/settings_ui_test.py`

One command drives the whole on-display settings feature and prints a
pass/fail report. It audits every settings page's touch targets, runs the
seven scenarios above, records the animation frame rate and the heap while
settings is open, and checks that a value survives a restart.

### Against the simulator

```
pio run -e display-sim
python3 tools/settings_ui_test.py --sim-port 8099 --report-dir out/settings
```

It launches the simulator itself, in a fresh directory under the report
dir, seeded from `fixtures/controller.json` (see "Fixtures" below), and
tears it down at the end. A full run takes about seven minutes on this
machine, most of it the scenarios; the first 90 seconds are the warm-up
waiting out the boot churn, which the measuring rules in the repo's
`CLAUDE.md` require before any rate or heap figure is worth recording.

### Against the bench device

```
python3 tools/settings_ui_test.py --host 192.168.1.121 --report-dir out/settings
```

The device run adds what the simulator cannot do. It stops the synthetic
brew handshake first (`synth(0)`, restored on exit) so the load rig's
lifecycle does not move the UI underneath the taps, reads
`/api/debug/pclk` and warns when the stored pixel-clock divider is not the
build's 6, turns All screens on through the UI if it is off (and puts it
back), records a menu-screen baseline frame rate and a 5 s window per page,
and runs a leak phase of 20 open and close cycles of the largest page
before any scenario that can restart. The device is shared and is flashed
only by the epic's lead; nothing here writes to it except through the UI.

Useful flags:

- `--only rig,temps,display,animation,machine,schedules,status` runs a
  subset. Only those scenarios and their own pages are visited, so
  `--only status --skip-restart` is the quickest way to exercise the
  plumbing.
- `--skip-restart` skips the restart round trip and every scenario check
  that reboots the venue.
- `--fixture PATH` seeds the simulator from an edited copy of the fixture,
  which is how a value the display cannot reach is put in front of the
  preflight.
- `--sim-program`, `--sim-port`, `--report-dir`.

### Reading the report

Every step prints one line as it happens: an ISO-8601 UTC timestamp,
`step=<name>`, then `key=value` pairs. At the end come a table of the pages
(targets, smallest target, frame rate), a table of the scenarios (status,
wall time, checks run, checks failed), the sampled heap minima and then
either `PASS` or `FAIL` with every violation listed. The exit code is 0
only on `PASS`.

`<report-dir>/report.json` carries all of it, plus every step in order.
`<report-dir>/<page>.png` is each page's framebuffer with its targets' hit
rectangles drawn on it, at half resolution.

Two lists, and the difference between them matters:

- **violations** are the run's assertions and the exit code answers to
  them: a page that failed its audit, a scenario that failed, an unmet
  preflight condition, a restart that lost the value, and on the device a
  page rate below 90 percent of the menu baseline or a leak phase that
  lost internal DRAM.
- **findings** are recorded and printed but do not fail the run. In
  practice they are `RESTORE FAILED` lines: a scenario that did not put a
  value back where it found it. Several are a check's own deliberate
  residue (the Status scenario proves a brightness bump survives a restart
  by leaving it bumped), so the bead asks for them to be logged without
  stopping the run.

Two heap numbers look similar and are not: `dma_free_sampled_min` is the
smallest value this run happened to sample, and `dma_min` is the venue's
own boot-lifetime minimum, which no sampling can see.

### Fixtures and the preflight

`fixtures/controller.json` is the simulator's NVS namespace, written before
the process starts: one JSON object, string values under the short NVS keys
`src/display/core/Settings.h` registers. It exists for two reasons. Some
starting values cannot be reached from the display's own steppers (a 45 s
dim timeout is not on the 30 s grid, a colour outside the twelve named
palette entries survives only one visit), so a scenario that stepped away
from one could never put it back. And some data the display cannot create
at all: the Animation scenario needs a gradient in the library, which
defaults empty, and the Schedules scenario needs a schedule to work around.

Before anything is changed, the preflight confirms the three instruments
answer (`/api/debug/tap`, `/api/debug/touchmap`, `/api/debug/settingsui`)
and that every checked starting value is on its stepper's grid. An unmet
condition prints `UNSUPPORTED FIXTURE key=... value=... scenario=...` and
that scenario is failed without being run, so its unreachable value is
never stepped away from. Pointing `--host` at a production build fails here
too, naming the routes that build does not compile.

The runner never POSTs `/api/settings`, never prints the WiFi password, and
puts back through the UI every value it changes itself.

### Adding a scenario

A scenario is `test_<name>.py` in this directory. Give it:

- module-level `FAILURES` (a list of `(name, detail)` pairs) and `TOTAL`
  (an int), both maintained by a small `check(rig, name, cond, detail)`
  helper, which is the shape every scenario here already uses;
- a `_sequence(rig, venue)` holding the checks in order, so `main()` and
  `run()` cannot drift apart;
- `run(rig, report, venue)`, which calls `_sequence`, logs one
  `scenario_checks` step through `report.step`, and raises `AssertionError`
  listing anything that failed;
- a `main()` that builds its own `Venue` (from `fixtures.py`) and launches
  a simulator, so the script stays runnable alone.

Then add its name to `SCENARIO_ORDER` in `tools/settings_ui_test.py` and
its pages to `CATEGORY_PAGES` in `audit_pages.py`. Read `venue.sim` before
restarting anything and `venue.can_restart` before doing it, `venue.log_path`
before reading the simulator's log, and skip with a logged reason rather
than silently when the venue cannot support a check.
