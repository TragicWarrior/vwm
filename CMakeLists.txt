cmake_minimum_required(VERSION 2.8)

project(vwm)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall")
set(PROJECT_DESCRIPTION "A window manager for the console.")

if (("${CMAKE_C_COMPILER_ID}" STREQUAL "Clang") OR (("${CMAKE_C_COMPILER_ID}" STREQUAL "GNU") AND (CMAKE_C_COMPILER_VERSION VERSION_GREATER 4.0)))
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wextra")
endif()

# the following is until we learn how to reorder the gcc arguments to correctly link on Ubuntu
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--no-as-needed")

add_definitions("-D_REENTRANT")
add_definitions("-D_GNU_SOURCE")

set(CURSES_NEED_NCURSES "TRUE")
find_package(Curses REQUIRED)

set(CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake)

find_package(VIPER REQUIRED)

find_package(GPM)

if (!GPM_FOUND)
add_definitions("-D_NO_GPM")
endif()

find_package(CONFIG)

add_executable(vwm
    bkgd.c
    clock.c
    hotkeys.c
    mainmenu.c
    modules.c
    poll_input_thd.c
    private.c
    profile.c
    settings.c
    programs.c
    signals.c
    strings.c
    vwm.c
    vwm_panel.c
    )

if (GPM_FOUND)
target_link_libraries(vwm -lutil -lrt -lprotothread ${CONFIG_LIBRARY} ${GPM_LIBRARIES} ${CURSES_LIBRARIES} ${CMAKE_DL_LIBS} "${VIPER_LIBRARY}" )
else()
target_link_libraries(vwm -lutil -lrt -lprotothread ${CONFIG_LIBRARY} ${CURSES_LIBRARIES} ${CMAKE_DL_LIBS} "${VIPER_LIBRARY}")
endif()

# set the subst vars that configure_file() will use
set(PROJECT_VERSION 1.0)

set(PKG_CONFIG_LIBS
    "-lviper -lprotothread"
)

install (TARGETS vwm DESTINATION bin)

