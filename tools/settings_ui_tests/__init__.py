"""Shared rig for the on-display settings UI tests (gm-flw epic).

See rig.py for Rig (HTTP client against the simulator or the device) and Sim
(simulator process launcher). test_rig.py exercises this module on its own
and doubles as the module's own test.
"""

from .rig import Rig, RigHTTPError, Sim, SimError, color_hex, num, schedules, seconds

__all__ = [
    "Rig",
    "RigHTTPError",
    "Sim",
    "SimError",
    "color_hex",
    "num",
    "schedules",
    "seconds",
]
