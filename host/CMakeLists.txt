cmake_minimum_required(VERSION 3.20)

# Windows host application.
#
# host_core: platform-independent device model and protocol controller,
#            with tests; builds on Windows and Linux.
# dashboard-host: Windows console program on Core Audio and USB HID
#                   (Windows only).
# dashboard-tray: the same service as "dashboard-host run" as a system
#                   tray program (Windows only, #74).
project(stm32_audio_host LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Shared protocol codec (C), same sources as the firmware.
set(PROTOCOL_DIR ${CMAKE_CURRENT_LIST_DIR}/../protocol)
add_library(protocol STATIC ${PROTOCOL_DIR}/src/protocol.c)
target_include_directories(protocol PUBLIC ${PROTOCOL_DIR}/include)

add_library(host_core STATIC
    Audio/AudioMapping.cpp
    Audio/DeviceRegistry.cpp
    Audio/VolumeLimit.cpp
    App/FrameText.cpp
    App/HostController.cpp
    App/PreferenceFile.cpp
    App/PreferenceStore.cpp
    App/Preferences.cpp
    App/Startup.cpp)
target_include_directories(host_core PUBLIC Audio Hid App)
target_link_libraries(host_core PUBLIC protocol)

if(MSVC)
    target_compile_options(host_core PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(host_core PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Werror)
endif()

if(WIN32)
    enable_language(RC)

    # Windows sources shared by the console and the tray program.
    add_library(host_windows STATIC
        Platform/Windows/HostService.cpp
        Platform/Windows/WinAudioSystem.cpp
        Platform/Windows/WinHidLink.cpp
        Platform/Windows/WinPreferenceFile.cpp
        Platform/Windows/WinStartup.cpp
        Platform/Windows/WinStrings.cpp)
    target_include_directories(host_windows PUBLIC Platform/Windows)
    target_compile_definitions(host_windows PUBLIC WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
    target_link_libraries(host_windows PUBLIC host_core ole32 setupapi hid shell32 advapi32 user32)

    # Console program: run, list, set-default, volume, mute, watch, alias, startup.
    add_executable(dashboard-host Platform/Windows/main.cpp)
    target_link_libraries(dashboard-host PRIVATE host_windows)

    # Tray program (#74): no console; same service as "run".
    add_executable(dashboard-tray WIN32 Platform/Windows/tray.cpp Platform/Windows/tray.rc)
    target_link_libraries(dashboard-tray PRIVATE host_windows)
    if(NOT MSVC)
        target_link_options(dashboard-tray PRIVATE -municode)
    endif()

    foreach(target host_windows dashboard-host dashboard-tray)
        if(MSVC)
            target_compile_options(${target} PRIVATE /W4 /WX /permissive- /utf-8)
        else()
            target_compile_options(${target} PRIVATE -Wall -Wextra -Werror)
        endif()
    endforeach()
endif()

include(CTest)
if(BUILD_TESTING)
    find_package(Threads REQUIRED)
    add_executable(test_host Tests/test_host.cpp)
    target_link_libraries(test_host PRIVATE host_core Threads::Threads)
    if(MSVC)
        target_compile_options(test_host PRIVATE /W4 /WX /permissive-)
    else()
        target_compile_options(test_host PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
    add_test(NAME host COMMAND test_host)
endif()
