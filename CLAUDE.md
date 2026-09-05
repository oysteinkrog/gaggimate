# GaggiMate (idf5 branch): agent notes

ESP32-S3 espresso machine controller + display. The display is a LilyGo T-RGB
480x480 round RGB panel scanning out of PSRAM; the controller is a separate
board reached over BLE. Firmware builds with PlatformIO (`pio run -e display`
for production, `-e display-loadtest` for the bench rig).

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

Measure with `-e display-loadtest` (`GM_TOUCH_PROBE`): `GM_UISTAT` lines give
pass/snapshot/publish times and snapshot area per 5 s window; `GM_TOUCHLAT`
lines stamp press→overlay_publish→anim_frame per tap.

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
