#include "WinPreferenceFile.h"

#include <windows.h>

#include <vector>

#include "WinStrings.h"

namespace stm32_audio::host::win {

namespace {

constexpr DWORD kMaxFileBytes = 1024U * 1024U;  // far above any real file

}  // namespace

WinPreferenceFile::WinPreferenceFile(const wchar_t* name) : name_(name)
{
    wchar_t buffer[MAX_PATH];
    const DWORD n = GetEnvironmentVariableW(L"APPDATA", buffer, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) {
        return;
    }
    dir_ = std::wstring(buffer) + L"\\stm32-desk-dashboard";
    path_ = dir_ + L"\\" + name_ + L".txt";
}

std::optional<std::string> WinPreferenceFile::read()
{
    if (!ok()) {
        return std::nullopt;
    }
    const HANDLE file = CreateFileW(path_.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr,
                                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return std::nullopt;
    }
    std::optional<std::string> result;
    LARGE_INTEGER size{};
    if (GetFileSizeEx(file, &size) && size.QuadPart >= 0 && size.QuadPart <= kMaxFileBytes) {
        std::vector<char> data(static_cast<std::size_t>(size.QuadPart));
        DWORD got = 0;
        if (data.empty() ||
            (ReadFile(file, data.data(), static_cast<DWORD>(data.size()), &got, nullptr) && got == data.size())) {
            result = std::string(data.begin(), data.end());
        }
    }
    CloseHandle(file);
    return result;
}

bool WinPreferenceFile::writeFile(const std::wstring& path, const std::string& content) const
{
    const HANDLE file =
        CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    DWORD written = 0;
    const bool ok = WriteFile(file, content.data(), static_cast<DWORD>(content.size()), &written, nullptr) &&
                    written == content.size() && FlushFileBuffers(file);
    CloseHandle(file);
    return ok;
}

bool WinPreferenceFile::write(const std::string& content)
{
    if (!ok()) {
        return false;
    }
    if (!CreateDirectoryW(dir_.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) {
        return false;
    }
    const std::wstring tmp = dir_ + L"\\" + name_ + L".tmp";
    if (!writeFile(tmp, content)) {
        DeleteFileW(tmp.c_str());
        return false;
    }
    if (!MoveFileExW(tmp.c_str(), path_.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(tmp.c_str());
        return false;
    }
    return true;
}

void WinPreferenceFile::keepUnreadable(const std::string& content)
{
    if (ok()) {
        (void)writeFile(dir_ + L"\\" + name_ + L".bad", content);
    }
}

std::string WinPreferenceFile::location() const
{
    return ok() ? toUtf8(path_.c_str()) : std::string("(no %APPDATA%)");
}

}  // namespace stm32_audio::host::win
