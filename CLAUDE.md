# GaggiMate (idf5 branch) — agent notes

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
  %d` — that line is the regression tripwire.
- **Bounce pool stays at 16 scanlines total** (8 buffers x 2 lines, 15,360 B).
  Deeper pools starve the WiFi TX buffer pool: at 20+ lines two concurrent
  browser tabs kill the web UI (measured cliff between 14.7 and 18.3 kB DMA
  free). History in platformio.ini above `GM_LCD_BOUNCE_LINES`.
- **Pixel clock is n=6 (13.33 MHz, 50.7 fps).** n=5's 453us of pool slack is
  under the worst observed refill stall (638us), so it garbles rarely but
  visibly. History in platformio.ini above `RGB_MAX_PIXEL_CLOCK_HZ`.
- Vendored ESP-IDF files are patched by `scripts/patch_*.py` (pre-build
  extra_scripts). Each keeps a pristine `.gm-orig` beside the patched file.
  The Windows and WSL PlatformIO installs share `~/.platformio`; a
  Windows-side build can clobber the patched sources, and the patch scripts
  re-apply on the next WSL build — if a display fault appears out of nowhere,
  check the patches applied in the build log first.

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

## Bench facts

- Device: 192.168.1.121 on the bench, UART on COM3.
- Windows tooling runs Python 3.10 (`GM_RIG_PY` env var to override):
  Python313 silently lacks esptool and pyserial.
- Camera verification: `C:\work\camshots\grab.bat <file>` (one frame),
  `burst.bat` (8s at 6fps). Photos land in C:\work\camshots.
- `/api/settings` returns the WiFi password in cleartext — never dump it, and
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
