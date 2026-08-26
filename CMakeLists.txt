cmake_minimum_required(VERSION 3.10.2)

project(vwm LANGUAGES C)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall")
set(PROJECT_DESCRIPTION "A window manager for the console.")

if (("${CMAKE_C_COMPILER_ID}" STREQUAL "Clang") OR (("${CMAKE_C_COMPILER_ID}" STREQUAL "GNU") AND (CMAKE_C_COMPILER_VERSION VERSION_GREATER 4.0)))
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wextra")
endif()

# the following is until we learn how to reorder the gcc arguments to correctly link on Ubuntu
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--no-as-needed")

add_definitions("-D_REENTRANT")
add_definitions("-D_GNU_SOURCE")

# needed for cchar_t definition and turn on "wide support"
add_definitions("-D_XOPEN_SOURCE_EXTENDED")

# set these before running the find_package directive for Curses
set(CURSES_NEED_NCURSES "TRUE")
set(CURSES_NEED_WIDE "TRUE")

find_package(Curses REQUIRED)

set(CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake)

#find_package(PROTOTHREAD REQUIRED)

find_package(VTERM REQUIRED)

# use libviper source tree (headers + library not yet installed).
# Defaults to a sibling checkout (../libviper); override with
# -DLIBVIPER_SOURCE_DIR=/path/to/libviper at configure time.
set(LIBVIPER_SOURCE_DIR "${PROJECT_SOURCE_DIR}/../libviper"
    CACHE PATH "Path to the libviper source tree (contains vdk/, vkmio/, libvdk.so)")
get_filename_component(LIBVIPER_SOURCE_DIR "${LIBVIPER_SOURCE_DIR}" ABSOLUTE)

if(NOT EXISTS "${LIBVIPER_SOURCE_DIR}/libvdk.so")
  message(FATAL_ERROR
    "libvdk.so not found at ${LIBVIPER_SOURCE_DIR}/libvdk.so. "
    "Build libviper first, or pass -DLIBVIPER_SOURCE_DIR=<path> "
    "to point at your libviper checkout.")
endif()

include_directories(BEFORE ${LIBVIPER_SOURCE_DIR}/vdk ${LIBVIPER_SOURCE_DIR}/vkmio)
set(VDK_LIBRARY "${LIBVIPER_SOURCE_DIR}/libvdk.so")

find_package(GPM)

if (!GPM_FOUND)
add_definitions("-D_NO_GPM")
endif()

find_package(Freetype REQUIRED)

include(GNUInstallDirs)

# Screenshot fonts: empty = search system DejaVu, then the bundled
# copies (installed datadir, then the source-tree fonts/ for uninstalled
# runs).  A non-empty override is the only path tried for that face;
# a missing file is VWM_SHOT_ERR_FONT (no silent fallback).
set(VWM_SCREENSHOT_FONT "" CACHE FILEPATH
    "TTF used to rasterize screenshot regular glyphs")
set(VWM_SCREENSHOT_FONT_BOLD "" CACHE FILEPATH
    "TTF used to rasterize screenshot bold glyphs")

find_package(ZLIB REQUIRED)

find_package(Cups REQUIRED)

add_library(vwmterm SHARED
    modules/vwmterm3/events.c
    modules/vwmterm3/init.c
    modules/vwmterm3/module.c
    modules/vwmterm3/pt_thread.c
    modules/vwmterm3/signals.c
    )

add_library(vwmprint SHARED
    modules/vwmprint/init.c
    modules/vwmprint/cups.c
    )
target_include_directories(vwmprint PRIVATE ${CUPS_INCLUDE_DIRS})
target_link_libraries(vwmprint ${CUPS_LIBRARIES})

add_library(vwmfont SHARED
    modules/vwmfont/vwmfont.c
    modules/vwmfont/init.c
    )
target_include_directories(vwmfont PRIVATE ${ZLIB_INCLUDE_DIRS})
target_link_libraries(vwmfont ${ZLIB_LIBRARIES})

add_executable(vwm
    bkgd.c
    cJSON.c
    config.c
    clock.c
    winman.c
    mainmenu.c
    modules.c
    poll_input_thd.c
    private.c
    profile.c
    sched.c
    settings.c
    screensaver.c
    programs.c
    launch_picker.c
    manage_apps.c
    manage_hotkeys.c
    manage_settings.c
    manage_windows.c
    manage_ui_common.c
    signals.c
    strings.c
    vwm.c
    panel.c
    events.c
    utf8_wide.c
    screenshot.c
    screenshot_capture.c
    screenshot_render.c
    screenshot_png.c
    screenshot_ui.c
    ctl.c
    )

add_executable(vwm-msg
    vwm-msg.c
    )

# Modules dlopen()'d at runtime (libvwmterm.so etc.) resolve symbols like
# vwm_window_decorate against the vwm executable's dynamic symbol table.
# Pass -rdynamic so those globals make it in.
set_target_properties(vwm PROPERTIES ENABLE_EXPORTS ON)

if (GPM_FOUND)
target_link_libraries(vwm -lutil -lm -lrt ${GPM_LIBRARIES} ${CURSES_LIBRARIES} ${CMAKE_DL_LIBS} "${VDK_LIBRARY}" "${VTERM_LIBRARY}" ${FREETYPE_LIBRARIES} )
else()
target_link_libraries(vwm -lutil -lm -lrt ${CURSES_LIBRARIES} ${CMAKE_DL_LIBS} "${VDK_LIBRARY}" "${VTERM_LIBRARY}" ${FREETYPE_LIBRARIES})
endif()
target_include_directories(vwm PRIVATE ${FREETYPE_INCLUDE_DIRS})
target_compile_definitions(vwm PRIVATE
    "VWM_FONTDIR=\"${CMAKE_INSTALL_FULL_DATADIR}/vwm/fonts\""
    "VWM_FONTS_SRCDIR=\"${PROJECT_SOURCE_DIR}/fonts\"")
if(VWM_SCREENSHOT_FONT)
    target_compile_definitions(vwm PRIVATE
        "VWM_SCREENSHOT_FONT=\"${VWM_SCREENSHOT_FONT}\"")
endif()
if(VWM_SCREENSHOT_FONT_BOLD)
    target_compile_definitions(vwm PRIVATE
        "VWM_SCREENSHOT_FONT_BOLD=\"${VWM_SCREENSHOT_FONT_BOLD}\"")
endif()

# "make core" builds the window manager and the terminal module.
# "make" / "make all" additionally builds print and hostname-font modules.
add_custom_target(core)
add_dependencies(core vwm vwmterm)

# set the subst vars that configure_file() will use
set(PROJECT_VERSION 1.0)

set(PKG_CONFIG_LIBS
    "-lvdk"
)


install (TARGETS vwm DESTINATION bin)
install (TARGETS vwm-msg DESTINATION bin)
install (TARGETS vwmterm DESTINATION lib/vwm/)
install (TARGETS vwmprint DESTINATION lib/vwm/)
install (TARGETS vwmfont DESTINATION lib/vwm/)

# dtach session launchers -- PROGRAMS installs them with the execute bit
# set, alongside the vwm binary so they share its PATH (and tab-complete
# together as vwm-*)
install (PROGRAMS scripts/vwm-start scripts/vwm-resume DESTINATION bin)
install (FILES
    fonts/DejaVuSansMono.ttf
    fonts/DejaVuSansMono-Bold.ttf
    fonts/LICENSE
    fonts/AUTHORS
    fonts/README
    DESTINATION ${CMAKE_INSTALL_DATADIR}/vwm/fonts)
