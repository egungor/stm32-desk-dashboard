cmake_minimum_required(VERSION 3.22)

#
# This file is generated only once,
# and is not re-generated if converter is called multiple times.
#
# User is free to modify the file as much as necessary
#

# Setup compiler settings
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)


# Define the build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

# Set the project name
set(CMAKE_PROJECT_NAME STM32AudioController)

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Core project settings
project(${CMAKE_PROJECT_NAME})
message("Build type: " ${CMAKE_BUILD_TYPE})

# Enable CMake support for ASM and C languages
enable_language(C ASM)

# Create an executable object type
add_executable(${CMAKE_PROJECT_NAME})

# Add STM32CubeMX generated sources
add_subdirectory(cmake/stm32cubemx)

# Link directories setup
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined library search paths
)

# LVGL (git submodule) and the hardware-independent UI in firmware/ui.
# LVGL is the one place that uses a glob: it has several hundred sources,
# and options disabled in firmware/ui/lv_conf.h compile to nothing.
set(LVGL_DIR ${CMAKE_CURRENT_SOURCE_DIR}/Middlewares/Third_Party/lvgl)
set(UI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../ui)
if(NOT EXISTS ${LVGL_DIR}/lvgl.h)
    message(FATAL_ERROR "LVGL not found in ${LVGL_DIR}. Run: git submodule update --init")
endif()
file(GLOB_RECURSE LVGL_SOURCES CONFIGURE_DEPENDS ${LVGL_DIR}/src/*.c)

option(APP_TEST_SCREEN "Show the M1 test pattern and touch targets instead of the UI" OFF)
option(APP_UI_DEMO "Feed the UI with demo data instead of the USB host link" OFF)

# Firmware version (ADR-011): the FW_VERSION environment variable (CI: the
# tag without "v", else dev+<commit>), else "git describe" at configure
# time (configure again after a commit), else "dev". It goes into the USB
# configuration string, where the host reads it.
if(DEFINED ENV{FW_VERSION} AND NOT "$ENV{FW_VERSION}" STREQUAL "")
    set(FW_VERSION "$ENV{FW_VERSION}")
else()
    execute_process(COMMAND git describe --tags --always --dirty
                    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                    OUTPUT_VARIABLE FW_VERSION OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    string(REGEX REPLACE "^v" "" FW_VERSION "${FW_VERSION}")
endif()
if(FW_VERSION STREQUAL "")
    set(FW_VERSION "dev")
endif()
message("Firmware version: ${FW_VERSION}")

# Shared protocol codec and the portable STM32 protocol client.
set(PROTOCOL_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../protocol)
set(LINK_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../link)

# Add sources to executable
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user sources here
    App/Src/app.c
    App/Src/clock_check.c
    App/Src/lcd_test.c
    App/Src/link_port.c
    App/Src/log.c
    App/Src/rtos_diag.c
    App/Src/sdram.c
    App/Src/touch.c
    App/Src/touch_log.c
    App/Src/ui_demo.c
    App/Src/ui_port.c
    App/Src/usb_link.c
    ${LINK_DIR}/link_client.c
    ${LINK_DIR}/dash_client.c
    ${PROTOCOL_DIR}/src/protocol.c
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
    ${UI_DIR}/fonts/ui_icons_40.c
    ${LVGL_SOURCES}
)

# Add include paths
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined include paths
    App/Inc
    ${UI_DIR}
    ${LVGL_DIR}
    ${LINK_DIR}
    ${PROTOCOL_DIR}/include
)

# Add project symbols (macros)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined symbols
    LV_CONF_INCLUDE_SIMPLE
    APP_TEST_SCREEN=$<BOOL:${APP_TEST_SCREEN}>
    APP_UI_DEMO=$<BOOL:${APP_UI_DEMO}>
    FW_VERSION="${FW_VERSION}"
)

# Remove wrong libob.a library dependency when using cpp files
list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)

# Add linked libraries
target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx

    # Add user defined libraries
)
