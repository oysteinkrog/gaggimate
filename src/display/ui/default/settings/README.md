# On-display settings

The settings screens the machine shows on its own panel. Code-built LVGL,
not EEZ Studio. The design rules and the measured numbers live in the
repo's `CLAUDE.md`, section "On-display settings"; read that first. This
file is the working guide: what each file is, how to add a category, how
to run the tests, and what never to do here.

## File map

- `SettingsModel.h` / `.cpp`: the value model. Ranges, steps, wrap rules,
  display formats, choice lists, the time zone region and city split,
  gradient map slots, the animation parameter string, the palette, wake-up
  schedule editing. Plain C++17
  with no LVGL, Arduino or ESP-IDF dependency, so `pio test -e
  native_settingsui` runs it on the host.
- `SettingsUI.h` / `.cpp`: the shell. The cover object, the tile page, the
  paged list page (left and right arrows in the header, or a horizontal
  swipe, turn the pages), the page stack, `Settings::Guard` ordering, the
  debug tags, web-save reconciliation and the 1 s refresh tick. The header
  is the contract every category codes against.
- `SettingsRows.h` / `.cpp`: the eight row widgets (stepper, choice, toggle,
  action, locked, confirm, info, swatch). Each fits the shell's 320x56 slot
  and owns its own hold state. The swatch row is an action row that also
  draws a 96x30 colour ramp and a selected marker: `settingsRowSetSwatch`
  takes `kSettingsRowSwatchSamples` RGB565 values, or a null pointer to show
  no ramp, and the row frees its own canvas buffer on delete.
- `SettingsLog.h`: `settingsLogAppend`, a bounded `snprintf` accumulator for
  the categories' one-line commit logs.
- `CatTemps.cpp`: temperature offset, pressure sensor rating, brew and grind
  delay, delay auto-adjust.
- `CatDisplay.cpp`: main and standby brightness, dim timeout, 24-hour clock,
  time zone region and city.
- `CatAnimation.cpp`: animation, the standby screen's own animation, the
  Parameters row, frame rate, all
  screens, theme, gradient, plates and plate colour and opacity, element
  tint and tint colour, text scrim, screen fade out, fade in and fade
  curve, interlace. Every value row here applies live. It also owns the
  animation roster the settings screens read, including the simulator's
  mirror of it, and exports that as the three accessors `CatAnimParams.h`
  declares.
- `CatAnimParams.cpp` / `.h`: the Parameters page, pushed from the
  Animation category's third row. One stepper per parameter the chosen
  animation defines (0 to 100, step 5, fast step 10), labelled from the
  animation's own `BgAnimParamDef`, plus a "Reset to defaults" confirm
  row. Every step writes the whole `bgAnimParams` string back and calls
  `markDirty()`, so the animation behind the settings cover changes under
  the finger. The page is fixed to the animation it was opened for, the
  way the schedule editor is fixed to its position. The string arithmetic
  (read one group with the defaults behind it, write one group back, clear
  one group) is in `SettingsModel`, host-tested; the page itself holds no
  parsing.
- `CatGradientPicker.cpp` / `.h`: the gradient picker, pushed by all three
  of the Animation category's gradient rows. Two pages: the groups (Global
  for a per-animation row, My gradients when the library is not empty, then
  each declared category with a count) and the gradients in one group, each
  with its name, a swatch and a marker on the one in force. The header is
  the whole contract: the caller passes a title, whether Global is offered,
  and four callbacks (read the current ref, take a pick, reconcile the
  parent draft, say whether the slot is still worth editing). The picker
  knows nothing about which setting it edits.
- `GradientSwatch.cpp` / `.h`: the ramp a swatch shows. A standalone
  transcription of the production palette arithmetic, because the real one
  works on the globals the render task draws with, so sampling five
  gradients to fill five rows would publish each of them in turn as the
  active theme. `tools/animbench/swatch_parity.cpp` compares all 256 entries
  against the real code for every built-in and for library strings with 2 to
  16 stops, repeated positions and flat endpoint runs, at seven tone
  settings; `make check` runs it.
- `CatMachine.cpp` / `CatMachine.h`: startup mode, standby timeout, auto
  wake-up, and the `MachineDraft` the schedule pages share. The header is the
  whole contract between this file and the schedule editor, including the
  weak row-provider hook that adds the fourth row.
- `CatSchedules.cpp` / `.h`: the schedule list page and the schedule editor
  page, both pushed with the Machine draft as their ctx.
- `CatStatus.cpp`: display and controller firmware, network and IP address,
  the controller and scale link state, the clock, the Device info row that
  opens the info screen (the WiFi setup QR code lives there), and the
  Restart confirm row.
- `SettingsFixture.cpp`: the sixth tile. One of each row widget, and the
  counters the Fixture scenario asserts on. Compiled only under
  `GM_TOUCH_PROBE` or `GAGGIMATE_SIM`.

### The Standby anim row

The Animation category's second row picks a background animation for the
standby screen on its own. It cycles through "Same" and then every animation
in the roster, and stores `bgAnimStandbyId`: -1 for "Same", otherwise the
animation's id. -1 is the default, so a device that has never touched this
plays one animation everywhere, the way it always did.

The rule lives in one place, `DefaultUI::updateState`. On every UI pass it
reads `bgAnimId`, and when the current screen is the standby screen and
`bgAnimStandbyId` names an animation in the live registry it pushes that id
instead. Everything downstream keys off the id it pushed, so the standby
animation brings its own stored parameters and its own gradient with it.
Nothing extra happens on a standby entry or exit: the render task sees a new
id, hands back the outgoing animation's tables and inits the incoming one on
that frame, which is the same path a live edit of the main animation already
takes.

A stored id past the end of the registry reads as "Same" rather than as an
error, in the row, in the web form and in the selection rule. Nothing
range-checks the value on its way in, and a build rolled back to a shorter
roster would otherwise index off the end.

One consequence worth knowing before reading a bug report: with "All
screens" off the animation only ever plays on the standby screen, so a
Standby anim other than "Same" is then the only animation anyone sees, and
the Animation row picks the one nothing shows. The web form says as much in
its help text.

### The three gradient rows

"Gradient all" sets `bgAnimGradientRef`, the global. "Gradient" sets one
slot of `bgAnimThemeMap`, the override for the animation on screen.
"Standby grad" sets the slot for the standby screen's own animation, and is
disabled, showing "Same as main", while the standby screen follows the main
animation or names the same one.

All three push the same picker and take a ref back through a callback. Four
things about that are load bearing:

- **The picker captures the slot it was opened for and never retargets it.**
  A web save that changes `bgAnimId` under an open picker still writes the
  animation the row was opened for. If that slot stops being a thing worth
  editing (the standby animation is turned off), the picker closes without
  selecting rather than writing a slot nothing reads.
- **Only the top page reconciles**, so each picker level calls the opening
  category's reconcile itself before anything else, the way the schedule
  pages do for the Machine draft. Without it the Animation page would come
  back from the picker showing what was stored when it opened.
- **A pick is applied before the pop.** `popPages` rebuilds the page
  underneath from the draft, so writing the draft after the pop would leave
  the old value on screen until the next refresh.
- **Choosing Global clears the slot, and it is offered only by the two
  per-animation rows.** "Gradient all" is the global, so "the global" is not
  a value it can take; its picker starts at the groups.

A ref is validated against the stored library under `Settings::Guard` at the
moment it is picked, so a library entry deleted from the web UI while the
list was on screen cannot be selected.

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

   Adding a row to a category that already has one is the same edit plus
   one more: `CATEGORY_PAGES` pins the exact row list of every page, and
   five rows to a page means one new row shifts every row below it onto
   the next page and can add a page. Address rows by name in a scenario
   rather than by page number (`page_with_row` and `tap_row` in
   `test_animation.py`); the audit table is where the per-page lists
   belong.

   A pushed page whose row list depends on stored data (the schedule list,
   the Parameters page) has no fixed entry in `CATEGORY_PAGES`. Audit it
   from the scenario at visit time with `rig.audit` instead.
6. A new source file under `src/` needs `touch src/CMakeLists.txt` before the
   ESP-IDF builds pick it up; the glob has no `CONFIGURE_DEPENDS`.

## Running the tests

Use the `pio` on PATH, one PlatformIO install per checkout (`CLAUDE.md`).

The value model, on the host, 59 cases, a few seconds:

```
pio test -e native_settingsui
```

The swatch ramps, on the host, against the real palette code:

```
cd tools/animbench && make check
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
