cmake_minimum_required(VERSION 3.20)

# Headless build of firmware/ui with LVGL (see README.md). Linux (GCC,
# Clang) and Windows (MSVC).
project(ui_simulator LANGUAGES C)

set(FW_DIR ${CMAKE_CURRENT_LIST_DIR}/..)
set(LVGL_DIR ${FW_DIR}/STM32AudioController/Middlewares/Third_Party/lvgl)
set(UI_DIR ${FW_DIR}/ui)

if(NOT EXISTS ${LVGL_DIR}/lvgl.h)
    message(FATAL_ERROR "LVGL not found in ${LVGL_DIR}. Run: git submodule update --init")
endif()

# Warnings as errors for our own code; LVGL is built with defaults.
if(MSVC)
    set(UI_WARNINGS /W4 /WX)
    add_compile_options(/utf-8)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
    set(UI_C_STANDARD 11)  # MSVC has no C99 mode
else()
    set(UI_WARNINGS -Wall -Wextra -Werror)
    set(UI_C_STANDARD 99)
endif()

# LVGL sources, configured by firmware/ui/lv_conf.h (the same file the
# firmware will use). Options turned off in lv_conf.h compile to nothing.
file(GLOB_RECURSE LVGL_SOURCES CONFIGURE_DEPENDS ${LVGL_DIR}/src/*.c)
add_library(lvgl STATIC ${LVGL_SOURCES})
target_include_directories(lvgl PUBLIC ${UI_DIR})
# SYSTEM: warnings inside LVGL's headers are not ours to fix.
target_include_directories(lvgl SYSTEM PUBLIC ${LVGL_DIR})
target_compile_definitions(lvgl PUBLIC LV_CONF_INCLUDE_SIMPLE UI_SIMULATOR)
set_target_properties(lvgl PROPERTIES C_STANDARD ${UI_C_STANDARD})

add_library(ui STATIC
    ${UI_DIR}/ui.c
    ${UI_DIR}/ui_text.c
    ${UI_DIR}/ui_theme.c
    ${UI_DIR}/ui_settings.c
    ${UI_DIR}/ui_edit.c
    ${UI_DIR}/ui_layouts.c
    ${UI_DIR}/ui_shell.c
    ${UI_DIR}/ui_dash.c
    ${UI_DIR}/ui_music.c
    ${UI_DIR}/fonts/ui_font_14.c
    ${UI_DIR}/fonts/ui_font_16.c
    ${UI_DIR}/fonts/ui_font_22.c
    ${UI_DIR}/fonts/ui_icons_40.c)
target_include_directories(ui PUBLIC ${UI_DIR})
target_link_libraries(ui PUBLIC lvgl)
set_target_properties(ui PROPERTIES C_STANDARD ${UI_C_STANDARD})
set_source_files_properties(${UI_DIR}/ui.c ${UI_DIR}/ui_text.c ${UI_DIR}/ui_theme.c ${UI_DIR}/ui_settings.c ${UI_DIR}/ui_edit.c ${UI_DIR}/ui_layouts.c ${UI_DIR}/ui_shell.c ${UI_DIR}/ui_dash.c ${UI_DIR}/ui_music.c
    PROPERTIES COMPILE_OPTIONS "${UI_WARNINGS}")

add_executable(ui_simulator main.c)
target_link_libraries(ui_simulator PRIVATE ui)
target_compile_options(ui_simulator PRIVATE ${UI_WARNINGS})
set_target_properties(ui_simulator PROPERTIES C_STANDARD ${UI_C_STANDARD})
