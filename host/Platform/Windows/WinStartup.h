// Start with Windows (issue #74): a value under
// HKCU\Software\Microsoft\Windows\CurrentVersion\Run that starts the tray
// program at logon, for the current user only (no administrator rights).
// One instance at a time: "run" and the tray program share a named mutex.
#pragma once

#include <windows.h>

#include <string>

namespace stm32_audio::host::win {

// Folder of the running program, with a trailing backslash.
std::wstring programDirectory();

// The tray program next to the running program (dashboard-tray.exe).
std::wstring trayProgramPath();

// True if the Run value starts @p exe (Startup.h: startupCommandMatches).
bool startupEnabled(const std::wstring& exe);

// Add (@p enable) or remove the Run value for @p exe. True on success;
// removing a value that does not exist is a success.
bool setStartup(bool enable, const std::wstring& exe);

// Held while a program serves the controller (Local\ named mutex).
class SingleInstance {
public:
    SingleInstance();
    ~SingleInstance();
    SingleInstance(const SingleInstance&) = delete;
    SingleInstance& operator=(const SingleInstance&) = delete;

    // False if another program already holds it.
    bool first() const { return first_; }

private:
    HANDLE mutex_ = nullptr;
    bool first_ = false;
};

}  // namespace stm32_audio::host::win
