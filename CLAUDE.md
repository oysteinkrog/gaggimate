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
  check the patches applied in the build log first. One patch targets the
  LVGL libdep rather than the framework: `scripts/patch_lvgl_meter_inv.py`
  (per-env, into `.pio/libdeps/<env>/lvgl`) gives lv_meter scale-lines
  indicators sector invalidation. If dial updates ever get slow again, check
  it applied for that env.
- **One PlatformIO binary per checkout, and the build tree is volatile until
  everyone obeys that.** Use the `pio` on PATH by absolute path
  (`~/.local/bin/pio`, pipx, core 6.1.19), never
  `~/.platformio/penv/bin/pio` (6.1.18), and never a Windows-side PlatformIO,
  which shares the same `~/.platformio`. PlatformIO deletes the whole
  `.pio/build` tree whenever the project checksum changes, the checksum
  includes the core version, so two installs used on one checkout wipe each
  other's env trees on every build, for every env and every agent working
  there. **Adding or removing an env in platformio.ini changes the checksum
  too**, so never edit the env list while a runner is on the board (2026-09-07:
  one throwaway env cost a 7 minute loadtest rebuild before the board could be
  restored). The failure does not look like a build problem: on 2026-09-05 it
  hit twice under four rig workers, and each rebuilt kdev ELF has a new sha
  (build metadata), so the board had to be reflashed before kb.py worked
  again; on 2026-09-13 `display-sim` and `display-loadtest` vanished in the
  middle of a finished test run and the runner reported that it could not
  launch the simulator, which reads as a test failure and is not one (nobody
  watched that build happen, and platformio.ini was clean, so the second
  install is the remaining fit rather than an observation). So check that the
  binary you need still exists immediately before a long run, and rebuild if
  it is gone. On an intact tree `pio run -e display-sim` takes
  about five seconds. `pio run -t compiledb` follows the same rule and also
  recreates an env's build tree, so run it before the build you need, not
  after, and not for another env in between.
- **The flash runs in QIO, and the LVGL draw cost per widget is the flash
  bus, not pixels** (gm-2cl.19, 2026-09-08). `boards/LilyGo-T-RGB.json` has
  said qio at 80 MHz all along, the flash is a Winbond W25Q128 with the
  quad eFuse set, and every build shipped IDF's default of dio because the
  board setting never reached the sdkconfig. `sdkconfig.qio.defaults` sets
  QIO for the T-RGB envs (not the 8 MB XIAO headless env); the bootloader
  carries it, the image headers keep saying dio on purpose, so a board
  switches on a USB flash of `bootloader.bin` with the app and an OTA alone
  leaves it in dio. `/api/debug/flashmode` reports the live SPI0 mode. Why
  it matters: the per-object draw profile (`DrawProfile.h`,
  `/api/debug/drawprof?arm=1`, loadtest and sim builds) put 4 to 6 ms on
  every small widget of the new_profile page while its draw calls
  (DRAW_PART) were 0.1 to 0.6 ms and the host draws the same widget in 25
  us: code and rodata fetched from flash on cache misses, doubled by the
  render task's PSRAM traffic on the same bus. QIO took the widget to
  2.1 ms and every page-change draw down 30 to 45% (standby 50.5 to 40.7
  ms, brew 61.8 to 35.4, status 80.6 to 43.4, new_profile 251.9 to
  170.4). Zoom 255 on the generated images and clip_corner on the panels
  cost nothing measurable (`tuneGeneratedScreen`, `zoomfix=`,
  `clipcorner=` on `/api/debug/anim`; `tools/screen_tune_check.py`). The
  refill ISR is about 18% of core 1 (busy histogram on
  `/api/debug/scanout`), so it is not where a UI pass goes either. What
  is left per widget is still code volume against a 16 KB instruction
  cache; the 32 KB cache option costs 16 KB of the DRAM the web UI needs
  and was not tried. **Fetching instructions from PSRAM
  (`CONFIG_SPIRAM_FETCH_INSTRUCTIONS`) was tried on the bench and not
  shipped** (2026-09-08, same board and method, on top of QIO): the widget
  went to 1.3 ms and the page-change draws to standby 36.5, brew 27.7,
  steam 23.2, water 22.6, status 27.3, grind 28.0, profile 56.6,
  new_profile 121.1, info 68.8 ms, but `.flash.text` is 3.0 MB, so PSRAM
  free fell from 4.55 MB to 1.55 MB with the animation resident. The owner
  decided on 2026-09-09 to keep production off and measure on
  `display-loadtest-xip` (`sdkconfig.xip.defaults`, gm-2cl.20), the
  loadtest build with the fetch on; rodata (2.1 MB) does not fit on top. For that call: `psram_min` on
  `/api/debug/heap` is the PSRAM low-water mark since boot, and after the
  device runner, three cold web loads and a screen cycle it read 4.47 MB
  against 4.63 to 4.70 MB idle, so peak use above idle is about 230 KB.
  After QIO and the history worker the rest of the pipeline measured the
  same (2026-09-09, bench board, whole-frame path): `tools/churn_sweep.py`
  0.00 refreshes a second on every screen but standby at 0.13, and
  `tools/touch_lat.py` on the brew screen's icon press to the first frame
  carrying the plate median 56 ms, p90 68.
- **A directory on the SD card is listed with `opendir`/`readdir`, never
  with `File::openNextFile()` on the boot path** (`saferep::recoverReplace`,
  2026-09-08). `openNextFile()` opens every entry it returns, and on FAT
  each open is a linear scan of the directory, so the walk is quadratic in
  the file count. The first version of the gm-bzu.7 startup recovery walked
  `/h` that way, and on the bench card (333 MB of shot history) the setup
  task sat in it for over eight minutes on two boots in a row: everything
  registered after ShotHistoryPlugin never ran, so there was no WiFi, no
  BLE and no web server, while the panel and the animation looked healthy.
  The tell in the serial log is `Logging shot history to SD card` with no
  `STA got IP` after it. The simulator's FS shim has no mount point and
  keeps the File walk, so only the board can show this.
- **Every open of a shot-history file runs on the history worker, never on
  the web server's task, and the response is sent from the client's poll**
  (`WebUIPlugin::handleHistoryRequest`, 2026-09-09). A FAT lookup in `/h`
  walks the directory one sector at a time: about 2 s per walk on the
  bench card (3,000 shots). `FS::open` walks twice (a stat, then the
  open), 4.8 s for a shot file, so the worker opens through `fopen` on
  the mount path and reads the size with `fstat`: 2.3 s, a missing id
  the same, and the response is a filler over the handle. The static handler that served
  `/api/history/` probed for `<name>.gz` and `<name>` first, so
  `recent.bin`, which is computed and never stored, cost two full walks
  on async_tcp and the 5 s task watchdog rebooted the board on the web
  UI's Home page load. The worker task does the filesystem work and marks
  the job done; AsyncTCP polls the parked client every 500 ms on the
  async_tcp task and that poll sends the response. Do not send from the
  worker: the library's RequestContinuation example does, and under a
  burst of five requests the ack path finished the response under the
  worker, deleted the client and faulted in `write_send_buffs`. One file
  at a time, a queue of eight, 503 past that; `hist_served`,
  `hist_dropped`, `hist_queue_max` and `hist_open_us_max` on
  `/api/debug/heap`. The walk itself is a layout cost (one flat FAT
  directory with long-name entries) and is gm-bzu.23.

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

- **A page change costs one whole-page snapshot, and it is bus traffic,
  not pixels** (gm-2cl.7, 2026-09-08, `tools/snapshot_lat.py`, bench board,
  cap 45, divider 8, Starfield). The UI task gets about 20 to 25 MB/s of
  PSRAM while the render task and the panel refill run, so every stage of
  the snapshot cost what it touched: clearing both planes (921 KB) 35 to
  53 ms, scanning the whole alpha plane (460 KB) 23 to 28 ms, the six
  scrim passes plus five transposes over the 14 KB grid 16 to 20 ms, and
  the regional scrim path walking the whole grid at its stride 24 to 59.
  Five changes took every page but one from 130 to 306 ms to 48 to 100
  (`ov_whole_*` on `/api/debug/anim` is the split: snap, pub, clear, draw,
  scan, scrim, and `ov_whole_at_ms` says which page change it was):
  the clear is alpha only (colour under alpha 0 is never read: the writer
  copies over a transparent pixel without reading it, the blend gives
  bg exactly at alpha 0, the scan reads alpha alone) and, for a buffer
  that was published and not drawn into since, zeroes only what its run
  table covers (`SleepAnimation::clearBackAlphaByRuns`,
  `overlayDrawnUnpublished`; `clrruns=0` forces the memset for an A/B,
  `tools/snapshot_clear_check.py` compares the two modes on stable pixels:
  zero differing); the planar writer records each buffer row's written
  extent (`gm_snap_row_x0/x1`, LV_Helper.h, reset per pass, set to
  "everything" for a row not cleared whole) so the publish scans only the
  extent and a fill that lands outside it stores without reading the
  alpha plane; the whole scrim build is two sweeps (a horizontal 5-max,
  then per row a vertical 5-max, a horizontal 1-2-1 into a three-row
  stack ring and the vertical 1-2-1 out of it, bit-exact with the six
  passes and so with `buildScrimRegional`), 12 ms with the halo, and a
  publish whose ranges reach more than a third of the cell rows takes it
  instead of the regional path. The snapshot is rendered during the
  120 ms fade-out and published when the gain reaches zero, so what the
  eye can see of a page change is the publish (now 17 to 40 ms) plus
  whatever the snapshot ran past the fade. The page still over the line
  is new_profile: 165 ms of LVGL draw for a 300 px panel with
  `clip_corner` (every child drawn through a radius mask) and zoomed
  images (`lv_img_set_zoom`, a software transform per pixel); that is a
  screen-design cost, the generated `screens.c` owns it, and the pixel
  writer is not where it goes. Info (69 ms of draw, the QR code) is the
  other heavy one. Under the render task the draw stage runs 30 to 60%
  slower than at 5 fps, which is the shared instruction cache and the
  bus; pausing the animation for the snapshot would buy that back at the
  price of a visible hitch on every page change and was not done.
- **When LVGL owns the framebuffers, its refresh waits for the panel to
  leave the buffer it is about to write** (gm-bzu.5, 2026-09-08,
  `LV_Helper.cpp`, `refrTimerGuarded`). `presentFrameBuffer` only records
  the new index in esp_lcd; the bounce refill reads the old buffer until
  its frame wraps, and LVGL's direct mode writes that old buffer at the
  start of its next refresh (`refr_sync_areas` copies last frame's areas
  into it, then renders), before it waits for anything. So the last flush
  records the refill count at the present, the refresh timer's callback
  is wrapped, and the wrapper waits (1 ms polls, 60 ms bound) for the
  count to move before the refresh runs. The timer is paused after every
  run and resumed by the next invalidation, so the wait overlaps the gap
  between passes and is paid only when a refresh follows a present within
  one panel period. Measured with the animation off (`animoff=1`, bench
  board, divider 8, screens cycled and a synthetic brew): 235 presents,
  57 refreshes that would have written the scanned buffer, mean wait
  5.2 ms, max 12.8 ms, no timeouts. With the animation on this path is
  not taken (the animation's own `presentFrame` does the same wait).
  `lv_flip_presents`, `lv_flip_free`, `lv_flip_waits`,
  `lv_flip_wait_us_max` and `lv_flip_timeouts` on `/api/debug/anim`; a
  non-zero timeout count means the panel stopped refilling.
- **The framebuffers go back to LVGL only when the animation's stop is
  confirmed** (gm-bzu.16, 2026-09-08). `SleepAnimation::stop()` returns
  true when both workers have parked and every band transfer has retired;
  `stopConfirmed()` re-checks the same. On false, `DefaultUI` sets
  `animStopPending`: LVGL stays on its scratch buffer (touch and widgets
  keep working, unseen), neither start nor stop runs, and the loop
  finishes the handoff once confirmed or after a 3 s cap with an error
  log. A push-task creation failure in `start()` brings the render task
  down through the same `finishStop()` instead of declaring it stopped.
  Ten animoff/on cycles on the bench board all confirmed at once; the
  quarantine path has no device reproduction and is verified by reading.
- **Touch is read by its own task, and the press plate is written from
  it** (gm-2cl.4, 2026-09-08, `TouchTask.cpp`). `touchtask` polls the
  controller every 5 ms from a core 1 task at priority 2 with a 4 KB
  PSRAM stack (about 1.7 KB used) and publishes the latest sample through
  a seqlock; LVGL's `touchpad_read` takes that sample instead of reading
  the controller, so clicks, holds, repeats and unlocks stay LVGL events
  and nothing about their semantics changed, except that a press edge is
  latched for LVGL's next read: a tap that begins and ends inside one UI
  pass (every injected tap is 80 ms; a page build on the device is longer)
  is still delivered as one pressed sample at its point and then the
  release, where the old poll would have lost it, and the injected tap
  only ever worked because the queue waited for LVGL. On a press edge the task
  hit-tests the point against a hit map the UI task publishes at the end
  of every pass (`DefaultUI::publishTouchHitMap`, at most 96 rects in
  PSRAM, the same walk as `lv_indev_search_obj`, header packed into one
  64-bit atomic) and writes the plate element itself; the render task
  re-latches slot 0 between bands (`pickupInteractionElement`) so the
  plate lands in the frame being rendered, not the next one. The map's
  generation moves only when its content changes: the task clears a held
  plate when the generation moves under the finger (a screen change), and
  a version that bumped it every pass cleared every plate 25 ms after the
  press. Measured with `tools/touch_lat.py` (20 injected taps on the
  Fixture toggle row, bench board, cap 45, divider 8), press to the end
  of the first frame carrying the plate: interlaced quiet median 33 ms,
  p90 43; with the 120 px lv_anim plate moving, 35 and 46; on the
  board's stored whole-frame path 49/69 and 57/74. Before, through
  LVGL's pressed restyle, the same tap was 84/103 quiet and 153/193
  busy. What is left is the poll (up to 5 ms) plus the rest of the
  frame plus the present; the plate is not visible before the flip. The
  LVGL path itself is unchanged (press to publish 23 ms quiet, 65 busy).
  **Every user of the panel's I2C bus goes through `LilyGo_RGBPanel`'s
  recursive bus mutex** (`_busLock`, `BusGuard`): the touch controller
  read, `setVcom`, `setInversion`, `writeCommand`, `writeData`, `sleep`
  and the SD mount. Wire serialises one transaction, but a register read
  is two and its receive buffer is shared, so the touch task's GT911 poll
  and the UI task's first-pass VCOM write (bit-banged through the XL9555
  expander) read each other's bytes: the expander's read-modify-write
  took a GT911 byte as its port state and the controller NACKed every
  poll until reboot, 30 I2C errors a second, from the animation start
  onwards. The old design never saw it because the poll and the VCOM
  write were on the same task. `touchpoll=0` on `/api/debug/anim` parks
  the task (LVGL reads the controller itself again) and was the A/B that
  cleared the task itself; `touch_samples` should climb at about 145 a
  second and a rate near 15 means every transaction is timing out.
  **A release counts only after three empty samples.** The GT911 rewrites
  its status register every 10 ms and the driver clears it after every
  read, so a 5 ms poll reads "no touch" on every other sample of a steady
  press; without the debounce every hold was a stream of press and
  release edges, the press plate and LVGL's pressed restyle flickered, and
  a hold could click several times (owner's report, 2026-09-08).
- **A needle image invalidates its own rotated box, never the meter**
  (`scripts/patch_lvgl_meter_inv.py`, 2026-09-08). Upstream LVGL 8.4 sends
  a NEEDLE_IMG indicator through `inv_line`, which reads `needle_line.r_mod`
  out of the type_data union; for an image that word is part of the src
  pointer, so the "line" is thousands of pixels long and the area clamps to
  the whole 480x480 meter. Every telemetry tick that moved a side
  temperature needle was a whole-screen snapshot (126 ms) throttled to four
  a second on the grind screen and two on brew (`dirty_recent` full of
  0,0,479,479), and the UI task spent half its time in them: that was the
  stalling the owner saw in the scale readout. With `gm_inv_needle_img`
  the grind screen refreshes 1.8 times a second, all of it the blinking
  scale icon. The same patch no longer returns early for an owned dial:
  only the scale-lines invalidation is skipped, because the needle image
  on the same meter is still LVGL's and a first version froze the owned
  dials' needles until some other refresh covered them. The scale
  readout's number label is 170 px wide and right-aligned for the same
  family of reason: the owned Text element eases the digits at the
  animation's rate while the flex row around it is laid out only on an
  LVGL refresh, so a content-sized number left the "g" trailing.
- **A blinking icon is two layer sprites, not an LVGL redraw** (gm-2cl.17,
  2026-09-08, `DefaultUI::serviceIconLayers`). The dial screens' 40x40
  temperature icon blinks because the flow toggles its CHECKED state about
  twice a second and the theme recolours it on that state (`screens.c`,
  the dials widget tick); not the hidden flag, which the touchmap showed
  constant while `dirty_recent` filled with 130,415,179,464. Each toggle
  was an invalidation, a snapshot and a publish, and that pass was what
  was left of the brew and grind screens' churn (2.0 and 1.9 refreshes a
  second at rest). An lv_img on the active screen whose state or hidden
  flag has changed twice since the screen was entered gets
  `LV_OBJ_FLAG_USER_3`: the patched lv_img draws nothing for it and the
  patched `lv_obj_invalidate_area` drops its invalidations (both in
  `scripts/patch_lvgl_label_elem.py`, which now applies per hunk so a hunk
  added later still lands in a file the marker is already in). Each state
  the image shows is rendered once into a layer sprite, keyed by
  `lv_obj_get_state`, two per icon, and every UI pass shows the sprite for
  the current state and hides the other, or both while the image or an
  ancestor is hidden; a third state, a src or box change, a screen change,
  an animation stop or a delete hands the image back to LVGL. The
  handover in is the same as `moveObjectViaLayer`'s: the image is
  invalidated before the flag is set, so the sprite is gated on the
  publish that no longer holds it. `MAX_LAYERS` is 5 (one move, two icons
  of two sprites, two marquees). Bench board, synthetic brew on, 20 s
  windows: brew 2.03 refreshes a second to 0.00, steam 1.88 to 0.25 (the
  takeover pass), and the icon still alternates between the same two
  colours as on the LVGL path (`tools/icon_layer_check.py`: framebuffer
  samples through `/api/debug/fb`, bright third of the icon's box). `icons=0` on
  `/api/debug/anim` puts the icons back on LVGL and `icon_layers` lists
  what is owned, with the mirrored toggle count. Internal free at rest
  moved within noise (28.2 to 29.9 KB across boots). A sweep of every
  screen after this (`tools/churn_sweep.py`, 15 s windows) found standby, brew, status, menu, steam,
  water, profile, grind and new_profile at 0.00 refreshes a second at
  rest, and only the info screen still refreshing, at 3.75.
- **A scrolling label is a looping, clipped layer sprite** (gm-2cl.18,
  2026-09-08, `DefaultUI::serviceMarquees`). Four labels in `screens.c`
  are `LV_LABEL_LONG_SCROLL_CIRCULAR` (the info screen's obj27 and obj29,
  `profile_name` on brew, `profile_name_1`), and when the text overflows,
  LVGL's scroll animation invalidates the box on every tick: the info
  screen refreshed 3.75 times a second for its 250x21 label and the text
  stepped at that rate. A circular-scroll label whose text overflows its
  content box is rendered twice into a layer sprite (text, a gap of three
  spaces, text; the same `lv_draw_label` descriptor the label draws with,
  left aligned as lv_label forces for overflowing text) and gets both
  owner flags: USER_2 so the patched lv_label draws no text, USER_3 so
  none of its invalidations reach the display, and the LVGL animation
  runs on underneath at no cost. The layer loops from the box's left edge
  to one period left of it (`SleepAnimation::layerLoop`, a motion whose
  clock wraps) over the time LVGL would take (`anim_speed` style, else
  DPI/3 = 43 px/s, so 9,069 ms for a 390 px period), starting at the
  label's current `offset.x` so the takeover does not jump, and is drawn
  only inside the box (`layerSetClipX`). Released on text or box change,
  hidden, screen change, animation stop, delete, or `marquees=0`;
  `marquee_layers` on `/api/debug/anim` lists box, period and cycle time.
  Bench board, info screen (`tools/marquee_check.py`): 3.73 refreshes a
  second to 0.00; the box's
  column profile moves 44 to 50 px/s in both modes and 7 of 10 layer-mode
  framebuffer samples match an LVGL-mode sample to within a mean of 1.3
  to 4.9 per column against 0.4 to 2.8 for LVGL against itself (the other
  three had no LVGL sample of a near phase in the set). The text element
  scan never takes a circular-scroll label (`textLabelEligible`), so the
  two owners do not meet. `MAX_LAYERS` is 7.

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
  **Build `-e display` after touching anything near a `GM_BLEND_PROBE`
  block.** Only the bench and loadtest builds define it, and twice the
  production env stopped building without anyone noticing for a day: the
  anonymous namespace was closed inside the block, and the planar kernel
  itself was inside it (fixed 2026-09-08, d0781460 and 45367098).
  **That build works in a worktree, whatever this file said before**
  (2026-09-13): `-e display` linked here in 5 min 17 s, 5,895,863 B of a
  6,553,600 B app partition (90.0%), with the real 505,988 B web blob. The
  earlier note said the worktree's libdeps would not install on WSL1 (a
  permission error unpacking Nanopb) and sent the build to the main checkout.
  The libdeps were already unpacked by then, in the main checkout and in the
  worktree, and the nanopb generator ran from the main checkout's copy, so a
  clone with no libdeps at all may still hit the original error. Try the build
  before believing it cannot run.
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
  78 ms). **The build default is 0 (whole frame) since 2026-09-11, the
  owner's decision** (`Settings.h` `bg_ilace` and the web form's fallback;
  it was 1 from gm-2cl.9 on 2026-09-08 until then). Every row refreshes
  every frame, at about half the interlaced rate. A stored value still
  wins on a device that has one, so a measurement states which path it
  ran on. What the whole-frame path costs on that board (2026-09-11, kernels
  in flash, cap 40, `tools/framefn_sweep.py` through the web preview;
  `/api/debug/pclk` read divider 7 after these runs, where every runner
  log up to 2026-09-10 22:54Z had read 8, and nothing in the rig writes
  it, so the day's rates are at 7 unless stated): every animation but one
  ran 20 to 31 fps whole-frame, because
  a frame's work (18 to 36 ms) is more than one panel period and the flip
  waits for the next; the same animations interlaced run 34 to 37 fps. A
  device on the whole-frame path (the default now) runs every animation at
  about half its interlaced rate, and which path a device is on is the
  first thing to check when the animation "got slow".
- **An animation's `frame()` is timed on its own, and `band_us` does not
  see it** (`framefn_us` on `/api/debug/anim`, 2026-09-11). The stage
  counters cover the band kernel, the blend and the push; `frame()` runs
  once per frame before the first band and neither the counters nor the
  kblob bench (which never calls it) show it. A sweep of all 44 animations
  put every `frame()` at 0 to 2.3 ms except Harmonograph at 23.8: it
  rasterises its 2,048-sample curve there, an 11x11 max stamp per sample
  into a 230 KB coverage buffer in PSRAM, so the loop ran at 14.5 fps with
  the stored animation and 27 ms of every frame was unaccounted for.
  Stamping in coverage-row order (a counting sort by stamp row, so each
  PSRAM line is fetched once a frame instead of once per lap of the
  curve), clearing only the union of the previous and current curve box,
  and skipping the zero corners of each stamp took it to about 17 ms and
  the loop to 18 to 19 fps in the same post-boot window. The stamp pass
  then went to the PIE (`harmonographStampAsm`: the padded stamp row is
  loaded with `EE.LD.128.USAR.IP`, which latches the funnel shift from
  the address's low four bits, scaled with `EE.VMUL.U8` at SAR 8, placed
  in a 32-byte window by two `EE.SRC.Q`, and max-composited with
  `EE.VMAX.S8` under an 0x80 bias since the PIE has no unsigned max;
  the coverage buffer carries 16 bytes of slack on both sides for the
  window), which took frame() to 11 to 13 ms and the loop to 18.9 fps
  interlaced against 15.7 in the same state (whole-frame 10.7 to 12.1,
  quantised at 68 ms). `harmostamp=0|1|2` on the loadtest build's
  `/api/debug/anim` selects portable, PIE, or both with a byte compare
  (`harmostamp_checked`, `harmostamp_mismatch`; 71 frames, 0), and
  `tools/qemubench/tests/anim_harmonograph_stamp` is the bit-exact proof
  over every window offset, level and a set of stamp shapes, guards
  included. A pinned-IRAM build (`GM_BGANIM_IRAM_KERNELS=1`, all 21
  kernels) measured the same 14.5 fps as the flash build on the same
  serial channel, so IRAM was not the lever there either. A new
  animation whose `frame()` does per-pixel work belongs on this list, and
  the sweep is the way to find it.
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
  plates was ~106k non-transparent pixels and 12 to 13 ms of blend a frame
  at any render priority, so how much of the screen is translucent over
  the animation is the budget knob for everything above.
- **A Custom plate is a radius-140 disc behind the dial, not the dial
  panel itself** (gm-2cl.10, owner's choice 2026-09-09,
  `DefaultUI::applyAnimPlates`, `ANIM_PLATE_DISC_RADIUS`). The four dial
  panels (brew, steam, water and status; 360 and 400 px discs in
  `screens.c`) stay transparent in mode 2 and a 280 px child disc, the
  panel's bottom child, carries the colour and opacity; modes 0 and 1 hide
  it. The generated screens are untouched, so the children keep their
  positions and nothing is clipped. Bench board, brew screen, interlace
  pinned, cap 45, divider 8, black plate at the stored opacity, two runs:
  blend 9.6 ms over 103k overlay pixels before, 6.9 and 7.1 ms over 65k
  after, frame rate 27.5 to 30.2 fps. The bead asked for 3 ms; the 2.6
  measured is what 38k fewer pixels buy at the 0.07 us a pixel the
  planar kernel now costs, not the 0.12 the estimate used. The profile
  row and the centre controls sit inside the disc (framebuffer capture
  through `/api/debug/fb`); the status bar readouts above the dial were
  outside the old plate too. Measured with the mode switched through the
  Animation category and switched back, so the stored mode ends as found.

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
  the brew screen's plates were about 106k non-transparent pixels and 12 to
  13 ms of blend a frame before the radius-140 disc, 65k and 7 ms after
  (UI-pipeline invariants above). The bench board
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
- **A field is live if the display reads its stored value while the category
  is open, and every touched live field is re-asserted at reconcile**
  (gm-nov3.23, gm-nov3.36, gm-nov3.39; `reassertTouchedLiveFields` in
  `CatAnimation.cpp`, `mergeTouchedSlots` in `CatAnimParams.cpp`). The rule
  above describes the draft, and for a field nothing reads during the visit
  the draft is the whole story until commit. A live field is different: the
  row writes `Settings` the moment it changes and `DefaultUI` resolves the
  panel from the stored value on its next pass, so a web save landing
  mid-visit drew the web's value behind a row still naming this visit's, with
  nothing on screen to say so, until commit moved it back at the exit. Apply
  the test rather than keeping a list: read the row's write, then find whether
  `DefaultUI` reads that field on a path the open cover reaches. The list went
  stale twice on 2026-09-13, once for the global gradient and once for
  everything that was not a gradient. Today it takes in every value row of the
  Animation category except the standby animation id, whose read at
  `DefaultUI.cpp:4561` is gated on the standby screen while the cover sits on
  the menu screen, plus every slot of `bgAnimParams`. The field by field
  inventory, with the write and the read line for each, is the comment above
  `reassertTouchedLiveFields`; keep it true when a row is added. One function
  writes the set and both reconcile and commit call it, so the two cannot
  drift about which fields a visit owns; commit still writes exactly what the
  rows said, and an untouched field still adopts whatever the web posted. One
  window survives: `SettingsUI::service()` reconciles before
  `DefaultUI::updateState()` in the same pass, so a POST landing between those
  two calls shows the web's value for that pass, a few hundred milliseconds.
  The checks are `check_touched_scalars_agree_with_panel` and
  `check_touched_param_slot_agrees_with_panel` in
  `tools/settings_ui_tests/test_gradientdraft.py`, and both assert on the
  stored value before the page is popped, because after the pop commit has
  written the draft and a version that reconciled nothing would pass.
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
  framebuffer read mid-hold. That note used to add that the device only
  delivers `/api/debug/fb` at step 2. It delivers every step since gm-6ivh
  (28cec8ec, 2026-09-13): the filler wrote whole output rows and returned 0
  when the send budget could not hold one, which the web server reads as the
  end of the body, so a step 1 request always returned exactly 5 rows, 4,800
  of 460,800 bytes, with a 200 and no error. It was never heap dependent and
  never request dependent. Step 2 and above have rows of 480 bytes and under,
  which always fit, which is why only step 1 ever looked broken.
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
  audit, 91cb0ed5). The page header is 240 px wide at y -160: previous and
  next arrows at x -100 and +100 (hit boxes 82 to 138 px from the centre,
  far corner 227.4 px out, under the 228 px edge rule) with the title on
  one line in the 144 px between them, its height fixed to the font's line
  height because LONG_DOT wraps instead of truncating when the height is
  left to the content (the old 96 px column showed "Animatio" / "n").
  `Rig.audit()` in `tools/settings_ui_tests/rig.py` is the check, and the
  runner audits every page on every run. The arrows point left and right
  (owner's request, 2026-09-09; they were up and down chevrons on the same
  spots) and a horizontal swipe on a category page turns it too: the page
  root clears `LV_OBJ_FLAG_GESTURE_BUBBLE`, since LVGL hands a gesture to
  the first ancestor that does not bubble, and the handler calls
  `lv_indev_wait_release` before `gotoPage`, because LVGL 8.4 gates
  CLICKED on scrolling and not on a gesture, so a swipe across a toggle
  row would otherwise flip it on release. `/api/debug/tap` takes `x2=`
  and `y2=` for a scripted drag and `Rig.swipe()` wraps it.
- **`bgAnimTheme`'s legacy namespace is frozen at 18 and is never the table
  length** (gm-nov3.7, 2026-09-12). Before the gradient library existed
  `bgAnimTheme` was the whole setting: 0 to 17 were the built-ins of an
  18-entry table and 18 meant the single custom gradient in
  `bgAnimCustomTheme`. `bg_resolve_theme` used to read that sentinel as
  `bg_theme_count()`, so growing the table to 60 would have made a device
  that stored 18 draw whatever new built-in landed at index 18.
  `BG_THEME_LEGACY_CUSTOM` (BgAnim.h) pins it at 18 for good, mirrored by
  `BG_THEME_CUSTOM` in `web/src/config/bgAnimations.js`, and a legacy integer
  outside 0 to 18 reads as built-in 0. An explicit ref is a different
  namespace over the same digits: `bg_resolve_anim_theme` fills a built-in
  ref straight from the table and never through the legacy branch, so the ref
  "18" is the built-in at index 18 while a legacy 18 still means the custom
  gradient. Four writers apply the rollback mirror and all four go through
  `bg_legacy_mirror_for_ref` or its JS twin `legacyThemeMirror`: the web
  form's `globalAssignFields`, the firmware POST handler, and the display's
  live row and its commit. Built-ins 0 to 17 mirror unchanged, 18 and above
  mirror as 0 (never as 17, and never as the index itself), a library ref and
  an unresolvable ref leave the field alone. The carry-over of the custom
  string into the library is `bg_plan_gradient_migration` (pure) plus
  `bg_run_gradient_migration` (an abstract store), because
  `Settings::doSave` writes `bg_th` and `bg_ct` before `bg_gl` and `bg_gref`
  and carries on past a failed key: one combined save could clear the source
  before its replacement existed. A clear is never verified, because
  Preferences reports a failed empty-string write as success (Property.h), so
  the next boot re-plans from what actually landed. The whole policy is
  checked by `pio test -e native_settingsui` (groups I, J and K, against a
  60-entry test table) and `node tools/gradient_mirror_check.mjs`.
- **A gradient resolves in three steps, and all four writers mirror a built-in
  into `bgAnimTheme`** (gm-tany, 2026-09-12). `bg_resolve_anim_theme`
  (BgAnimThemes.cpp) reads this animation's own `bgAnimThemeMap` slot, then
  the global `bgAnimGradientRef`, then the older `bgAnimTheme` plus
  `bgAnimCustomTheme`. Step three is what a build without the ref key reads,
  so an upgrade changes nothing on screen; the mirror is what keeps a
  rollback showing the same picture, under the frozen rules above. The ref grammar is
  shared by the map slots and the global: a decimal built-in index, "c<id>"
  for a library entry, or "" for neither, parsed by `bg_ref_valid` and
  rejected at the POST handler rather than stored. A ref that no longer
  resolves (a deleted library entry) reads as "" on both surfaces, which is
  what the firmware draws. Both surfaces call the same thing Global: the
  display's per-animation row shows "Global (<name>)" at index 0 and the web
  picker's first choice says the same. BgAnimThemes.cpp compiles into the
  simulator as well (gm-nov3.3), so a function added there needs no stub;
  what breaks the display-sim link is a device-only dependency added to that
  file. `sim/platform/bganim_stub.cpp` holds nothing but the animation
  allocation counters now.
- **A built-in gradient is added in `data/gradients.json` and nowhere else**
  (gm-nov3.1, 2026-09-12). `scripts/gen_gradients.py` writes
  `src/display/ui/default/bganim/BgAnimThemeTable.h` (what BgAnimThemes.cpp's
  `THEMES` now points at) and `web/src/config/bgThemes.js` (what
  bgAnimations.js re-exports as `BG_THEMES`), both checked in so no build
  needs Python, and `--check` fails on a stale copy from `tools/animbench`
  `make check`. Before it the same 18 gradients were written out three times
  and synced by hand, the third copy being `kSimThemeNames[]` in
  CatAnimation.cpp, which existed because the simulator did not link
  BgAnimThemes.cpp; since gm-nov3.3 it does, so both builds read the table
  through `bg_theme_*` and BgAnimThemes.cpp is the only firmware file that
  includes the generated header. Nothing else may include it: a second copy
  of every stop lands in the image. The host model test
  (`test/test_settings_model`) includes it because it links that file, and it
  reads the six accessors rather than a second provider of its own
  (gm-nov3.19). The
  list is append only and the generator does not enforce that: an entry's
  position is its stored id, in `bgAnimGradientRef` and in every slot of
  `bgAnimThemeMap`, so reordering or removing one changes what a device
  already set. Six stops each, because the palette arithmetic assumes even
  spacing and `bg_resolve_theme` copies 6 x 3 bytes.
- **A gradient row carries three targets: a band that opens the picker and
  two arrows that step, and the picker keeps the slot it was opened for**
  (gm-nov3.3 and gm-nov3.32, 2026-09-13, `CatGradientPicker.cpp`, the two
  swatch rows in `SettingsRows.cpp`). gm-nov3.3 made the three gradient rows
  whole-row targets that push a picker and took the prev/next arrows off,
  because a row-wide target laid over them is two targets in one place and
  `Rig.audit()` fails on the overlap. The owner asked for the stepping back,
  and the overlap came from the target being row wide rather than from the
  arrows existing: since gm-nov3.32 the picker is opened by a centre band
  200 px wide (`kTextColW`, the width a choice row's text column has) and the
  row container is not clickable at all, so the arrows land on the pixels a
  choice row's arrows already occupy. Measured on the simulator, all four
  Animation pages: no audit violations, smallest target 56x56, which is the
  two arrows sitting exactly on the minimum with nothing to spare; the band
  is 200x56, the closest pair inside a row is prev to next at 4 px, and band
  to prev is 12 px. There is
  no room for a wider band: the bottom row's far corner is 227.2 px from the
  panel centre against the 228 px edge rule. The arrows walk the picker's own
  flat order (Global where the row offers it, then the saved gradients, then
  each category's built-ins, wrapping both ways), so stepping and then opening
  the picker finds the marker where it should be, and a step writes the same
  field a pick writes, through `animGradientAssign`. Holding an arrow is safe
  because a step is one `Property::set`, which only raises a dirty flag
  (`Property.h`), and NVS is written by the periodic flush
  (`Settings::loopTask`): a five second hold stepped 45 times and left three
  distinct readings of the stored field. The stepping row's ramp is a bar
  under the name rather than a block beside it, and it keeps the picker row's
  96 columns on purpose, because two checks compare a category row's swatch
  pixels against a picker row's. Six rules the pages cannot show. A pick is
  written before `popPages`, because the pop rebuilds the page underneath
  from the draft and a write after it would leave the old value on screen.
  Each level calls the opening category's reconcile itself, since the shell
  reconciles only the top page. The picker captures the animation slot it was
  opened for and never retargets it under a web save; if that slot stops
  being editable (the standby animation turned off) it closes without
  selecting, and both reconciles return immediately after the pop that frees
  their ctx. `clampAnimId` at `enter` and at `reconcile` is load bearing
  again now the arrows are back: a stored `bgAnimId` from a longer registry
  wrote past the end of a heap vector on the first Gradient arrow press once
  (91cb0ed5). An inert row's arrows are inert with it, which
  `settingsRowSetEnabled` gives for free (`LV_STATE_DISABLED`, which
  `lv_obj_hit_test` refuses). And a swatch must not publish the gradient it
  draws: the real palette code works on the globals the render task draws
  with, so `GradientSwatch.cpp` is a separate transcription, held to the real
  one by `tools/animbench/swatch_parity.cpp` (all 256 ramp entries, every
  built-in, library strings of 2 to 16 stops with repeated positions and flat
  endpoint runs, seven tone settings). The rest is in
  `src/display/ui/default/settings/README.md`.
- **The simulator runs the real gradient rules, so a gradient test can
  fail** (gm-nov3.3). `sim/platform/bganim_stub.cpp` used to stub
  `bg_map_valid`, `bg_library_valid`, `bg_ref_valid` and the rest as always
  false, and `WebUIPlugin.cpp` gates every POST to `bgAnimThemeMap`,
  `bgAnimGradients` and `bgAnimGradientRef` behind them, so on the simulator
  no web save of any of those fields could land: two runner checks were
  asserting that the POST bounced and reported a pass for a step that could
  not fail. BgAnimThemes.cpp turned out to have no device dependency, so it
  compiles into the simulator and the stubs are gone. Keep it that way: a
  device-only call added to that file takes the simulator's gradient tests
  back to a state where they cannot fail. Every web-interference step in
  `tools/settings_ui_tests/test_animation.py` reads the field back before
  judging the UI, for the same reason.
- **The standby animation's parameters and gradient are stored per animation
  id**, so they already existed before there was any way to edit them. The
  web tab reaches them through the main and standby selector above the
  tuning block, the display through the "Standby params" and "Standby grad"
  rows, and both are inert when `bgAnimStandbyId` is -1 (follow the main
  animation).
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
  `dirty_recent`, `icon_layers`, `marquee_layers`, the `ov_whole_*`
  page-change split, the `lv_flip_*` counters, `touch_task`,
  `touch_samples`, `touch_hwm`, `hitmap_n`, `hitmap_gen`, the `texts=`,
  `textease=`, `dials=`, `icons=`, `marquees=`, `clrruns=`, `touchpoll=`
  and `animoff=` knobs) and
  `/api/debug/pclk` (the live pixel-clock divider) are device-only: both sit
  inside the real-panel block of `WebUIPluginDebug.cpp`, which `GAGGIMATE_SIM`
  and `GAGGIMATE_HEADLESS` exclude. Every `/api/debug/*` route, the probe
  routes and the bench routes are registered by
  `WebUIPlugin::setupDebugEndpoints()` in that file (gm-bzu.19); the
  production routes stay in `WebUIPlugin.cpp`'s `setupServer()`.

Three test commands, and what each proves:

- `pio test -e native_settingsui` runs the value model on the host, 59 cases,
  no LVGL and no Arduino. It proves ranges, steps, wrap, formats, the zone
  split, the gradient map, the picker's grouping and schedule parsing.
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

- **The bench board stores pixel-clock divider 7**, not the build default 6,
  so its rates are not comparable with a run at the default. The runner reads
  `/api/debug/pclk` and warns. This is the stored-settings trap from the
  hardware invariants above, hit again. **It stored 8 until some time between
  2026-09-10 22:54Z and 2026-09-11**, and every measurement in this file dated
  on or before 2026-09-10 that says "divider 8" was taken at 8 and is correct
  as written. Nothing in the rig writes the setting and nobody has owned up to
  changing it, so read the value rather than assuming either number. Confirmed
  stored on 2026-09-13 by reading `panelClockDiv` from the settings twice an
  hour apart, with `/api/debug/pclk` reporting `div 7, live true` both times
  and the serial `GM_SCANOUT` frame rate agreeing independently at 43.5 a
  second, which is the divider 7 row in `rig_soak.py`'s own table.
  `panelclock::MIN_USER_DIV` floors a user setting at 6, so 7 passes.
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

- A malformed stored schedule time shows as 00:00 in the editor and as the
  raw string in the list (`CatSchedules.cpp`, through
  `settingsui::scheduleTimeParts`). The web handler no longer writes one
  (2026-09-09, `isScheduleTime` in `WebUIPlugin.cpp` drops an entry whose
  time is not HH:MM in range), so only a value stored before that or
  written by hand can still show this way.
- `buildRegions` rebuilds the whole region span list, one `std::string` per
  zone entry, on every region or city arrow press (`SettingsModel.cpp`): three
  builds for a region press, two for a city press. Measured on the host
  (2026-09-09, x86 at -Os, 461 entries): 11 us per build, 34 us per region
  press, 14 us per `locate`. The device has not been timed; at the 20 to 50x
  the S3 runs string-heavy flash code slower than the host this is about
  1 to 2 ms per press, under one UI pass, so it is left as it is.

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
- **Animation kernels run from flash; only the render loop is pinned in
  IRAM** (2026-09-10). IRAM text past the first 16 KB is taken from
  internal DRAM byte for byte. The 21 ported animations each carried a
  private `#define GM_ANIM_IRAM IRAM_ATTR` and together pinned 21.5 KB of
  kernels (linker map, `.iram0.text` by object): the boot heap pool shrank
  from 196 KiB to 170 KiB, the loadtest build idled at 9 KB internal
  instead of 24 to 35, DMA-capable free sat at 1.4 KB, and WiFi lost every
  outgoing frame within a minute of connecting (`FAILED ALLOC size=1630`,
  the NetWatchdog reconnect loop, no web UI). `GM_ANIM_IRAM` is defined
  once in `BgAnimCommon.h` and is empty unless the build sets
  `GM_BGANIM_IRAM_KERNELS=1`, which is a bench A/B knob and never ships.
  The tripwire is the `heap_init: At 3FC... (N KiB): RAM` line at boot:
  under 190 KiB means something new is static in DRAM or IRAM.
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
- **The web UI reaches the firmware only through `scripts/build_webui.sh`**
  (2026-09-10). `npm run build` alone writes `web/dist` and nothing reads
  it: the script gzips the bundle and runs `embed_webui.py`, which writes
  the git-ignored `src/display/webassets/` (blob, manifest, `.S`). A fresh
  worktree has a 1-byte `web_ui.bin` stub there, `embed_webui_pre.py` keeps
  it so a bare build links, and the firmware then answers 404 "Not found"
  on `/` and every page while `/api/*` works. Three flashes from this
  worktree shipped that stub before anyone opened the web UI. The tell is
  `.pio/build/<env>/webassets/web_ui_blob.o` at 864 bytes (the real one is
  about 500 KB), or `web_ui.bin` at 1 byte.
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

The background animations' band() hot paths (13 at the time; Silk 2 made it
14 on 2026-09-05) went through a hand-written
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
  pointer. Both bganim allocators return 16-byte-aligned memory for this
  reason: `allocHot` always did, and `alloc` does since 2026-09-12, when
  Truchet's `blendLast` came back 4-byte aligned from `ps_malloc` and every
  vector store landed up to twelve bytes before the block. That is the heap's
  head canary, so the next free took the board down inside
  `/api/debug/animtest` (`multi_heap_free ... head != NULL`, the render task's
  backtrace ending in the animation's own `release`). **The host cannot show
  this class of bug**: glibc malloc is 16-byte aligned, so the goldens, the
  fuzzer, the lifecycle check and QEMU all agreed with a device that was
  writing outside its allocation. Only `/api/debug/animtest` on the board
  found it, and only because the free that asserts came after the test.
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
  included) and writes the one project-wide compile_commands.json; the
  build-tree rules for it, and for editing the env list, are in the
  PlatformIO bullet under the hardware invariants above.
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
- **The kblob rig runs the kernel from IRAM and the board runs it from
  flash, so the rig under-reports, and by an amount that is a property of the
  kernel** (gm-4bd.6 and .7, 2026-09-13, bench board, divider 7). Same code,
  same session: plasma 6.81 ms in the blob against 8.14 on the board, 1.22
  times; crescent 8.53 against 14.8, 1.73 times. What the board pays on top is
  fetching the kernel's own code through the 16 KB instruction cache the two
  cores share, so a tight kernel reads close to the board and one with library
  calls or a lot of per-row setup reads far better in the blob than it will
  run. The proof that the gap is the kernel's and not noise: crescent's second
  commit (98afb42b) left the blob time at 8.53 and took the board time to
  12.5, changing nothing in the pixel loops, because two `sqrtf` calls a row, a
  flash resident library function, came out of `band()` and became a table
  built once a frame. There is no fixed factor to correct by. A blob win that
  does not appear on the board is code volume, not pixels, and the gap is the
  number to watch.
- **A band time ratio means nothing except against a plasma measured in the
  same run, in a stated board state** (2026-09-13). Plasma is the animation
  least affected by the board's state, because its kernel is the smallest and
  has the least to lose from a cold instruction cache, so the state moves the
  ratio rather than moving both numbers together. Two `/api/debug/animtest`
  fleet runs of the same binaries, before any of the speed work, on the same
  board:

                  plasma    sundial          crescent          glint
      run A       194584    473440 (2.43x)   661116 (3.40x)    239499 (1.23x)
      run B       241123    458098 (1.90x)   659998 (2.74x)    242821 (1.01x)
      moved         24%       3.2%             0.2%              1.4%

  Plasma moved 24% and nothing else moved more than 3.2%, so every ratio moved
  by about the same 22 to 24%. Read run B alone and glint is inside the 1.25
  budget at 1.01 with no work done at all, where its bead had recorded 1.37 to
  1.54. That is the mistake to recognise, and it is not a mistake about the
  animation under test: it is a plasma reading taken in a state that slowed
  plasma down. **The method that held:** hard reset through esptool,
  `/api/debug/synth?brew=0` to stop the loadtest build's synthetic brew, settle
  40 s, then `/api/debug/animtest` with plasma in the same run, three times.
  Three runs of that agreed within 3% for four animations, better than anything
  else tried. The states that produced the higher readings differed in at least
  four ways at once (uptime of hours against minutes, the synthetic brew
  cycling the two heaviest screens, `framefn_sweep.py` through the web preview
  instead of animtest, and the interlaced path pinned on), and which of the four
  moves it is an experiment nobody has run. Two loaded-state figures quoted in
  the gm-4bd.6 and gm-4bd.9 notes, sundial 1.52 and glint 1.21, are unsourced:
  the lane could not find the raw output afterwards and withdrew them. Use run A
  and run B, which are recorded in full.
- **`EE.LDXQ.32` is the PIE unit's only gather, and this QEMU fork gets it
  wrong** (gm-4bd.6 and .7, 2026-09-13). The instruction takes a 16-bit lane of
  a vector, scales it by four, adds a base and loads 32 bits into one lane, so
  eight of them plus an `EE.VUNZIP.16` fetch eight table entries where a scalar
  pair loop costs eleven instructions for two. Two consequences in source. The
  index scales by four, so a gathered table is 32-bit and its entry count is
  not its byte count: establish the largest index the caller can produce and
  check it against the allocation, the way glint pads a 512 entry palette for a
  sum that reaches 384. And paying for 32-bit tables in the 9,216 byte hot slab
  means moving something out, which is safe only for a table read once a row
  rather than per pixel (sundial's `halfPx` and `surfRow`). **The emulator
  returns the entry one 32-bit word below the correct one, every time, for
  every lane** (espressif/qemu issue 162);
  `tools/qemubench/tests/probe_ldxq32` is the eleven line reproduction. The
  sundial and crescent tests hand the vector path a table pointer one word
  high, marked `QEMU_LDXQ_BIAS`, so the error cancels: every lane of arithmetic
  is checked (sundial 2,260,707 lanes, crescent 6,024,680, zero mismatches) and
  only the gather address is left to silicon, where `/api/debug/animtest` and
  the kblob hash check it. So a QEMU pass on one of those tests says the
  arithmetic is right, not that the address is. A test that uses the
  instruction and passes with no bias is either not reaching it or comparing
  two wrong things. One trap that cost a rebuild: a kernel walks single pixels
  until its output pointer is 16-byte aligned and only then enters the vector
  body, so one call uses both paths, and the harness has to split the call the
  same way the kernel does and run it twice, biased for the vector span and
  true for the rest.
- **The fuzzer is only a fuzzer with the sanitizers on**
  (`tools/animbench/Makefile.fuzz`, run with
  `ASAN_OPTIONS=verify_asan_link_order=0` on WSL1). Without ASan a
  one-entry table overrun reads the neighbouring byte and passes; that is
  how silk shipped a palette pad of 4 against a dither amplitude that
  ditherAmp() caps at 16, reading past its LUT at the default parameters.
  Any animation with an unclamped, padded gather sizes its pad from that
  cap, and a change to a table's layout re-runs the fuzz for the fleet.
- **An init() that fails halfway releases everything it allocated before
  returning false** (gm-bzu.15, 2026-09-08). Five animations allocated
  every table inside one block keyed on the first pointer; when one
  allocation failed they returned false with the rest in place, and the
  retry that BgAnim.h promises either skipped the block and ran with null
  tables (starfield and ember wrote through null in frame()) or allocated
  the hot tables again and leaked the first set in the slab, which never
  resets while a table is live. `tools/animbench/lifecycle_check.cpp`
  (`make -f Makefile.lifecycle check`, ASan and UBSan, CI runs it after
  the goldens) is what finds this: every animation through 480, 240, 466
  and 233 and back with a release between, the heap made to run out at
  each allocation in turn, and a non-zero exit on an init failure, a
  slab or heap leak, or a sanitizer report. The goldens and the
  call-shape check init once and never release, so none of it showed
  there. On WSL1 run it with
  `ASAN_OPTIONS=verify_asan_link_order=0:detect_leaks=0`; the harness
  counts heap blocks itself.
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
- **Speed 50 is the default on every animation and means the same amount of
  movement everywhere** (gm-33fm, 2026-09-12). Movement is the half change
  time: how long the picture takes to change half as much as two unrelated
  moments of the same animation differ (`tools/animbench/web/motion.js`,
  guide in `MOTION.md`). Target 1200 ms at Speed 50, accept band 950 to 1500,
  and above that the slider follows one universal curve,
  `rate = 2^((sp - 50) / 18.2)`, so 0 gives 8050 ms and 100 gives 179. Before
  the calibration the fleet spread 144x at Speed 50 and ten of the newest
  ports shipped a Speed default of 10 to 20, which is what "the animations
  barely move" was: Glint at its default 10 ran at 0.22 of every number
  anyone had measured. A new animation is measured before it lands, and its
  default is 50. Two metrics, and which one applies is a property of the
  animation: read `thalfLpMs` (8x8 box average before differencing) for
  Starfield, Orbits, Harmonograph, Nebula and Floor, `thalfMs` for the rest.
  The half change time does not scale as one over the rate, so the multiplier
  is a starting point and the measured band is the contract. It is also blind
  to whole pixel stutter, which is what `grain_ratio.js` is for: read its
  delta across a change, never its level.
  **Two questions, two runs, and mixing them up cost a day** (gm-kh2s,
  2026-09-12). How much an animation moves at Speed 50 next to the others is
  the run above: a window fixed in wall clock, which is right, because it is a
  person watching for a fixed time. Whether the Speed slider scales that
  animation is `motion.js --matched`, which divides every time in the playback
  by the setting's rate so each Speed sees the same window of animation time,
  and reports `thalfAdjMs` with the rate multiplied back. **Do not use the
  first run to answer the second question.** Its window sees 6.7 times more
  animation time at Speed 100 than at Speed 50, so the level it calls "fully
  changed" moves with the setting; judging the slider that way failed seven
  animations whose clocks were correct and passed three that were not.
  **Where the fleet stands: 44 of 44 inside the band at Speed 50** (median
  1180 ms, spread 1.5x, down from 144x) and **44 of 44 flat across the
  slider** under `--matched`, worst 1.017x. Five animations scaled one term
  and not the rest, and all five are fixed: Steam's rise without the puff
  lifetime or the sway, Ripples' ring travel without the drop interval or the
  ring life (so Speed 100 showed less movement than Speed 75), Starfield's
  drift without the twinkle or the shooting stars, Lava's orbits without the
  radius pulse, and Silk's travel and rotation without the width wobble. The
  last three had passed every check the fleet had.
- **The goldens cannot catch a port drifting from its page design, and for
  twenty animations it had** (gm-pciz, 2026-09-12). The goldens under
  `tools/animbench/golden` are rendered by the firmware, so `make check` only
  proves the firmware draws what it drew yesterday. `make pagecheck`
  (`web/page_vs_golden.js`, third stage of `make check`) plays the page entry
  the way `bench.cpp` plays the firmware, captures golden frames 30, 120 and
  210, quantises both sides to RGB565 and counts differing pixels. Forty-two
  of the 44 are exact, which is the rule a new port is held to; the other two
  are debt recorded in `page_exact.json`: ripples 18369 and cube 33, both
  accepted by the owner on 2026-09-13 with the reason in each animation's own
  header and in that file's `accepted` block. Steam (e536caf1) and orbits
  (dac4c7e1) were debt until that day and are exact now.
  It was 24 exact and 20 in debt when the check was
  written, and every one of the eighteen closed since had a different cause,
  three of them user-visible defects that had shipped (Mandala drew
  concentric rings where the page draws petals, Ember was materially darker
  than the design, and Nebula's drift speed depended on the frame rate).
  Lower an allowance when you fix one; never raise one to pass a check. The
  page entry is the approved design and the firmware is the side to change.
  `bench.cpp` renders a warm-up frame at t = 0 before its loop, so an
  animation that accumulates state per frame starts one frame ahead on the
  firmware side, which is the recorded cause of Nebula's gap.

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

- Device: 192.168.1.121 on the bench, UART on COM3. **Opening COM3 with
  pyserial's defaults resets the board** (reset reason `usb`, confirmed
  2026-09-11 by opening the port with the board at 353 s of uptime and
  reading 30 s after): the DTR and RTS pulse on open drives the
  USB-serial-JTAG reset, so every serial capture taken before this note
  was a fresh boot, and the reset seen at the end of the 2026-09-11 sweep
  was this (the two from 2026-09-10 had no serial capture open and stay
  unexplained). Open with
  `s = serial.Serial(); s.port = "COM3"; s.dtr = False; s.rts = False;
  s.open()`, which leaves the board running (checked the same way). The
  flash step resets on purpose.
- **The board's radio link degrades in place, minutes into a boot, and a
  reset cleared it** (gm-t9ld, reproduced and placed 2026-09-13). A reset is
  the only recovery tried, so it is the one that is known to work, not the
  only one that can. The symptom
  is that every HTTP response stalls or truncates: small JSON GETs taking 4 to
  27 seconds or failing, whole framebuffer reads taking nine minutes. It looks
  exactly like a firmware fault and it is not one. What places it is a ping
  with a control, back to back from the same host: 55% loss to the board, 0%
  loss to the gateway, 53% loss to the board again. The board's own counters
  stay healthy throughout (`egress ok`, zero LogicLoop overruns, 21.7 fps,
  `int_free`, `int_largest` and `dma_free` unchanged), the serial log carries
  no WiFi driver event at all, no roam, no disconnect, no reassociation, and
  the association never drops. A reset with no flash cures it completely: same
  BSSID, same rssi, 0% loss and 0.18 s GETs for eight rounds afterwards. So it
  is the board's own radio state, not the air, and it is not the esptool reset
  that starts every session (the same command an hour earlier gave 15 clean
  minutes). **Two earlier explanations are dead**: gm-nov3.10's internal DRAM
  starvation (the heap numbers in the broken window match a healthy one), and
  the framebuffer endpoint itself (ICMP never reaches a handler). Why it
  degrades is not established and is gm-bzu.26.
  **How to tell a sick board from a slow one before you measure anything**:
  time three small GETs of `/api/debug/anim` and stop if any one exceeds a
  second. In the bad window 11 of 15 single GETs were over a second or
  failed, so one sample misses a sick board about a quarter of the time and
  three are better than one. How much better is not measured: treating the
  three as independent gives 98 in 100, and nothing establishes independence,
  which a board failing in bursts would break. Three GETs cost under a second
  on a healthy board, so run them. Ten pings is the other cheap test and it
  was unambiguous in both states.
- Windows tooling runs Python 3.10 (`GM_RIG_PY` env var to override):
  Python313 silently lacks esptool and pyserial.
- Camera verification: `C:\work\camshots\grab.bat <file>` (one frame),
  `burst.bat` (8s at 6fps). Photos land in C:\work\camshots.
- `/api/settings` returns the WiFi password in cleartext: never dump it, and
  never POST to it by hand (the web UI is the only safe writer).

## Open cleanups

- The PHY PLL-track deferral is gone (gm-bzu.24, removed 2026-09-09,
  kept on branch `keep/phy-track-defer` at 48dcf8a6). It was a framework
  patch (`patch_phy_track_defer.py`) plus a task and a VSYNC release in
  PanelClock.cpp that ran the RF PHY's once-a-second PLL-track tick in
  vertical blanking, written when the panel's interrupts shared core 0
  with the radio and the tick's flash fetches displaced one band a second.
  The revert test (bench board, loadtest build, WiFi and the BLE controller
  link up, standby screen with the animation on) toggled it at run time
  within one boot, four phases each: at the stored divider 8, 5 minute
  phases, 0 resyncs and 0 slips in every phase, catchups 0.03 to 0.05 a
  second either way; at divider 6 (set live, the production clock), 4
  minute phases, again 0 and 0, catchups 0.2 a second either way with
  depth max 2 of 8. With the panel's interrupts on core 1 the tick fits.
  The build without it soaked 10 minutes at divider 6 (`tools/rig_soak.py`,
  585 s settled): 0 resyncs, 0 slips, catchups 0.6 a second at depth max
  2 of 8, busy_max 83 us.
  `GM_SCANOUT` and `/api/debug/scanout` no longer carry `phy_defer`, the
  slip log no longer carries `phy_us`, and `tools/rig_soak.py` parses the
  shorter line. The BLE scan backoff and the 5 s PLL-track period stay
  (less coex churn for free). The A/B script `tools/phydefer_ab.py` lives
  on the branch with the code it drives.
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
