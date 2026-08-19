# Contributing to GaggiMate

Thank you for considering a contribution to the project. This document explains the repository structure and how to get involved.

## CLA

We require contributors to sign our Contributor License Agreement. To review
and merge your code, please contact [@jniebuhr](https://github.com/jniebuhr)
(mdwasp) on Discord to access the CLA. A signed CLA is required for
contributing.

## Codebase Overview

The repository is organized into the following folders:

```
.
├── boards/             # Custom PlatformIO board definitions
├── docs/               # Documentation assets and diagrams
├── icons/              # Source icons and conversion scripts
├── lib/                # PlatformIO libraries such as GaggiMateController
├── partitions/         # Flash partition tables, one per flash size and target
├── scripts/            # Helper scripts for builds and formatting
├── sim/                # Host shims that let the display firmware run on a desktop
├── src/
│   ├── CMakeLists.txt  # The source selection that actually takes effect
│   ├── controller/     # Firmware for the controller board
│   └── display/        # Firmware for the display unit (LVGL UI, plugins)
├── test/               # Host-side unit tests (pio test)
├── tools/              # Host-side measurement tools, e.g. tools/animbench
├── ui/                 # SquareLine Studio project for the LVGL UI
├── web/                # Preact-based web interface
├── sdkconfig.*.defaults# ESP-IDF configuration, layered per environment
└── platformio.ini      # PlatformIO configuration
```

### Build environments

Two environments are shipped by the release workflow:

| Environment  | Board                | Notes                                         |
| ------------ | -------------------- | --------------------------------------------- |
| `display`    | LilyGo-T-RGB         | 16 MB flash, dual OTA. The LVGL UI and web UI |
| `controller` | Gaggimate-Controller | 8 MB flash, dual OTA. Hardware and safety     |

The rest exist for development and are not published:

| Environment           | Purpose                                                                                                                                                 |
| --------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `display-demo`        | Display firmware with `GM_FAKE_CONTROLLER`, so every screen is reachable with no controller board attached. BLE never starts and telemetry is synthetic |
| `display-bench`       | As above plus `GM_ANIM_BENCH`, which runs a background-animation measurement sweep instead of honouring the animation settings                          |
| `display-headless`    | Display firmware with the panel drivers and LVGL UI compiled out. Useful for working on networking, plugins or the web UI without hardware              |
| `display-headless-8m` | `display-headless` on an 8 MB Seeed XIAO ESP32-S3. Single app slot, so BLE OTA is refused at runtime; flash it over USB                                 |
| `native_autotune`     | Host build of the SIMC autotune unit tests. `pio test -e native_autotune`                                                                               |
| `display-sim`         | Desktop simulator, see below. Requires SDL2                                                                                                             |

`native_autotune` is a host build, so it needs a host `gcc`/`g++` on the PATH of
whichever PlatformIO runs it. Without one it errors in well under a second with
`'gcc' is not recognized` (Windows) or `command not found`. That is a missing
toolchain, not a failing test; the same environment passes in CI.

### Source selection: read `src/CMakeLists.txt`, not `build_src_filter`

The display and controller build with `framework = arduino, espidf`. Under
ESP-IDF, PlatformIO's `build_src_filter` does nothing at all, and it says so on
every build:

```
Warning: the 'src_filter' option cannot be used with ESP-IDF
```

The `build_src_filter` lines still present in `platformio.ini` are kept only to
document intent and to keep the environments looking alike. **The only source
selection that takes effect is the glob in `src/CMakeLists.txt`**, driven by
`-DGAGGIMATE_CONTROLLER_BUILD=1` and `-DGAGGIMATE_HEADLESS_BUILD=1` passed
through `board_build.cmake_extra_args`. If a file you added is not being
compiled, or one you expected to be excluded is, that file is where to look.

Two consequences worth knowing before they surprise you:

- The glob has no `CONFIGURE_DEPENDS` (IDF re-evaluates the file in cmake script
  mode, where CONFIGURE_DEPENDS is rejected), so adding or deleting a source
  file needs a reconfigure rather than just a rebuild.
- `.S` files under `src/` are not collected by the dual-framework build.
  `webassets/web_ui_blob.S` is therefore assembled explicitly by
  `scripts/embed_webui_pre.py`.

### ESP-IDF configuration

`sdkconfig.*` files in the project root are generated per environment and are
git-ignored. The tracked inputs are the `sdkconfig.*.defaults` files, applied
left to right by `SDKCONFIG_DEFAULTS` in each environment's
`board_build.cmake_extra_args`, last assignment winning. `sdkconfig.common.defaults`
carries the shared settings and the reasoning behind each one;
`scripts/assert_sdkconfig.py` checks a set of invariants after every link.

The pioarduino platform is pinned to a specific version in `platformio.ini`, and
the comment there explains why: later ESP-IDF revisions ship a Bluetooth
controller blob that crashes NimBLE bring-up on ESP32-S3 rev v0.2. Do not bump
it casually.

### Controller Firmware

Located in `lib/GaggiMateController` and `src/controller`.
Implements hardware control and BLE communication.
`GaggiMateController.*` sets up peripherals (heater, pump, valve) and handles safety mechanisms such as thermal runaway shutoff.
Communication with the display uses the NimBLE library.

### Display Firmware

Resides in `src/display`.
`core/Controller.h` orchestrates Wi‑Fi, Bluetooth, profiles, and process control.
Uses LVGL for graphics, with UI assets generated by SquareLine Studio (`ui/` folder).
Plugins (e.g., BLEScalePlugin, MQTTPlugin, BoilerFillPlugin, HomekitPlugin) extend functionality via an event-driven PluginManager.

### Web Interface

The `web/` directory contains a Preact + Vite project. The README inside explains how to run it:

- `npm run dev` - Starts a dev server at <http://localhost:5173/>
- `npm run build` - Builds for production, emitting to `dist/`
- `npm run preview` - Starts a server at <http://localhost:4173/> to test production build locally

`scripts/build_webui.sh` installs the web dependencies, builds the bundle,
gzips it and packs it into `src/display/webassets/`, from where it is embedded
directly into the display app image and served out of memory-mapped flash. It no
longer ships in the filesystem image: LittleFS holds only profiles and shot
history, so an OTA update never touches user data.

A display build without a prior `build_webui.sh` still links. The pre-build hook
substitutes an empty stub and the device serves an empty UI, so run the script
whenever you actually want to see the web interface.

### Next Steps for Learning

- Review plugin implementations in `src/display/plugins` to see how new functionality hooks into the controller using events.
- Look at `src/display/core/Settings.*` for how user preferences are stored and updated.
- Study the BLE protocol defined in `lib/NanoPbComm` (nanopb schema in `lib/NanoPbComm/proto/gaggimate.proto`) to understand how the display and controller communicate.

## Getting Started

1. **Fork the repository** and create your branch from `master`.

2. **Install dependencies**:

   - Run `npm install` inside the `web/` directory for the web interface.
   - Ensure [PlatformIO](https://platformio.org/install/cli) is available for firmware builds.

3. **Build the project** to verify your environment:

   ```shell
   platformio run -e display -e controller
   scripts/build_webui.sh
   ```

4. Choose what to deploy:

   | Asset               | Commands                                                                            |
   | ------------------- | ----------------------------------------------------------------------------------- |
   | Display Firmware    | `platformio run -e display -t upload -t monitor`                                    |
   | Controller Firmware | `platformio run -e controller -t upload -t monitor`                                 |
   | Web UI              | <pre>scripts/build_webui.sh<br>platformio run -e display -t upload -t monitor</pre> |

   **NOTE**: You can omit `-t monitor` if you don't want to immediately attach to the board's serial console.

## Simulator

You can run parts of the project directly on your computer using SDL2. To do this, use the `display-sim` environment in platformio.
This target mocks out the controller PCB but workings of processes, touch UI and web UI can be tested.

```
platformio run -e display-sim -t run
```

## Code Style

- Format C/C++ sources using `scripts/format.sh` which applies the project's `clang-format` settings.
- For web code, use `npm run format` to apply Prettier.
- Run static analysis with `platformio check -e display` and `platformio check -e controller`.
- Run the host-side unit tests with `platformio test -e native_autotune`.
- After touching anything under `src/display/ui/default/bganim/`, run
  `make -C tools/animbench check`. It re-renders the 39 golden frames and checks
  that `band()` returns the same row whatever strip height it is asked for, which
  is what the interlaced half-res sleep path relies on. Plain `g++`, a few seconds.
- For markdown files, use `npx prettier -w <file>.md`

## Pull Requests

- Keep PRs focused and describe the motivation behind your change.
- Include screenshots when modifying the UI.
- Ensure all formatting and checks pass before opening the PR. The push/PR
  workflow compiles `controller`, `display` and `display-headless`, builds the
  web bundle, runs the unit tests and checks the animation goldens, so a PR that
  does not build will say so.

## Reporting Issues

If you encounter a problem, open an issue describing the steps to reproduce and any relevant logs.
