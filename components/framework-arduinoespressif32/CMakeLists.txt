# Name shim so esp-nimble-cpp can find PlatformIO's Arduino component.
#
# esp-nimble-cpp 2.5.0 detects a PlatformIO Arduino build by substring-matching
# "___idf_framework-arduinoespressif32" against __COMPONENT_TARGETS, and then
# PRIV_REQUIRES the bare name "framework-arduinoespressif32". pioarduino
# registers the Arduino source tree under a hashed directory name
# (framework-arduinoespressif32-src-<md5>), so the substring test passes but the
# bare name is not a registered component and idf_build_process aborts:
#
#   Failed to resolve component 'framework-arduinoespressif32' required by
#   component 'h2zero__esp-nimble-cpp': unknown name.
#
# This hits every env on this branch, not just one. The fix cannot live in
# managed_components/ (the component manager regenerates it) and cannot come
# from a version bump (2.5.0 is both the only 2.5.x and the newest release), so
# it lives here: a component with the name nimble asks for, forwarding to the
# real one. REQUIRES (not PRIV_REQUIRES) so the Arduino include dirs propagate
# through to nimble -- with CONFIG_ENABLE_ARDUINO_DEPENDS=y, NimBLEDevice.cpp
# includes esp32-hal-bt.h and calls btStarted().

idf_build_get_property(_component_targets __COMPONENT_TARGETS)

set(_arduino_components "")
foreach(_target IN LISTS _component_targets)
    if(_target MATCHES "^___idf_(framework-arduinoespressif32-.+)$")
        list(APPEND _arduino_components "${CMAKE_MATCH_1}")
    endif()
endforeach()

if(_arduino_components STREQUAL "")
    # Reached when this shim is pulled into a build with no PlatformIO Arduino
    # component at all. Register empty rather than fail: nimble's substring test
    # matches this shim's own target, so the requirement exists even when there
    # is nothing to forward it to.
    message(STATUS "framework-arduinoespressif32 shim: no pioarduino Arduino component found, registering empty")
else()
    message(STATUS "framework-arduinoespressif32 shim: forwarding to ${_arduino_components}")
endif()

idf_component_register(REQUIRES ${_arduino_components})
