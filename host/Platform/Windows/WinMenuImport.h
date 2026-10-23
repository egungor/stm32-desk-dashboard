// The menu import on Windows (#20), shared by "dashboard-host menu import"
// and the tray's "Menü görüntülerini yükle…": reads the image files and
// the Claude settings, calls importMenu() (MenuImport.h) with WinHTTP and
// the month files in %APPDATA%\stm32-desk-dashboard. Saved portal pages
// (.html) go to importMenuPages() (MenuHtmlImport.h) instead, without the
// Claude API.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "MenuHtmlImport.h"
#include "MenuImport.h"
#include "WinPreferenceFile.h"
#include "WinStrings.h"

namespace stm32_audio::host::win {

// IMenuFiles on %APPDATA%\stm32-desk-dashboard\<name>.json.
class WinMenuFiles : public IMenuFiles {
public:
    std::optional<std::string> read(const std::string& name) override;
    bool write(const std::string& name, const std::string& text) override;
};

// True if every path is a saved page (.html, .htm): no Claude API call.
bool areMenuPages(const std::vector<std::wstring>& paths);

// Images: blocks for the API call (up to minutes). Pages: at once.
// Images and pages together are refused.
MenuImportResult importMenuFiles(const std::vector<std::wstring>& paths, std::uint8_t language);

}  // namespace stm32_audio::host::win
