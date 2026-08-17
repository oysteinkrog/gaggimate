# GAGGIMATE_CONTROLLER_BUILD=1 selects src/controller/**; default is src/display/**.
if(DEFINED GAGGIMATE_CONTROLLER_BUILD AND GAGGIMATE_CONTROLLER_BUILD)
    FILE(GLOB_RECURSE app_sources
        ${CMAKE_SOURCE_DIR}/src/controller/*.c
        ${CMAKE_SOURCE_DIR}/src/controller/*.cpp
    )
else()
    FILE(GLOB_RECURSE app_sources
        ${CMAKE_SOURCE_DIR}/src/display/*.c
        ${CMAKE_SOURCE_DIR}/src/display/*.cpp
    )
    # Headless envs opt out of driver/ui code via -DGAGGIMATE_HEADLESS_BUILD=1.
    #
    # This is the ONLY source selection that takes effect. PlatformIO's
    # build_src_filter is inert under ESP-IDF -- it says so on every build
    # ("Warning: the 'src_filter' option cannot be used with ESP-IDF") -- so any
    # per-env include/exclude has to be expressed here.
    if(DEFINED GAGGIMATE_HEADLESS_BUILD AND GAGGIMATE_HEADLESS_BUILD)
        list(FILTER app_sources EXCLUDE REGEX "/src/display/drivers/")
        list(FILTER app_sources EXCLUDE REGEX "/src/display/ui/")
        # ...except PanelClock, which WebUIPlugin needs whether or not a panel
        # exists: /api/settings reports panelclock::hasLiveControl(). The file is
        # self-contained (esp_lcd_panel_rgb.h is its only IDF dependency) and every
        # entry point is a no-op until attach() is handed a panel, which a headless
        # build never does.
        FILE(GLOB_RECURSE panelclock_sources ${CMAKE_SOURCE_DIR}/src/display/drivers/common/PanelClock.cpp)
        list(APPEND app_sources ${panelclock_sources})
    endif()
endif()

idf_component_register(SRCS ${app_sources}
    INCLUDE_DIRS ${CMAKE_SOURCE_DIR}/src
    REQUIRES esp-nimble-cpp
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:-std=gnu++20>
)
