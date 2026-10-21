#include "WinStartup.h"

#include <vector>

#include "Startup.h"
#include "WinStrings.h"

namespace stm32_audio::host::win {

namespace {

const wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
const wchar_t kValueName[] = L"STM32 Desk Dashboard";
const wchar_t kMutexName[] = L"Local\\stm32-desk-dashboard-host";

}  // namespace

std::wstring programDirectory()
{
    std::vector<wchar_t> path(MAX_PATH);
    for (;;) {
        const DWORD n = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (n == 0) {
            return L"";
        }
        if (n < path.size()) {
            std::wstring full(path.data(), n);
            const std::size_t slash = full.find_last_of(L"\\/");
            return (slash == std::wstring::npos) ? L"" : full.substr(0, slash + 1);
        }
        path.resize(path.size() * 2);  // long path: try again
    }
}

std::wstring trayProgramPath()
{
    return programDirectory() + L"dashboard-tray.exe";
}

bool startupEnabled(const std::wstring& exe)
{
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) {
        return false;
    }
    std::vector<wchar_t> value(1024);
    DWORD type = 0;
    DWORD bytes = static_cast<DWORD>(value.size() * sizeof(wchar_t));
    const LONG result = RegQueryValueExW(key, kValueName, nullptr, &type, reinterpret_cast<BYTE*>(value.data()), &bytes);
    RegCloseKey(key);
    if (result != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ)) {
        return false;
    }
    value.resize(bytes / sizeof(wchar_t));
    const std::wstring stored(value.data(), wcsnlen(value.data(), value.size()));
    return startupCommandMatches(toUtf8(stored.c_str()), toUtf8(exe.c_str()));
}

bool setStartup(bool enable, const std::wstring& exe)
{
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) !=
        ERROR_SUCCESS) {
        return false;
    }
    LONG result;
    if (enable) {
        const std::wstring command = toWide(startupCommand(toUtf8(exe.c_str())));
        result = RegSetValueExW(key, kValueName, 0, REG_SZ, reinterpret_cast<const BYTE*>(command.c_str()),
                                static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
    } else {
        result = RegDeleteValueW(key, kValueName);
        if (result == ERROR_FILE_NOT_FOUND) {
            result = ERROR_SUCCESS;
        }
    }
    RegCloseKey(key);
    return result == ERROR_SUCCESS;
}

SingleInstance::SingleInstance()
{
    mutex_ = CreateMutexW(nullptr, TRUE, kMutexName);
    first_ = (mutex_ != nullptr) && (GetLastError() != ERROR_ALREADY_EXISTS);
}

SingleInstance::~SingleInstance()
{
    if (mutex_ != nullptr) {
        if (first_) {
            ReleaseMutex(mutex_);
        }
        CloseHandle(mutex_);
    }
}

}  // namespace stm32_audio::host::win
