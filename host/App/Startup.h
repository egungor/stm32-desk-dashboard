// Start with Windows and the tray program's text (issue #74): the parts
// that need no Windows API, so they are unit tested on every platform.
#pragma once

#include <cstdint>
#include <string>

namespace stm32_audio::host {

// The value stored under HKCU\...\Run: the program's path in quotes, so a
// path with spaces starts the right program.
std::string startupCommand(const std::string& exePath);

// True if @p stored (a Run value) starts the program at @p exePath:
// quotes, surrounding spaces and the case of the path are ignored (Windows
// paths are case-insensitive), arguments after the path are allowed.
bool startupCommandMatches(const std::string& stored, const std::string& exePath);

// Text of the tray program, in the language of the board's settings record
// (0 Turkish, 1 English; anything else is Turkish). UTF-8.
enum class TrayText {
    Title,             // tooltip first line and window titles
    Waiting,           // controller not connected
    Connected,         // controller connected
    StartWithWindows,  // menu, check mark when enabled
    OpenFolder,        // menu: the folder with preferences.txt and host.log
    OpenLog,           // menu
    Exit,              // menu
    AlreadyRunning,    // message box when another instance serves the board
    StartupFailed,     // message box when the Run key cannot be written
    LoadMenu,          // menu: choose menu images and import them (#20)
    MenuImages,        // file dialog filter name
    MenuImporting,     // balloon while the Claude API reads the images
    MenuImported,      // balloon title after a successful import
    MenuImportFailed,  // balloon title after a failed import
};

const char* trayText(TrayText id, std::uint8_t language);

}  // namespace stm32_audio::host
