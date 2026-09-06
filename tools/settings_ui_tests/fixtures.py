"""Preflight and simulator seeding for the settings UI runner (gm-flw.13).

Two jobs, both about the state a scenario starts from:

1. `seed(data_dir)` writes `fixtures/controller.json` into a fresh
   simulator data directory before the process starts. The simulator's NVS
   is one JSON object per namespace (`sim/platform/preferences_shim.cpp`),
   string values under the short NVS keys `Settings.h` registers, and
   `Preferences::begin` loads whatever is there and `end()` writes the
   merged map back, so a seeded key survives the boot untouched while the
   simulator adds its own (the selected profile, mostly). The runner never
   POSTs `/api/settings`: seeding a file before launch is the only way it
   puts a value into the device model it cannot reach through a stepper.

2. `preflight(rig, venue)` checks, before anything is changed, that the
   instruments answer and that every scenario's starting value is
   reachable on the row that owns it. A stored value off its stepper's
   grid (a 45 s dim timeout on the 30 s grid, a colour outside the twelve
   named palette entries) cannot be restored after a scenario steps away
   from it, so the scenario that touches that field is marked failed
   before it runs rather than left to corrupt the device's settings.

The grids come from `src/display/ui/default/settings/SettingsModel.cpp`'s
NumericSpec table and `kPalette`; the wire format of each field is the
shared contract's (seconds for the two timeouts, numeric strings for the
offset and the pressure scaling, `#rrggbb` for the colours).
"""

import json
import os

from .rig import RigHTTPError, color_hex, num, schedules, seconds

HERE = os.path.dirname(os.path.abspath(__file__))
FIXTURE_PATH = os.path.join(HERE, "fixtures", "controller.json")

# The twelve named palette entries (SettingsModel.cpp kPalette). A colour
# outside this list survives one visit as a thirteenth "#rrggbb" choice and
# is gone once the row cycles away from it, so a scenario that changes a
# colour row cannot start from one.
PALETTE = (
    0xFFFFFF, 0xFFE0B3, 0xFFBF00, 0xFF8000,
    0xFF0000, 0xFF80C0, 0x8000FF, 0x0000FF,
    0x00FFFF, 0x008080, 0x00FF00, 0x000000,
)


def seed(data_dir, fixture_path=FIXTURE_PATH):
    """Writes the fixture into `<data_dir>/nvs/controller.json`. data_dir is
    the simulator's `sim_data` directory; the file must be in place before
    the process starts, since Preferences reads it once at begin()."""
    nvs_dir = os.path.join(data_dir, "nvs")
    os.makedirs(nvs_dir, exist_ok=True)
    with open(fixture_path, encoding="utf-8") as f:
        fixture = json.load(f)
    target = os.path.join(nvs_dir, "controller.json")
    with open(target, "w", encoding="utf-8") as f:
        json.dump(fixture, f, indent=2, sort_keys=True)
    return target


# ---- representability -------------------------------------------------------
#
# One entry per checked field: the wire key, the scenario that would step it,
# and a predicate over the parsed wire value. `parse` turns the wire value
# into the number/tuple the predicate wants and raises on anything it cannot
# read at all (which is itself an unmet condition).


def _on_grid(value, lo, hi, step):
    return lo <= value <= hi and (value - lo) % step == 0


def _tenths(value):
    """A pressure scaling in tenths of a bar, exactly: 16.00 -> 160. The
    stepper's grid is one tenth, so a value with a hundredths digit has no
    grid line."""
    scaled = round(value * 10)
    return scaled if abs(value * 10 - scaled) < 1e-6 else None


CHECKS = (
    # key, scenario, parse, predicate, description of the grid
    ("temperatureOffset", "temps", num, lambda v: _on_grid(int(v), -20, 20, 1) and float(v) == int(v),
     "whole degrees, -20..20"),
    ("pressureScaling", "temps", num, lambda v: _tenths(v) is not None and _on_grid(_tenths(v), 10, 300, 1),
     "tenths of a bar, 1.0..30.0"),
    ("brewDelay", "temps", num, lambda v: float(v) == int(v) and _on_grid(int(v), 0, 4000, 50),
     "50 ms steps, 0..4000 ms"),
    ("grindDelay", "temps", num, lambda v: float(v) == int(v) and _on_grid(int(v), 0, 4000, 50),
     "50 ms steps, 0..4000 ms"),
    ("mainBrightness", "display", int, lambda v: _on_grid(v, 1, 16, 1), "1..16"),
    ("standbyBrightness", "display", int, lambda v: _on_grid(v, 0, 16, 1), "0..16"),
    ("standbyBrightnessTimeout", "display", seconds, lambda v: _on_grid(v * 1000, 30000, 1800000, 30000),
     "30 s steps, 30 s..30 min"),
    ("standbyTimeout", "machine", seconds, lambda v: _on_grid(v * 1000, 0, 14400000, 60000),
     "1 min steps, 0 (Never)..4 h"),
    ("bgAnimFps", "animation", int, lambda v: _on_grid(v, 5, 60, 5), "5 fps steps, 5..60"),
    ("bgAnimPlateOpacity", "animation", int, lambda v: _on_grid(v, 0, 100, 5), "5 % steps, 0..100"),
    ("bgAnimScrim", "animation", int, lambda v: _on_grid(v, 0, 100, 5), "5 % steps, 0..100"),
    ("bgAnimPlateColor", "animation", color_hex, lambda v: (v[0] << 16 | v[1] << 8 | v[2]) in PALETTE,
     "one of the twelve named palette colours"),
    ("elementTintColor", "animation", color_hex, lambda v: (v[0] << 16 | v[1] << 8 | v[2]) in PALETTE,
     "one of the twelve named palette colours"),
)

# The runner's own restart round trip steps Standby brightness, so it needs
# the same condition the Display scenario does; named here so a caller can
# ask whether the restart phase is supported without duplicating the map.
RESTART_KEY = "standbyBrightness"
RESTART_SCENARIO = "display"

# Fixtures the display cannot create for itself. Each is (name, scenario,
# predicate over the settings dict).
FIXTURE_ROWS = (
    ("library_gradient", "animation", lambda s: bool(s.get("bgAnimGradients", "").strip())),
    ("schedule", "schedules", lambda s: len(schedules(s.get("autowakeupSchedules", ""))) >= 1),
)

# Every instrument the runner needs before it changes anything. A production
# build compiles none of the first three (GM_TOUCH_PROBE / GAGGIMATE_SIM),
# so pointing --host at one fails here, naming the route.
INSTRUMENTS = (
    "/api/debug/tap",
    "/api/debug/touchmap?screen=0",
    "/api/debug/settingsui",
)


class Venue:
    """What a scenario's `run(rig, report, venue)` is told about where it is
    running. The runner builds one after launching (or connecting to) the
    venue; each scenario's own `main()` builds an equivalent one so the
    script stays runnable alone.

    `sim` is the `rig.Sim` behind a simulator run and None on the device, so
    a check that must restart or relaunch the process tests for it; the same
    check also honours `skip_restart`. `log_path` is the simulator's
    combined stdout/stderr file, which is the only place some of the
    firmware's category log lines can be read (there is no HTTP route for
    them), so it is None on the device too.
    """

    def __init__(self, sim=None, program=None, workdir=None, port=None, host=None, log_path=None,
                 is_device=False, skip_restart=False):
        self.sim = sim
        self.program = program
        self.workdir = workdir
        self.port = port
        self.host = host
        self.log_path = log_path
        self.is_device = is_device
        self.skip_restart = skip_restart

    @property
    def can_restart(self):
        """True when a check may restart or relaunch the process: only on a
        simulator run, and only when the caller did not ask to skip it."""
        return self.sim is not None and not self.skip_restart


class Preflight:
    """The result: which instruments answered, which conditions were unmet,
    and the set of scenarios that must be marked failed before they run."""

    def __init__(self):
        self.missing_instruments = []
        self.unsupported = []  # {"key", "value", "scenario", "want"}
        self.blocked = set()   # scenario names
        self.settings = None

    @property
    def ok(self):
        return not self.missing_instruments and not self.unsupported

    def blocks(self, scenario):
        return scenario in self.blocked


def preflight(rig, log=None):
    """Reads the instruments and the current settings and reports every
    unmet condition. Changes nothing: this runs before the warm-up, so a
    refusal costs the venue nothing."""
    result = Preflight()
    for path in INSTRUMENTS:
        try:
            rig.get_json(path)
        except RigHTTPError as e:
            result.missing_instruments.append((path, str(e)))
            if log:
                log("MISSING INSTRUMENT", route=path, error=str(e))
    if result.missing_instruments:
        return result

    s = rig.settings()
    result.settings = s
    for key, scenario, parse, ok, want in CHECKS:
        raw = s.get(key)
        try:
            value = parse(raw)
            good = bool(ok(value))
        except (TypeError, ValueError):
            value, good = raw, False
        if not good:
            result.unsupported.append({"key": key, "value": raw, "scenario": scenario, "want": want})
            result.blocked.add(scenario)
            if log:
                log("UNSUPPORTED FIXTURE", key=key, value=raw, scenario=scenario, want=want)
    for name, scenario, present in FIXTURE_ROWS:
        if not present(s):
            result.unsupported.append({"key": name, "value": "(absent)", "scenario": scenario,
                                       "want": "at least one, seeded before boot"})
            result.blocked.add(scenario)
            if log:
                log("UNSUPPORTED FIXTURE", key=name, value="(absent)", scenario=scenario)
    return result
