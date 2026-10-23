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
    Picture/PictureLibrary.cpp
    Picture/PictureService.cpp
    Notify/ToastText.cpp
    Menu/MenuData.cpp
    Menu/MenuHtmlImport.cpp
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

    # The settings window (ADR-011): WebView2, with the SDK's static loader,
    # an MSVC library; other compilers (MinGW) build the tray without it.
    # The NuGet package is fetched once into the build folder and checked.
    if(MSVC)
        set(WEBVIEW2_VERSION 1.0.4129.50)
        set(WEBVIEW2_SHA256 d3934f482d484b89fb4825df720c710664e1143a1e90f7b3a60794ef33f473d2)
        set(WEBVIEW2_DIR ${CMAKE_BINARY_DIR}/_deps/webview2-${WEBVIEW2_VERSION})
        if(NOT EXISTS ${WEBVIEW2_DIR}/build/native/include/WebView2.h)
            set(webview2_zip ${CMAKE_BINARY_DIR}/_deps/webview2-${WEBVIEW2_VERSION}.zip)
            file(DOWNLOAD https://www.nuget.org/api/v2/package/Microsoft.Web.WebView2/${WEBVIEW2_VERSION} ${webview2_zip}
                 EXPECTED_HASH SHA256=${WEBVIEW2_SHA256} TLS_VERIFY ON)
            file(ARCHIVE_EXTRACT INPUT ${webview2_zip} DESTINATION ${WEBVIEW2_DIR})
        endif()
        if(CMAKE_GENERATOR_PLATFORM MATCHES "ARM64" OR CMAKE_CXX_COMPILER_ARCHITECTURE_ID MATCHES "ARM64")
            set(webview2_arch arm64)
        elseif(CMAKE_SIZEOF_VOID_P EQUAL 8)
            set(webview2_arch x64)
        else()
            set(webview2_arch x86)
        endif()
        add_library(webview2_loader STATIC IMPORTED)
        set_target_properties(webview2_loader PROPERTIES
            IMPORTED_LOCATION ${WEBVIEW2_DIR}/build/native/${webview2_arch}/WebView2LoaderStatic.lib
            INTERFACE_INCLUDE_DIRECTORIES ${WEBVIEW2_DIR}/build/native/include)
        target_sources(dashboard-tray PRIVATE Platform/Windows/WinSettingsWindow.cpp)
        target_link_libraries(dashboard-tray PRIVATE webview2_loader version shlwapi)
        target_compile_definitions(dashboard-tray PRIVATE DASHBOARD_SETTINGS_WINDOW=1)
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
