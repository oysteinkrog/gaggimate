# GaggiMate (idf5 branch): agent notes

ESP32-S3 espresso machine controller + display. The display is a LilyGo T-RGB
480x480 round RGB panel scanning out of PSRAM; the controller is a separate
board reached over BLE. Firmware builds with PlatformIO (`pio run -e display`
for production, `-e display-loadtest` for the bench rig).

## Knowledge base (`knowledge-base/`)

Curated, citation-strict qmd collections, same harness as the Initial Force
monorepo. Before writing or tuning a kernel, reading a cycle number, or
making a load-bearing claim about the Xtensa core, query the KB first:
`qmd query "<question>" -c esp32s3-assembly-optimization-kb --limit 10`
(`-c` is mandatory). Routing table and conventions in
`knowledge-base/CLAUDE.md`; query craft in `/kb-query`; new topics via
`/kb-new-topic`. The Animation kernels section below is the operational
summary; the KB carries the sources and the measurements behind it.

## Hardware invariants (violate these and the display regresses)

- **The panel's interrupts must live on core 1.** `esp_intr_alloc` pins an
  interrupt to whichever core runs the allocating call, and the BLE link-layer
  ISR (RWBLE, priority 3) lives on core 0. When LCD_CAM and DMA_OUT_CH0 shared
  core 0 with it, every radio event preempted the bounce refill past its ~600us
  of slack and painted a displaced band. `Controller::setupPanel` therefore
  creates the panel from a task pinned to core 1 and logs `panel init on core
  %d`; that line is the regression tripwire.
- **Bounce pool stays at 16 scanlines total** (8 buffers x 2 lines, 15,360 B).
  Deeper pools starve the WiFi TX buffer pool: at 20+ lines two concurrent
  browser tabs kill the web UI (measured cliff between 14.7 and 18.3 kB DMA
  free). History in platformio.ini above `GM_LCD_BOUNCE_LINES`.
- **Pixel clock is n=6 (13.33 MHz, 50.7 fps), and the stored setting can
  override the build default.** `panelClockDiv` in the device settings (web
  UI "Panel refresh rate") is applied over `RGB_MAX_PIXEL_CLOCK_HZ` at boot,
  so a device can run n=5 while the build says n=6; the bench machine did,
  and every garbling measurement before 2026-09-03 was taken at n=5 without
  knowing it. `/api/debug/pclk` reports the live divider: check it before
  measuring anything. At n=5 the panel consumes 28 MB/s of PSRAM and the
  bounce refill copies at ~28 MB/s whenever core 0 runs flash-resident code
  (BLE host on every controller message, WiFi), because flash and PSRAM
  share the MSPI bus; the refill falls a few buffers behind, stays there for
  milliseconds, laps the 8-buffer pool and the VSYNC square-up displaces one
  band. At n=6 consumption is 24 MB/s and no copy exceeded 190 us against
  632 us of slack. `panelclock::MIN_USER_DIV` floors the stored setting at 6
  (the debug endpoint is not floored). History in platformio.ini above
  `RGB_MAX_PIXEL_CLOCK_HZ`.
- Vendored ESP-IDF files are patched by `scripts/patch_*.py` (pre-build
  extra_scripts). Each keeps a pristine `.gm-orig` beside the patched file.
  The Windows and WSL PlatformIO installs share `~/.platformio`; a
  Windows-side build can clobber the patched sources, and the patch scripts
  re-apply on the next WSL build; if a display fault appears out of nowhere,
  check the patches applied in the build log first. **One PlatformIO
  install per checkout: the `pio` on PATH (`~/.local/bin/pio`, pipx, core
  6.1.19), never `~/.platformio/penv/bin/pio` (6.1.18).** PlatformIO deletes
  the whole `.pio/build` tree whenever the project checksum changes, and the
  checksum includes the core version, so two installs used on one checkout
  wipe each other's env trees on every build: on 2026-09-05 that happened
  twice under four running rig workers, and each rebuilt kdev ELF has a new
  sha (build metadata), so the board had to be reflashed before kb.py worked
  again. `pio run -t compiledb` for kdev must follow the same rule. One patch targets the
  LVGL libdep rather than the framework: `scripts/patch_lvgl_meter_inv.py`
  (per-env, into `.pio/libdeps/<env>/lvgl`) gives lv_meter scale-lines
  indicators sector invalidation. If dial updates ever get slow again, check
  it applied for that env.

## UI-pipeline invariants (violate these and touch latency regresses)

The background animation owns the panel on active screens; LVGL redraws are
snapshotted into an RGB565+A8 overlay the render task composites. Getting a
telemetry-driven screen from a 650 ms LVGL pass (1.5 Hz widget updates,
~750 ms touch) down to ~110 ms per visual refresh (~7 Hz, animation at
15 fps) took four load-bearing arrangements:

- **The UI task has core 1 to itself; the animation render task lives on
  core 0** at priority 1, under SleepPush (2) and Controller::loopLogic (3).
  When render shared core 1 with the UI task at equal priority they
  round-robined and each ran at half speed exactly when both were busy.
  Only compute moved: the panel's interrupts stay on core 1 (setupPanel).
- **While flushes are suppressed, LVGL must not render at all.**
  `lvgl_helper_suppress_flush` parks the refresh timer (period, not pause;
  `_lv_inv_area` un-pauses on every invalidation) and
  `lvgl_helper_take_dirty_rects` harvests `disp->inv_areas` directly. The
  render-and-discard pass it replaces cost more than the snapshot render
  that actually feeds the overlay.
- **Dirty tracking is a rect list, not one bounding box** (GM_DIRTY_RECT_CAP
  everywhere; the caps are static_assert-linked). A union box between two
  far-apart widgets is a full-screen snapshot.
- **Meter updates must stay sector-sized**: the vendored patch above plus the
  clip precheck in `action_on_meter_draw` (eez/actions.cpp), which skips
  ticks outside `draw_ctx->clip_area` before paying rounded-cap mask setup.
- **While the animation composites, the dial rings are compositor elements,
  not LVGL draws** (gm-2cl.6, `TickRingElement.h`,
  `DefaultUI::serviceDialElements`). A dial with `LV_OBJ_FLAG_USER_1` set
  is owned by the render task: its draw handler paints no ticks, its value
  setters record the value and skip the invalidation (the second hunk in
  `scripts/patch_lvgl_meter_inv.py`), and its tick cache slot is pinned
  because the render task reads the sprites from the other core. The
  element writes the same bytes the overlay path would have (alpha
  `(255 * s) >> 8` from the sprite byte, then `blend565`), so the ring is
  pixel-exact at rest; the lit edge eases over about 90 ms. The flag is set
  and cleared only on the UI task, released before any screen change,
  animation stop or tick-length morph, and a released slot stays pinned
  for two more frames. `/api/debug/anim?dials=0` puts the rings back on
  LVGL for an A/B (`elem_rings` counts the owned rings);
  `tools/dial_elem_check.py` is the device check and
  `tools/tickringbench` the host one. The brew progress bar's fill is a
  RoundRect element on the same terms (`serviceBarElement`). What the
  element costs is the pixel work, and the row path had to be trimmed to
  reach parity (2026-09-07, bench board, divider 8, synthetic brew, cap
  45): with the two rings owned, 6.7 ms of element a frame against the
  7.3 ms of overlay blend the ring pixels no longer cost, frame rate
  within run-to-run noise of LVGL's (13.7 to 15.2 fps against 14.2 to
  14.3), zero differing stable pixels. The first version, which read
  every box row and re-derived each tick's colour per row through a
  flash-resident float call, cost 10.6 ms and lost 2 fps; the span table
  per tick row, the per-frame colour table (`Element::ringColors`) and
  the per-row tick bitmask are what brought it down, in that order of
  effect. The refresh count under the brew stays near 3 a second in both
  modes because the value text still goes through LVGL (gm-2cl.5).
- **Live labels are Text elements, and an owned label keeps its hidden
  flag** (gm-2cl.5, `GlyphAtlas.h`, `DefaultUI::serviceTextElements`,
  `scripts/patch_lvgl_label_elem.py`). A label on the active screen whose
  text has changed once since the screen was entered is live; up to six
  live labels get `LV_OBJ_FLAG_USER_2`, which the patched lv_label reads
  as "draw nothing, do not invalidate on set_text", and the patched
  lv_obj_pos skips the size and move invalidations for it too (a
  content-sized label resizes on every width change, and that was the
  rectangle still refreshing after the first version). The UI task
  rebuilds the glyph list after every ui_tick from an A8 atlas in PSRAM
  (one 32 KB arena per font, glyphs copied once from the flash font
  through the 4bpp opa table) and places each glyph the way lv_draw_label
  would. Hiding the label instead does not work: the generated flow code
  reads and writes the hidden flag of the status bar labels every tick
  and un-hid them within one pass (2026-09-08, `text_dbg` counters). The
  element reproduces the overlay writer's alpha rule, `(opa * mask) >> 8`
  (`patch_lvgl_setpx_fast.py`), so full coverage lands at 254 and is
  blended, never copied: with a copy at 255 the interiors were one step
  brighter and 69 stable pixels differed; with the rule, zero
  (`tools/text_elem_check.py`, bench board, brew screen, whole-frame
  path). A numeric value eases toward the flow's value (150 ms time
  constant) so a reading counts instead of stepping; `textease=0` turns
  that off and `texts=0` puts the labels back on LVGL. `text_elems` on
  `/api/debug/anim` lists what is owned and `dirty_recent` the last 16
  rectangles LVGL invalidated, which is how the remaining brew-screen
  churn was traced to the size refresh and then to the 40x40 scale icon
  the flow blinks (gm-2cl.7).

- **The render loop lives in IRAM** (`renderLoop`, `renderFrame`,
  `presentFrame`, `pushLoop` and the scrim rows, `SleepAnimation.cpp`). The
  two cores share one 16 KB instruction cache, and every LVGL pass on core 1
  evicted the loop: with the code in flash, band, push and blend each doubled
  under a telemetry screen refreshing 3 times a second and the loop ran at
  17 fps; in IRAM, 28 fps under the same load (2026-09-07, Starfield, cap 45,
  bench board). The pins cost about 8 KB of internal RAM, since IRAM text
  past the first 16 KB is taken from DRAM one for one; pinning every kernel
  would cost 11 KB more and was not done.
- **What is left of the churn cost is PSRAM bus contention, not pixels.**
  LVGL's drawing and the snapshot copy contend with the render task for the
  bus. Compositing only the overlay's non-transparent runs (gm-2cl.11) saved
  12% of blend with plates on and nothing without them, so it was measured
  and not shipped. The lever against churn is fewer LVGL passes (gm-2cl.5,
  .6, .7).
- **The blend kernel itself is compute-bound, not bus-bound** (gm-2cl.14,
  2026-09-07, bench board, brew screen, cap 45, interlace pinned on). The
  blend probe (`/api/debug/anim?probe=4..7&probereps=1|2&bpie=0|1`,
  loadtest builds, `tools/blend_probe.py`) blends one captured overlay row
  into every band with the source and destination each placed in internal
  SRAM or PSRAM, once and twice per band so fixed costs cancel: 95 ns per
  pixel from SRAM, 75 from PSRAM, 79 for the scalar kernel with everything
  internal, so the memory does not set the cost and the vector kernel
  (`blendRowPie`, default on) buys nothing on that row. That is about 20
  cycles for a 16-bit alpha blend, and gm-2cl.16 is the kernel work it
  justifies. About 3 ms a frame of the blend stage is fixed cost (scrim
  pass, row and run walk, per-band timer reads), not pixels. Stage
  breakdown at rest on the brew screen: band 9.5 ms, push 8.3, blend 5.7,
  element 3.4, frame 32; under an LVGL pass band and blend nearly double
  with the same pixels, which is the contention above.
- **The overlay is planar and the blend stage's fixed cost was the scrim
  pass** (gm-2cl.16, 2026-09-08). The overlay is an RGB565 plane and a
  16-bit alpha plane (255 stored as 256, `gm_overlay_plane_px` pixels each,
  16-byte aligned PSRAM); LVGL writes it through `gm_set_px_planar` and the
  second hunk of `scripts/patch_lvgl_setpx_fast.py`. The snapshot's x
  margin is rounded to a multiple of 8 (`DefaultUI::overlayExtX`) so every
  aligned group is one vector load (`blendPlanarGroups`, exact against the
  old kernel: probe 4 mismatch 0). The kernel changed nothing at 9.5k
  pixels: the pixel work is about 3 ms either way and it is PSRAM line
  fetches, about 1.3 us per 64-byte line, not compute. The fixed cost was
  the text-halo scrim pass at 2.7 ms a frame: its per-cell-row expansion
  was reset per band and so ran on every interlaced row. Per frame now
  (`expandedCy`), the pass is about 2.0 ms, half of it the kernel. The
  scrim=0 A/B that had said "no change" was void: the UI pass re-applies
  the stored scrim through `setScrim` every tick, so the knob lasted one
  pass. `scrim=N` is now an override that holds until `scrim=-1`, and the
  JSON reports `scrim_override`. Probe levels 5 to 9 leave the blend row
  at fixed points (loop header, row head, after the scrim, without the
  expansion, without the scrim kernel) so a slice reads off `blend_us`;
  `blend_scrim_us` reports the scrim pass. `tools/blend_ab.py` is the
  production A/B. Ruled out, one build each: render stack placement,
  object placement, render priority, DMA completion ISR core, CPU push
  instead of DMA, panel refill duty (divider 8 to 16). `scale565Oct`, the
  BandDma submit path and `scanoutMark` were flash-resident and called per
  band from IRAM; pinned now, about 660 B, push 5.7 to about 4.5 ms.
- **The overlay footprint per page is measured, not guessed** (gm-2cl.15,
  `tools/overlay_footprint.py`, report under `tools/overlay_footprint/`).
  `ov_px` on `/api/debug/anim` is the overlay pixels inside the composite's
  run spans, counted at publish; it matched an offline count of the
  `/api/debug/ovl` dump exactly (7,962 both, brew screen). Bench board,
  2026-09-08, interlace pinned, cap 45, divider 8, Starfield, per frame:
  standby 8.4k px and 2.7 ms of blend; menu 13k and 4.9; brew 8.0k and 4.7
  (plus 3.4 ms of dial elements); steam 4.6k and 3.0; water 5.5k and 3.7;
  status 10.6k and 5.6 (plus 5.0 of elements); grind 5.3k and 3.9; profile
  6.0k and 3.9; the settings pages 7.7k to 13.3k and 2.9 to 4.0 with no
  elements. Two pages are the outliers: new_profile at 104k px and 9.2 ms,
  and info at 41k and 6.9, both because of large translucent panels. The
  brew screen's 106k figure quoted above was taken with the plates on; the
  bench board has them off, and the scrim (about 1.6 to 2.6 ms on every
  page) is now the larger of the two fixed costs.
- **The bench board stores `bgAnimInterlace` 0**, whatever the runner
  fixtures say, so a measurement that assumes the interlaced path must pin
  it (`interlace=1` on the debug endpoint, not stored) and say so. Every
  dial-element number above was taken on the whole-frame path (frame 52 to
  78 ms). The build default is 1 since gm-2cl.9 (2026-09-08, `Settings.h`
  `bg_ilace`, and the web form already fell back to 1); a stored 0 still
  wins on a device that has one, which is why the bench board needs the
  pin.
- **Rendering straight into the bounce ring without a framebuffer does not
  work on this bus** (gm-2cl.13, killed 2026-09-07). Two rounds, Starfield,
  standby screen, divider 8 (110 us per 2-row band): 36 to 45% of the 9,200
  band requests a second underran, even with the producer at priority 22 and
  its kernel in IRAM, because band plus blend cost 85 to 100 us per band and
  did not change with priority or placement. The producer is bus-bound. The
  panel garbles the moment it falls behind, so the framebuffer path stays and
  the scratch patch (`raster_poc.patch`) is the record.

Measure with `-e display-loadtest` (`GM_TOUCH_PROBE`): `GM_UISTAT` lines give
pass/snapshot/publish times and snapshot area per 5 s window; `GM_TOUCHLAT`
lines stamp press→overlay_publish→anim_frame per tap.

Three refresh rates, none of them the panel's (measured 2026-09-05 with
`C:\work\camshots\anim_fpsprobe.py`, which reads `anim_frames` on
`/api/debug/anim` across a timed window; `frame_us` excludes the pacing sleep
and `frames` is the scan-out's counter, so neither is a rate):

- **The animation loop runs at the `bg_fps` setting (default 30), and with
  the default interlace on, each pixel row is refreshed every other frame:
  30 fps loop, 15 Hz per row.** With the slider at 60, nine animations reach
  41-52 fps interlaced (20-26 Hz per row) and the five heavy ones (lava,
  aurora, mandala, ember, nebula) 27-34, with zero scan-out slips in every
  window. The same loop with interlace off (whole frame per frame) is 16-25
  fps for every animation and cannot exceed 25.3: the cheapest full frame is
  ~24 ms of work (orbits: band 3, blend 6-7, push handoff 3-4, band-DMA slot
  wait 6-7, ~5 of per-band overhead over 240 bands) and the flip waits for
  the scan-out, so anything over one 19.7 ms panel period takes two. Half
  resolution buys nothing (the 2x expansion costs what the render saves),
  the CPU push is slower than the GDMA (16 MB/s vs 21.5), and a slower pixel
  clock (div 7, 8) does not shorten the work. `slotwait_us` on the debug
  endpoint is the render task blocked on the band DMA. The direct path pushes
  only each row's chord (`dmaCrop`, `crop=0` turns it off): 15% less work
  per interlaced frame and +1-2 fps at cap 60, invisible at cap 30 or on the
  quantised full-frame path.
- **The panel scans at 50.7 Hz** regardless (n=6), so a full-frame animation
  at the panel rate needs render+push under 19.7 ms per frame. Nothing in
  the fleet is within 4 ms of that on the standby screen.
- **The LVGL overlay refreshes at ~7-9 Hz** (the ~110 ms pass above), and
  a widget that moves through LVGL moves at that rate or less: the
  motion test (`/api/debug/anim?uianim=1`, a 120 px plate on an lv_anim)
  measured 3.9 to 5.4 refreshes a second, 45 to 140 ms per refresh, and
  the pixel writer is not where the time goes (a plain-store writer
  changed nothing). Motion at the loop's rate goes through **layers**
  (`SleepAnimation::layer*`, `DefaultUI::moveObjectViaLayer`): the object
  is rendered once into an RGB565+A8 sprite, hidden from LVGL, slid by
  the render task, and handed back when it lands. `uianim=3` is the same
  plate through a layer: 29.2 fps at cap 30, 32.7 at cap 60 (2026-09-05,
  standby screen). What a layer costs is PSRAM reads, about 170 to 250 ns
  per non-transparent pixel per frame (the vector and scalar blends measure
  the same), plus the sprite's rows going out whole while it moves: a
  120x120 plate is 2.5 ms of composite and about 6 ms of frame in all, so
  moving elements should stay around 150 px, and a full-screen slide
  through a layer (230k px, 40 ms a frame) is not a 30 fps transition.
  The same rate applies to the overlay itself: the brew screen with 60%
  plates is ~106k non-transparent pixels and 12 to 13 ms of blend a frame
  at any render priority, so how much of the screen is translucent over
  the animation is the budget knob for everything above.

## On-display settings (violate these and an edit is lost or a target is unreachable)

The settings screens are code-built LVGL in
`src/display/ui/default/settings/`: a host-testable value model
(`SettingsModel`), the shell (`SettingsUI`), seven row widgets
(`SettingsRows`) and one `Cat<Name>.cpp` per category.
`src/display/ui/default/settings/README.md` is the working guide: file
map, how to add a category, how to run the tests. This section is what
the design cannot show and what the runs measured.

- **The settings tile is the centre of the menu** (decided 2026-09-06, gm-z7x).
  `DefaultUI`'s menu-screen effect puts `btn_settings_1` at (0, 0) without
  its 15 px click pad, hides the generated info button and the
  BREW/STEAM/WATER word, and spreads the mode tiles evenly on a 140 px ring
  as the menu did before the settings work: four at 45 degree offsets, or
  three at 120 degree steps, brew first, clockwise from the top (100 px
  tiles with an 8 px click pad; the icons are 80 px whatever the tile
  size). The info screen,
  the only place that shows the WiFi setup QR code while the config access
  point is active, is opened by the Status category's "Device info" row.
  The menu audits clean on the simulator and the bench board; the audit
  works in axis-aligned rectangles, so the pads are what set the spacing
  (an 80 px gear with no pad and 100 px ring tiles with 8 px pads leave
  1 px between the centre box and the corner tiles' boxes).
- **Settings is a cover on the menu screen, not a screen.** `SettingsUI::open`
  creates one full-screen child of `objects.menu_screen_new`, hides every
  other direct child except `objects.status_icons`, and restores their
  captured visibility on close. `currentScreen` stays
  `SCREEN_ID_MENU_SCREEN_NEW` for the whole visit, so the animation host,
  the plate handling and the screen-change path in `DefaultUI` never see a
  settings visit at all. The runtime-built scale overlay
  (`DefaultUI::buildScaleScreen`) is the precedent for the construction and
  for the delete-event pointer cleanup.
- **No translucent plate on a settings page.** The cover sets `bg_opa` to
  `LV_OPA_TRANSP` and every row is text and icons on the screen background
  (`SettingsUI.cpp`). Translucent pixels are the render task's budget knob:
  the brew screen's plates are about 106k non-transparent pixels and 12 to
  13 ms of blend a frame (UI-pipeline invariants above). The bench board
  measured 29.7 to 30.6 fps on all nine settings pages against a 27.9 fps
  menu-screen baseline at cap 30, so the cover costs the animation nothing
  (device runner report, `report.json` pages and the `baseline` step,
  2026-09-06).
- **Every route out of a category commits it.** Visual fields apply on every
  change and call `DefaultUI::markDirty()` so the next UI pass shows them
  rather than the 2.5 s idle rerender: main and standby brightness
  (`CatDisplay.cpp`), and animation, frame rate, all-screens, theme,
  gradient, plates, tint and scrim (`CatAnimation.cpp`). Everything else is
  written to `Settings` by the category's `commit`, which runs when its page
  is removed by any route, and writes only the fields the visit touched.
- **`handleScreenChange` calls `SettingsUI::onExternalLeave()` before
  `eez_flow_set_screen`** (`DefaultUI.cpp`). A standby timeout, a controller
  mode change or a brew start mid-edit therefore commits and tears down the
  same way the exit chevron does, instead of leaving a stale cover on a
  screen that has moved underneath it.
- **A web save while a category is open is per-field last writer wins.** The
  web values land first, `settings:changed` sets the shell's flag,
  `SettingsUI::service()` refreshes on the next pass every row the user has
  not touched, and the touched fields keep their draft and win at commit. A
  web-requested restart reboots from the web task at once and an uncommitted
  draft is lost, the same as a power cut, and that is accepted.
- **`reconcile` reaches only the top page.** A pushed child page (the
  schedule list, the schedule editor) refreshes its parent's draft itself
  through `machineDraftReconcile` (`CatMachine.h`), because `kCatMachine`'s
  own `reconcile` never runs while a child is on top.
- **Lifecycle order is fixed.** `pushPage` runs `enter` under
  `Settings::Guard`; `popPage` and `teardownAll` run `commit` under the
  guard, delete the page root, then call `destroyCtx`. Root before ctx,
  because a row's DELETE callback can still hold the ctx. `refresh` and
  `reconcile` take the guard too.
- **Deleting a page from inside one of its own row callbacks is safe in
  LVGL 8.4 and an upgrade must keep it that way.** `lv_obj_destructor` marks
  in-flight events deleted and `event_send_core` stops dispatching after the
  callback returns (`lv_event.c`), which is what `openCategory`, `pushPage`
  and `popPage` called from click handlers rely on.
- **Row widgets clear `LV_OBJ_FLAG_PRESS_LOCK`, which `lv_obj_create` sets on
  every child** (`SettingsRows.cpp`, `createRowContainer`). With the flag on,
  a press stays glued to the object it started on, so a finger sliding off a
  row or a row being disabled would never produce the `PRESS_LOST` that
  cancels the hold.
- **Every target presses the same way: 40% toward the touch dim colour.**
  `settingsPressedColor(rest, dim)` (`SettingsRows.h`) is the one rule. The
  generated screens get it from `DefaultUI::applyPressedFeedbackTo`, which
  walks lv_btn and clickable lv_img objects and so covers the arrows and
  chevrons; the tiles are plain lv_obj with a non-clickable icon and LVGL
  puts PRESSED on the tile, never its children, so `buildTile` recolours
  icon and caption by hand; the whole-row targets (toggle, action, confirm,
  unlock) dim their text through `PressDim`. Measured 2026-09-06 on the
  simulator as the mean framebuffer change per pixel inside the hit box
  while held: menu buttons 19 to 29, tiles 0 before the rule and 42 after,
  rows 32 to 49 before (text jumped to the dim colour itself and vanished)
  and 13 to 27 after. The probe is a hold through `/api/debug/tap` with a
  framebuffer read mid-hold; the device only delivers `/api/debug/fb` at
  step 2.
- **Holds are driven only by the events LVGL delivers to the row**, never by
  an `lv_timer` that could outlive it. Steppers step once on `PRESSED` and
  once per `LONG_PRESSED_REPEAT` (LVGL default: 400 ms, then every 100 ms)
  and never on `CLICKED`; a hold reports fast after 2 s
  (`kSettingsRowFastHoldMs`, `SettingsRows.h`) and the field's own `fastStep`
  applies, since there is no universal multiplier. Locked rows unlock after
  a 1 s press (`kSettingsRowUnlockHoldMs`) and relock when the category is
  left. Confirm rows act after a 2 s hold (`kSettingsRowConfirmHoldMs`).
- **Geometry: 320x56 rows, five per page** (`SettingsUI::kRowW`, `kRowH`,
  `kRowsPerPage`), 96x96 tiles on a 145 px ring (`SettingsUI.cpp`,
  `buildTile`). Every tappable target except the exit chevron needs an
  effective hit rectangle of at least 56x56 px, no overlap with another on
  the same page, and 12 px of clearance from the panel's edge circle. The
  chevron is exempt from the size and edge rules and its ext click pad is
  34 px on settings pages, not the 45 px the generated screens use: at 45 px
  it reached 10x6 px into the two lower tiles (measured by the runner's
  audit, 91cb0ed5). `Rig.audit()` in `tools/settings_ui_tests/rig.py` is the
  check, and the runner audits every page on every run.
- **Ranges live in three places that must agree.** `SettingsModel.h` owns the
  display's editing ranges, steps, wrap rules and formats. `Settings` clamps
  a few fields on store (`setBrewDelay` and `setGrindDelay` to 0 to 4000,
  `setBgAnimPlateColor` to 24 bits) and stores the rest as given, including
  `setMainBrightness`, `setStandbyBrightness` and `setBgAnimFps`. The web
  form has its own input constraints in `web/src/pages/Settings/tabs/*.jsx`.
  The animation roster comes from `BgAnim.h` and is mirrored by
  `web/src/config/bgAnimations.js`; the zone table comes from `zones.h` and
  is mirrored by `web/src/config/zones.js`.
- **Nothing range-checks a stored value on the web path, so a category that
  indexes with one clamps first.** `CatAnimation.cpp` clamps `bgAnimId` into
  the live registry at `enter` and at `reconcile`: a stored id from a longer
  registry, after a rollback to a build with fewer animations, wrote past
  the end of a heap vector on the first Gradient arrow press (91cb0ed5).
- **`Settings::lock()` orders whole transactions, not fields.** It covers the
  web save's `batchUpdate`, `doSave`, a category's `enter`, `commit`,
  `refresh` and `reconcile`, `Controller::loopLogic`'s delay auto-adjust
  writes, and since 91cb0ed5 the web settings GET and the auto-wakeup minute
  tick, both of which copy container-typed properties. `Property::get` still
  hands out a reference, so any new cross-task reader of a `String` or a
  vector property must take the guard as well.
- **A failed NVS write is reported, not swallowed.** `Settings::flushNow()`
  returns false and the Restart row shows "Save failed, hold to retry"
  (`CatStatus.cpp`). `GM_SIM_FAIL_FLUSH=1` on the simulator arms one forced
  failure (`Settings::debugFailNextFlush`) so the path is testable.
- **The Fixture tile (the sixth) exists only under `GM_TOUCH_PROBE` or
  `GAGGIMATE_SIM`** (`SettingsFixture.cpp`): one of each row widget, so the
  shell and the widgets stay exercisable whatever the five real categories
  do. Production links five tiles.

Instruments, and where each one exists:

- `/api/debug/tap?x=&y=[&ms=]` queues one synthetic tap through
  `TouchInject`; compiled where `GM_TOUCH_PROBE` or `GAGGIMATE_SIM` is set.
- `/api/debug/touchmap?screen=0` dumps the active screen's object tree with
  every settings object's `SettingsDebugTag` and, for value rows, the
  untruncated canonical text; compiled unless `GAGGIMATE_HEADLESS`.
- `/api/debug/settingsui` opens, closes and navigates the shell and reports
  its state; `GM_TOUCH_PROBE` or `GAGGIMATE_SIM`.
- `/api/debug/heap` carries `int_free`, `dma_free`, `dma_min` and `hot_fail`;
  everywhere.
- `/api/debug/synth?brew=0` stops the loadtest build's synthetic brew
  lifecycle so a measurement is not dragged back to the status screen;
  `GM_SYNTH_HANDSHAKE`, which only `display-loadtest` and `display-blestress`
  set.
- `/api/debug/anim` (frame counters, `anim_id`, `uptime_ms`, `text_elems`,
  `dirty_recent`, the `texts=`, `textease=` and `dials=` element knobs) and
  `/api/debug/pclk` (the live pixel-clock divider) are device-only: both sit
  inside `WebUIPlugin.cpp`'s real-panel block, which `GAGGIMATE_SIM` and
  `GAGGIMATE_HEADLESS` exclude.

Three test commands, and what each proves:

- `pio test -e native_settingsui` runs the value model on the host, 28 cases,
  no LVGL and no Arduino. It proves ranges, steps, wrap, formats, the zone
  split, the gradient map and schedule parsing.
- `python3 tools/settings_ui_test.py` builds nothing and launches
  `.pio/build/display-sim/program` itself, seeded from
  `tools/settings_ui_tests/fixtures/controller.json`. It proves navigation,
  interaction, values, persistence, theme colours and the geometry audit.
  Last full run: PASS, 316 checks across 7 scenarios, 16 pages audited clean,
  smallest target 56x56 px, about 10 minutes wall time (`out_sim2/report.json`,
  2026-09-06).
- `python3 tools/settings_ui_test.py --host 192.168.1.121` drives the bench
  board on a `display-loadtest` build and adds what the simulator cannot do:
  frame rates, heap, and a 20-cycle leak phase. Last run: PASS with
  `--only temps,display,machine,schedules,status,rig`, 120 checks across 6
  scenarios, 9 pages audited clean, page rates 29.7 to 30.6 fps against a
  27.9 fps baseline at cap 30, `dma_min` 32067 B, sampled `int_free` minimum
  46907 B, `hot_fail` 0 (`out_device6/report.json`, 2026-09-06).

What the device runs taught, beyond the numbers:

- **The bench board stores pixel-clock divider 8**, not the build default 6,
  so its rates are not comparable with a run at the default. The runner reads
  `/api/debug/pclk` and warns. This is the stored-settings trap from the
  hardware invariants above, hit again.
- **The Animation scenario cannot run on that board as it stands.** Its stored
  `elementTintColor` is `#FEC4A4`, which is not one of the twelve palette
  colours the tint row cycles through, so the preflight reports an unsupported
  fixture rather than changing a stored value it could not put back.
- **The board shows 12-hour times with a space-padded hour** (` 1:09:27 PM`)
  and its NTP clock sat 4 to 5 minutes off the host's, so a test compares
  parsed fields, never strings or the host's own clock.
- **Opening the shell through the debug route is not a touch.** On a board
  idle past its 60 s standby timeout the standby screen can take over right
  after the open and close the cover, which is why `tools/settings_ui_test.py`
  retries the open after a reboot (`_open_after_boot`).

Known limits, recorded rather than fixed:

- A malformed stored schedule time, which the web UI can write because its
  handler stores whatever string the browser sent, shows as 00:00 in the
  editor and as the raw string in the list (`CatSchedules.cpp`, through
  `settingsui::scheduleTimeParts`).
- `buildRegions` rebuilds the whole region span list, one `std::string` per
  zone entry, on every region or city arrow press (`SettingsModel.cpp`). The
  churn is unmeasured.

## Internal DRAM budget (violate these and the web UI dies)

The web UI does not die of bugs in the server, it dies of internal DRAM
starvation: every WiFi frame on its way out is a ~1630 B DMA-capable internal
copy of a PSRAM pbuf, and when that allocation fails the driver logs
`wifi:m f null`, the socket stalls and the NetworkWatchdog reconnect loop
never recovers. The device used to idle at ~16 kB internal free (8 kB
DMA-capable, largest block 7.7 kB) and two browser tabs killed it. After the
2026-09-04 reclaim it idles at ~56 kB (48 kB DMA-capable). Rules:

- **Service task stacks go in PSRAM when the task never runs with the flash
  cache disabled** (`xTaskCreatePinnedToCoreWithCaps` with
  `MALLOC_CAP_SPIRAM`): SleepAnim, SleepPush, Controller::loopLogic,
  ESPMemoryMonitor, mdns. Anything that touches NVS, LittleFS, SD or
  `esp_flash` stays internal (Settings::loop, ShotHistory, DefaultUI::loop,
  async_tcp). A WithCaps task must never delete itself: that spawns a helper
  task that needs internal heap and aborts without it. Finished tasks park
  and the owner reaps them (`SleepAnimation::reapTasks`).
- **`/api/debug/heapmap` is the instrument**: internal regions, block-size
  histogram, and every task's stack size and high-water mark. Size stacks
  from the measured `hwm`, not from guesses. `/api/debug/heap` carries
  `dma_free`/`dma_min` and the asset gate counters.
- **Animation tables come from a fixed 12 KB slab, never from the heap
  pool** (`bganim::allocHot`, BgAnimCommon.h; `bganim::alloc` is PSRAM,
  always). Before the slab, placement was decided at init() against the free
  pool, and after the reclaim the pool idled within a few kB of the 48 KB
  reserve, so the same table landed in SRAM on one boot and PSRAM on the
  next: band time swung 2x per boot, and the DRAM left for WiFi depended on
  which animation was running. The slab is static (it is in the linker's RAM
  figure: 98,312 B with it), 3,072 B hold the shared sine/cosine LUTs for the
  boot, 9,216 B belong to the resident animation, and a table that does not
  fit falls back to PSRAM and counts in `hot_fail` (`/api/debug/heap`). An
  animation's static tables are not a way around it: BSS is the same pool.
  With the slab a normal boot idles at ~53 kB internal (45 kB DMA-capable)
  with the boot animation resident.
- **Big embedded assets stream at most three at a time, and a second or
  third only while `dma_free` is above 20 KB** (`kMaxAssetStreams`,
  `kAssetGateDmaFloor`, `WebUIPlugin::assetSlotFree`). In-flight WiFi copies
  scale as connections x TCP_SND_BUF/MSS; without the gate three cold tabs
  drained the DMA pool to 276 bytes. Extra requests are parked with request
  continuation, never refused; the first stream is always admitted so a
  parked request cannot wait on a pool nothing drains. Measured 2026-09-04
  with the slab, 3 rounds each: 2 tabs dma_min 15 kB, 3 tabs 6.8 kB, 4 tabs
  2.3 kB, zero request failures and zero refused WiFi allocations on a
  normal boot; on an AP-fallback boot 4 tabs produced 9 transient refused
  1630 B allocations and still no request failures.
- **`TCP_SND_BUF=5760` and the WiFi IRAM opts off travel together**: the
  four-segment send buffer is what makes the 437 kB bundle load in ~1.6 s
  instead of 2.75 s (the link is round-trip bound at ~18 ms under BLE coex
  and modem sleep), and the 17.7 kB the IRAM opts return is what pays for
  the in-flight copies. Rationale and the measurements behind every knob,
  including the ones tried and rejected, live above each setting in
  `sdkconfig.gaggimate.defaults`; read them before touching WiFi or lwIP.
- **Boots that fall back to the config AP idle ~7-11 kB lower** even after
  the STA recovers (`WifiStaWd: AP fallback active`). The bench's mesh has
  two BSSIDs and roams during boot, which trips the connect timeout about
  one boot in four; check the serial log for `softAP` before comparing idle
  numbers between boots.
- **Every gradient-editor preview holds the panel for 15 s**; a settings save
  that touches the animation fields ends the preview so the saved state wins
  immediately (WebUIPlugin::handleSettings).

Test rigs for all of this live in `C:\work\camshots` (Windows Playwright
venv `pwenv`, real Chrome): `pw_gradient_rounds.py <n> [cold|warm] [drag]`
(edit gradient, save, verify the framebuffer against the expected ramp via
`fbclass.py`), `pw_multitab.py <rounds> <tabs>` (simultaneous cold loads),
`pw_run.py` wrapper (required: a Windows Node process started from WSL1 needs
its stdio redirected). Run them from WSL with `pwenv/Scripts/python.exe`.
Verify the panel through `/api/debug/fb`, never the camera: the photos are
too dark to classify.

## Animation kernels (violate these and band time regresses silently)

The 13 background animations' band() hot paths went through a hand-written
Xtensa pass (2026-09-04, 13 Fable workers in parallel, four rounds). What
survived, and what the device taught:

- **Table placement beats instruction count.** The same kernel ran 1.3x to
  2x slower with its per-pixel tables in PSRAM than in SRAM (plasma 9.1 vs
  17.8 ms per full-res frame, caustics 22.6 vs 37.5, lava 26.0 vs 47.7,
  aurora 46.7 vs 67.6). The hot slab (DRAM section above) makes that
  placement a decision in source: `allocHot` for tables read per pixel or
  per row, ranked by reads per frame within 9,216 B, `alloc` (PSRAM) for
  bulk sequential sweeps. An animation that needs more than the slab shrinks
  a table; it does not get more slab.
- **GCC 14's schedule is the baseline, not the target.** Kernels that won on
  static instruction count lost on the chip (mandala 0.85x, silk 0.73x of
  the compiler's own bandRef) because the device pays for load-use stalls
  and cache misses, not instructions; both PIE-decode-into-scratch designs
  lost to keeping the index in a register. The kernels that won transcribed
  GCC's loop first and then found an edge (orbits 2.0x, caustics 1.5x,
  nebula 1.3x, ripples 3x to 5x depending on ring state). Lava, silk and
  Silk 2 got their kernels last (2026-09-05, written with the board
  offline, each behind a flag: `GM_BGANIM_LAVA_ASM`, `GM_BGANIM_SILK_ASM`,
  `GM_BGANIM_SILK2_ASM`), bit-exact under QEMU and on the device, with
  portable twins so the flag-on glue also runs through `make check`, the
  interlace check and the fuzz. The device then decided the defaults: Silk 2
  wins (1.16x, on), lava ties (0.99x, on), silk loses (0.87x, the third
  silk kernel the compiler has beaten on the chip, so `GM_BGANIM_SILK_ASM`
  defaults to 0 and bandRef renders; `-D<flag>=1` re-enables it for the
  next attempt). Parity is the expected result of transcribing GCC's loop;
  the wins came from an edge the compiler cannot take (a closed loop, a
  walking pointer, a gather the PIE unit does in one instruction).
- **Every kernel keeps its portable C++ as `bandRef` on the BgAnimation
  struct**, and the ladder to change one is: host goldens exact
  (`tools/animbench make check`), the real device compiler's disassembly
  (`tools/animbench/xtensa-asm14.sh <name>`, flags and toolchain of the
  firmware build, not the older xtensa-asm.sh), bit-exact execution in QEMU
  (`tools/qemubench/build.sh tests/anim_<name>` then `run.sh`, greps
  `GM_QEMUBENCH_PIE: PASS`), then the device: `/api/debug/animtest?anim=N`
  renders 8 frames x 3 parameter sets through band() and bandRef() back to
  back with alternating order and reports `mismatch_px` (must be 0) and the
  first differing pixel; `/api/debug/anim?useref=1` swaps the render loop
  to bandRef so the speed claim is measured under production conditions,
  not estimated. `C:\work\camshots\anim_rung4.py [ids]` runs both for the
  fleet; `anim_devbench.py` alone does the A/B (`RESERVE=` pins the band
  buffers' pool). Nothing else is evidence: instruction counts and host
  timings predicted wins the device reversed in 4 of 13 animations.
- **A production kernel never writes CPENABLE.** FreeRTOS enables the FPU
  and PIE lazily per task through the coprocessor-disabled exception, which
  is also how another task's coprocessor state gets saved; a kernel that
  sets CPENABLE itself skips that and can corrupt Controller::loopLogic's
  float state. The bare-metal QEMU harness sets it once in its own main().
- **ee.vld/vst.128.ip mask the low four address bits silently**, so a PIE
  kernel aligns its spans with a scalar prefix, never by trusting the
  pointer; `allocHot` returns 16-byte-aligned tables for this reason.
- BAND_H is 2 (240 band() calls per frame), so per-call setup is paid 240
  times: a kernel's row-state builder is as hot as its pixel loop.
- **Iterate on the device, not on predictions**: the `display-kdev` env
  hot-loads one animation source over HTTP into an IRAM buffer and times it
  with the cycle counter (`tools/kblob/kb.py run Anim<X>.cpp --anim N`,
  README alongside). Blob and firmware variants are timed in turn with the
  whole hot slab each, min-of-n per band, and hashed against the firmware's
  band() for equality; a round is seconds, not a flash. It needs memory
  protection off (sdkconfig.kdev.defaults), so it is a bench env and never
  a production knob, and it idles 16 KB lower on internal free than
  production. `pio run -t compiledb` recreates the env's build tree (ELF
  included) and writes the one project-wide compile_commands.json, so run
  it before the build, not after, and never for another env in between.
  **Adding or removing an env in platformio.ini also changes the checksum**
  and wipes every env's build tree (2026-09-07: one POC env cost a 7 minute
  loadtest rebuild before the board could be restored). Never edit the env
  list while a runner is on the board; rebuild what you flash next after
  the edit.
  Three things the first evening on it taught: never reflash the board
  while a round is running (the `band` rows become a snapshot of whatever
  the tree held at build time, and one worker spent an hour comparing
  against its own change); a `kb: LEAK` line means a table without a
  `release()`, and until the bench learned to reset the slab one leaking
  candidate put every later bench on that boot into PSRAM (nebula 1.7x
  slower, silently); and for an animation whose `frame()` carries state
  (nebula's scroll, starfield's RNG) the check that holds is blobref vs
  blob, not blob vs the firmware, whose globals hold the panel's history. And
  kbench times each band as the minimum of n back-to-back calls, so a design
  that caches a row across band() calls is mis-measured in either direction
  (repeats hit the cache and sample nothing, or miss twice per call); those
  designs are timed by the useblob production A/B, never by `min_ms`.
  The rig also settles where a frame goes, which the loop body cannot:
  silk's per-pixel body was already one add, one shift, one gather and one
  store, yet the frame was 13.4 ms, and five probe blobs in twenty minutes
  (probe off, grid 16, grid 32, pairs, both) showed the per-cell node work
  was a third of it and the exact fallback 0.7 ms. Grid 16 plus paired
  stores took it to 8.2 ms with the picture unchanged (goldens moved 2.0 of
  255, all of it the dither grain going 2x1); a redesign for the same speed
  (Silk 2, four rounds) never matched the look.
- **The fuzzer is only a fuzzer with the sanitizers on**
  (`tools/animbench/Makefile.fuzz`, run with
  `ASAN_OPTIONS=verify_asan_link_order=0` on WSL1). Without ASan a
  one-entry table overrun reads the neighbouring byte and passes; that is
  how silk shipped a palette pad of 4 against a dither amplitude that
  ditherAmp() caps at 16, reading past its LUT at the default parameters.
  Any animation with an unclamped, padded gather sizes its pad from that
  cap, and a change to a table's layout re-runs the fuzz for the fleet.
- **A row's pixels depend on its absolute y and the frame state, never on
  which other rows share the band() call.** Production's interlaced path
  (SleepAnimation.cpp, `splitRender`/`renderSkip`) calls band() with
  rows==1 and parity-skipping sequences, and the half-resolution path hands
  it 240-wide rows. Row doubling that copies from a neighbour inside the
  call's buffer, or picks "the real row" from the call-local offset, passes
  the golden diff and paints wrong rows on the device; three of the four
  2026-09-05 redesigns did exactly that until `tools/animbench`'s
  interlace_check caught the first at integration. The shape that passes:
  derive everything from the pair row `y & ~1` and memcpy only when the
  partner is in the same call. `render_one --shapes` runs the same check on
  an unregistered candidate (480 and 240 wide) before it touches `src/`.

## Measuring the display rig

Use `tools/rig_soak.py` (build + flash + serial soak + analysis in one
command; `--record` is a flight recorder). Rules it encodes, which also apply
to any new measurement:

- Telemetry rides the serial log (`GM_SCANOUT`/`GM_SLIP`, loadtest builds
  only), never HTTP: the rig's DMA ballast starves the WPA2 handshake, so
  WiFi drops exactly when the rig is working, and an HTTP-sampled rate is
  biased toward healthy-radio periods. Measurement channels must be
  independent of the subsystem under test.
- Rates are computed within one boot from device time, starting at
  t_us >= 90s (boot churn: BLE boost, WiFi association, heap cliff).
  t_us going backwards is a reboot.
- Run-to-run variance is ~2x. Below ~0.2 events/s, compare configurations by
  toggling within one run, not by flashing alternately.

Debugging methodology that this codebase has already paid for:

- Enumerate static configuration before building statistical instruments:
  `esp_intr_dump()` (one call) names every ISR's core and priority and found
  in five minutes what weeks of rate correlation could not.
- Phase evidence cannot separate two sources with the same period. The 1s
  grid fit both the PHY PLL-track timer and the BLE client scan interval;
  enumerate all same-period sources before convicting one.
- Distinguish "the copy was slow" (bus contention) from "the interrupt was
  late" (preemption) before picking a fix: the busy/gap histograms and the
  catch-up counters in the patched esp_lcd driver exist for exactly this.
  Mind what a metric contains: `gap` is callback entry to entry, so it holds
  the previous callback's copy time, and a "1000 us gap" was a 990 us copy
  with a 2 us interrupt latency behind it. The gaplog's `prev_busy_us` and
  the chunk timer were added to stop that misreading (a lock probe over every
  FreeRTOS critical section had already cleared the kernel of blame).
- Include the device's stored settings in "static configuration". The stored
  pixel-clock divider silently replaced the build's; enumerate what NVS can
  override before trusting a compile-time constant.
- Read a debug knob back after a UI pass before trusting an A/B on it. The
  scrim knob was overwritten by the settings refresh every tick, and a
  whole evening went into stack, object, priority, ISR-core and DMA
  experiments against a "fixed cost" the knob would have named at once.
  A knob that a periodic path also writes needs an override, not a set.
- Bisect a stage by exit point, not by theory. Probe levels that leave the
  loop at fixed points (`probe=5..9`) named the scrim block in one build
  after five builds of in-place cycle probes had cleared every instruction
  around it.

## Bench facts

- Device: 192.168.1.121 on the bench, UART on COM3.
- Windows tooling runs Python 3.10 (`GM_RIG_PY` env var to override):
  Python313 silently lacks esptool and pyserial.
- Camera verification: `C:\work\camshots\grab.bat <file>` (one frame),
  `burst.bat` (8s at 6fps). Photos land in C:\work\camshots.
- `/api/settings` returns the WiFi password in cleartext: never dump it, and
  never POST to it by hand (the web UI is the only safe writer).

## Open cleanups

- The PHY PLL-track deferral (`scripts/patch_phy_track_defer.py`,
  PanelClock.cpp) predates the core-1 interrupt fix and is probably vestigial
  now: with the panel's interrupts off core 0, the tick cannot touch the
  refill. Candidate for a measured revert-test; it costs a framework patch to
  maintain across IDF updates. The BLE scan backoff and the 5s PLL-track
  period stay regardless (less coex churn for free).
- `tools/animbench/OPTIMIZE.md`: do NOT modify golden/, BASELINE.md, or bench
  sources.
- The DMA footprint of a live BLE controller link is unmeasured; the rig's
  GM_DMA_BALLAST (now 2048 on display-loadtest) is a declared hostage guess,
  not a stand-in. Log 38 (ballast-8192 era) showed the WiFi TX cache-buffer
  pool (1630 B allocs, caps 0x80c) hitting FAILED ALLOC three times in 92 min
  with zero browser/WS clients, each ending in a watchdog WiFi reconnect, so
  the squeeze is firmware-internal, not client-load-driven. Production ships
  no ballast and has ~2x the rig's DMA headroom, but before certifying it:
  measure a real controller connection's DMA-capable cost and re-run the
  two-tab soak at that number.
