// IPreferenceFile on the Windows file system:
// %APPDATA%\stm32-desk-dashboard\<name>.txt, replaced atomically (write
// <name>.tmp, then MoveFileEx with MOVEFILE_REPLACE_EXISTING). An
// unreadable file is copied to <name>.bad before it is replaced.
// <name>: "preferences" (ADR-004), "weather" (the weather location).
#pragma once

#include <optional>
#include <string>

#include "PreferenceFile.h"

namespace stm32_audio::host::win {

class WinPreferenceFile : public IPreferenceFile {
public:
    // Directory from %APPDATA%; ok() is false if it cannot be determined.
    explicit WinPreferenceFile(const wchar_t* name = L"preferences");

    bool ok() const { return !dir_.empty(); }

    // %APPDATA%\stm32-desk-dashboard (no trailing backslash), or "" if
    // unknown. It may not exist yet. The tray program keeps host.log here.
    const std::wstring& directory() const { return dir_; }

    std::optional<std::string> read() override;
    bool write(const std::string& content) override;
    void keepUnreadable(const std::string& content) override;
    std::string location() const override;

private:
    bool writeFile(const std::wstring& path, const std::string& content) const;

    std::wstring name_;
    std::wstring dir_;
    std::wstring path_;
};

}  // namespace stm32_audio::host::win
