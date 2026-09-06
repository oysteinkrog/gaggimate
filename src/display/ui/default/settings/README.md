# On-display settings

The settings screens the machine shows on its own panel. Code-built LVGL,
not EEZ Studio. The design rules and the measured numbers live in the
repo's `CLAUDE.md`, section "On-display settings"; read that first. This
file is the working guide: what each file is, how to add a category, how
to run the tests, and what never to do here.

## File map

- `SettingsModel.h` / `.cpp`: the value model. Ranges, steps, wrap rules,
  display formats, choice lists, the time zone region and city split,
  gradient map slots, the palette, wake-up schedule editing. Plain C++17
  with no LVGL, Arduino or ESP-IDF dependency, so `pio test -e
  native_settingsui` runs it on the host.
- `SettingsUI.h` / `.cpp`: the shell. The cover object, the tile page, the
  paged list page, the page stack, `Settings::Guard` ordering, the debug
  tags, web-save reconciliation and the 1 s refresh tick. The header is the
  contract every category codes against.
- `SettingsRows.h` / `.cpp`: the seven row widgets (stepper, choice, toggle,
  action, locked, confirm, info). Each fits the shell's 320x56 slot and owns
  its own hold state.
- `SettingsLog.h`: `settingsLogAppend`, a bounded `snprintf` accumulator for
  the categories' one-line commit logs.
- `CatTemps.cpp`: temperature offset, pressure sensor rating, brew and grind
  delay, delay auto-adjust.
- `CatDisplay.cpp`: main and standby brightness, dim timeout, 24-hour clock,
  time zone region and city.
- `CatAnimation.cpp`: animation, frame rate, all screens, theme, gradient,
  plates and plate colour and opacity, element tint and tint colour, text
  scrim. Every row here applies live.
- `CatMachine.cpp` / `CatMachine.h`: startup mode, standby timeout, auto
  wake-up, and the `MachineDraft` the schedule pages share. The header is the
  whole contract between this file and the schedule editor, including the
  weak row-provider hook that adds the fourth row.
- `CatSchedules.cpp` / `.h`: the schedule list page and the schedule editor
  page, both pushed with the Machine draft as their ctx.
- `CatStatus.cpp`: display and controller firmware, network and IP address,
  the controller and scale link state, the clock, and the Restart confirm
  row.
- `SettingsFixture.cpp`: the sixth tile. One of each row widget, and the
  counters the Fixture scenario asserts on. Compiled only under
  `GM_TOUCH_PROBE` or `GAGGIMATE_SIM`.

The tests are one directory tree away: the shared rig and the scenarios in
`tools/settings_ui_tests/` (its own `README.md` documents `Rig` and `Sim`
call by call), the end-to-end runner at `tools/settings_ui_test.py`, and the
host model tests in `test/test_settings_model/`.

## Adding a category

1. Write `Cat<Name>.cpp` in this directory. It defines one
   `const SettingsCategoryDef kCat<Name>` with a title, a 40x40 icon from
   `eez/images.h`, and the callbacks `SettingsUI.h` documents: `rowCount`,
   `buildRow`, `enter`, `refresh`, `commit`, `reconcile`, `createCtx`,
   `destroyCtx`. A category with no editable content sets `createCtx` and
   `destroyCtx` to `nullptr` and gets `nullptr` for every ctx.
2. Declare `kCat<Name>` in `SettingsUI.h` and add it to `kCategories` at the
   top of `SettingsUI.cpp`. There are no stand-in definitions any more, so a
   missing definition is a link error rather than a page of numbered stubs.
3. `enter` snapshots `Settings` into the draft, `commit` writes back only the
   fields the visit touched, and `reconcile` re-reads the untouched ones. All
   three already run under `Settings::Guard`; do not take it again inside
   them, and do not skip it for a read you add elsewhere.
4. Build every row through `SettingsRows.h`. Rows report changes; they never
   format their own model, and the category never reaches into a row.
5. Add a scenario, `tools/settings_ui_tests/test_<name>.py`, with
   `run(rig, report, venue)` and the module-level `FAILURES` and `TOTAL` the
   sibling scenarios use. Then add its name to `SCENARIO_ORDER` in
   `tools/settings_ui_test.py` and its pages to `CATEGORY_PAGES` in
   `tools/settings_ui_tests/audit_pages.py`. The scenario puts back every
   value it changes.
6. A new source file under `src/` needs `touch src/CMakeLists.txt` before the
   ESP-IDF builds pick it up; the glob has no `CONFIGURE_DEPENDS`.

## Running the tests

Use the `pio` on PATH, one PlatformIO install per checkout (`CLAUDE.md`).

The value model, on the host, 28 cases, a few seconds:

```
pio test -e native_settingsui
```

The whole feature on the desktop simulator. This is the venue to use first,
and the runner launches and tears down the simulator itself:

```
pio run -e display-sim
python3 tools/settings_ui_test.py --report-dir out/settings
```

A full run takes about ten minutes, most of it the scenarios. The first
90 seconds are the warm-up the measuring rules in `CLAUDE.md` require before
any rate or heap figure counts. `--only temps,status` runs a subset,
`--skip-restart` drops the checks that reboot the venue, and `--sim-port N`
moves the web server off 8080 so two runs can coexist.

The same runner against the bench board, which must be running a
`display-loadtest` build:

```
python3 tools/settings_ui_test.py --host 192.168.1.121 --report-dir out/settings
```

The device run adds the frame rates, the heap and the leak phase. It stops
the synthetic brew lifecycle first and restores it on exit. The board is
shared and is flashed only by the epic's lead.

Read `<report-dir>/report.json` for every number and every violation, and
`<report-dir>/<page>.png` for each page's framebuffer with its hit
rectangles drawn on it. The exit code is 0 only on `PASS`.

## Seeding a simulator fixture

Some starting values cannot be reached from the display's own steppers, and
some data the display cannot create at all (a gradient in the library, for
one). `tools/settings_ui_tests/fixtures/controller.json` is the simulator's
NVS namespace as one JSON object of string values under the short NVS keys
`src/display/core/Settings.h` registers. `fixtures.seed(data_dir)` writes it
to `<data_dir>/nvs/controller.json` before the process starts, and
`--fixture PATH` points the runner at an edited copy.

The directory must literally be named `sim_data`: the simulator's
`preferences_shim.cpp` and `fs_shim.cpp` hard-code that relative path. Run
the simulator from a scratch directory so its `sim_data/` never lands in the
repo.

## Never

- **Never POST `/api/settings` by hand.** The web UI is the only safe writer.
  Tests change values through the UI, by the same route a finger would.
- **Never print the `wifiPassword` field of GET `/api/settings`.** `Rig`
  deletes the key before it returns the dict; anything new that reads
  settings does the same.
- **Never put a translucent plate over the animation.** Every translucent
  pixel is blend time in the render task on every animation frame.
- **Never add a mutable table to internal DRAM.** Lookup tables are `const`
  PODs in flash, which a `const std::vector` or a `String` is not. LVGL
  objects come from PSRAM through `gm_lv_malloc` and are not the concern;
  plain C++ allocations are.
- **Never step a value on `LV_EVENT_CLICKED`.** Steppers act on `PRESSED`
  and on each `LONG_PRESSED_REPEAT`, so a press and a click would double the
  step.
- **Never drive a hold from an `lv_timer`.** LVGL's `obj_del_core` resets the
  indev on a deleted object without sending it a cancel event, so a timer
  holding a raw row pointer across a page rebuild mid-hold would dangle.
- **Never touch the animation hot slab** (`bganim::allocHot`) from settings
  code.
