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
    App/BackgroundFetch.cpp
    App/DashController.cpp
    App/FrameRouter.cpp
    App/FrameText.cpp
    App/HostController.cpp
    App/MediaKeys.cpp
    App/PreferenceFile.cpp
    App/PreferenceStore.cpp
    App/Preferences.cpp
    App/SettingsModel.cpp
    App/Startup.cpp
    Hook/ApprovalService.cpp
    Hook/ClaudeHook.cpp
    Media/MediaCover.cpp
    Media/MediaService.cpp
    Notify/NotificationService.cpp
    Picture/PictureService.cpp
    Notify/ToastText.cpp
    Menu/MenuData.cpp
    Menu/MenuImport.cpp
    Menu/MenuLists.cpp
    Menu/MenuService.cpp
    Weather/WeatherCard.cpp
    Weather/WeatherLocation.cpp
    Weather/WeatherReport.cpp
    Weather/WeatherService.cpp)
target_include_directories(host_core PUBLIC Audio Hid App Hook Media Menu Notify Picture Weather)
# nlohmann/json (ADR-006), vendored: no warnings from it.
target_include_directories(host_core SYSTEM PRIVATE third_party)
find_package(Threads REQUIRED)
target_link_libraries(host_core PUBLIC protocol Threads::Threads)

if(MSVC)
    target_compile_options(host_core PRIVATE /W4 /WX /permissive- /utf-8)
else()
    target_compile_options(host_core PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Werror)
endif()

# The program's version for the settings window and "dashboard-host
# version" (ADR-011): the installer workflow passes the setup's version.
set(DASHBOARD_VERSION "dev" CACHE STRING "Version shown by the host programs")

if(WIN32)
    enable_language(RC)

    # The C++/WinRT sources: WinMediaSession (ADR-008; WIC), the notification
    # listener (ADR-009) and the notification probe of spike #33 (winsqlite3,
    # Windows' own SQLite).
    # The C++/WinRT headers need C++20 coroutines (in C++17 they fall back to
    # <experimental/coroutine>, which current MSVC refuses), so these files
    # are their own library in C++20; the rest of the host stays C++17.
    add_library(host_media_win STATIC Platform/Windows/WinMediaSession.cpp Platform/Windows/WinNotificationListener.cpp
                Platform/Windows/WinNotificationProbe.cpp)
    target_include_directories(host_media_win PUBLIC Platform/Windows)
    target_compile_definitions(host_media_win PUBLIC WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
    set_target_properties(host_media_win PROPERTIES CXX_STANDARD 20)
    target_link_libraries(host_media_win PUBLIC host_core windowsapp windowscodecs shcore ole32 winsqlite3 shell32 advapi32)

    # Windows sources shared by the console and the tray program.
    add_library(host_windows STATIC
        Platform/Windows/HostService.cpp
        Platform/Windows/WinAudioSystem.cpp
        Platform/Windows/WinHidLink.cpp
        Platform/Windows/WinHookPipe.cpp
        Platform/Windows/WinHttpClient.cpp
        Platform/Windows/WinMediaKeys.cpp
        Platform/Windows/WinMenuImport.cpp
        Platform/Windows/WinPicture.cpp
        Platform/Windows/WinPreferenceFile.cpp
        Platform/Windows/WinSettingsSignal.cpp
        Platform/Windows/WinStartup.cpp
        Platform/Windows/WinStrings.cpp)
    target_include_directories(host_windows PUBLIC Platform/Windows)
    target_compile_definitions(host_windows PUBLIC WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE
                               DASHBOARD_VERSION="${DASHBOARD_VERSION}")
    target_link_libraries(host_windows PUBLIC host_core ole32 setupapi hid shell32 advapi32 user32 winhttp comdlg32 windowscodecs
                          host_media_win)

    # Console program: run, list, set-default, volume, mute, watch, alias, startup.
    add_executable(dashboard-host Platform/Windows/main.cpp)
    target_link_libraries(dashboard-host PRIVATE host_windows)

    # Tray program (#74): no console; same service as "run".
    add_executable(dashboard-tray WIN32 Platform/Windows/tray.cpp Platform/Windows/tray.rc)
    target_link_libraries(dashboard-tray PRIVATE host_windows)
    if(NOT MSVC)
        target_link_options(dashboard-tray PRIVATE -municode)
    endif()

    foreach(target host_media_win host_windows dashboard-host dashboard-tray)
        if(MSVC)
            target_compile_options(${target} PRIVATE /W4 /WX /permissive- /utf-8)
        else()
            target_compile_options(${target} PRIVATE -Wall -Wextra -Werror)
        endif()
    endforeach()
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(test_host Tests/test_host.cpp)
    target_link_libraries(test_host PRIVATE host_core)
    target_include_directories(test_host SYSTEM PRIVATE third_party)
    if(MSVC)
        target_compile_options(test_host PRIVATE /W4 /WX /permissive- /utf-8)
    else()
        target_compile_options(test_host PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
    add_test(NAME host COMMAND test_host)
endif()
