#include "Startup.h"

#include <cctype>

namespace stm32_audio::host {

namespace {

std::string trimmed(const std::string& s)
{
    std::size_t begin = 0;
    std::size_t end = s.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(s[begin])) != 0) {
        ++begin;
    }
    while (end > begin && std::isspace(static_cast<unsigned char>(s[end - 1])) != 0) {
        --end;
    }
    return s.substr(begin, end - begin);
}

// ASCII case folding is enough: non-ASCII UTF-8 bytes compare as they are.
bool sameIgnoringCase(const std::string& a, const std::string& b)
{
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        const auto ca = static_cast<unsigned char>(a[i]);
        const auto cb = static_cast<unsigned char>(b[i]);
        if (std::tolower(ca) != std::tolower(cb)) {
            return false;
        }
    }
    return true;
}

}  // namespace

std::string startupCommand(const std::string& exePath)
{
    return "\"" + exePath + "\"";
}

bool startupCommandMatches(const std::string& stored, const std::string& exePath)
{
    const std::string value = trimmed(stored);
    const std::string exe = trimmed(exePath);

    if (value.empty() || exe.empty()) {
        return false;
    }
    if (value[0] == '"') {
        const std::size_t close = value.find('"', 1);
        return (close != std::string::npos) && sameIgnoringCase(value.substr(1, close - 1), exe);
    }
    // Unquoted: the whole value, or the part before the first space (a path
    // without spaces followed by arguments).
    if (sameIgnoringCase(value, exe)) {
        return true;
    }
    const std::size_t space = value.find(' ');
    return (space != std::string::npos) && sameIgnoringCase(value.substr(0, space), exe);
}

const char* trayText(TrayText id, std::uint8_t language)
{
    const bool english = (language == 1);

    switch (id) {
    case TrayText::Title:
        return english ? "STM32 Desk Dashboard" : "STM32 Masa Paneli";
    case TrayText::Waiting:
        return english ? "Waiting for the controller" : "Kumanda bekleniyor";
    case TrayText::Connected:
        return english ? "Controller connected" : "Kumanda ba\xC4\x9Fl\xC4\xB1";
    case TrayText::StartWithWindows:
        return english ? "Start with Windows" : "Windows ile ba\xC5\x9Flat";
    case TrayText::OpenFolder:
        return english ? "Open the settings folder" : "Ayarlar klas\xC3\xB6r\xC3\xBCn\xC3\xBC a\xC3\xA7";
    case TrayText::OpenLog:
        return english ? "Open the log" : "G\xC3\xBCnl\xC3\xBC\xC4\x9F\xC3\xBC a\xC3\xA7";
    case TrayText::Exit:
        return english ? "Exit" : "\xC3\x87\xC4\xB1k\xC4\xB1\xC5\x9F";
    case TrayText::AlreadyRunning:
        return english ? "The controller is already served by another program (dashboard-tray or "
                         "dashboard-host run)."
                       : "Kumanda zaten ba\xC5\x9Fka bir program taraf\xC4\xB1ndan kullan\xC4\xB1l\xC4\xB1yor "
                         "(dashboard-tray ya da dashboard-host run).";
    case TrayText::StartupFailed:
        return english ? "The start with Windows setting could not be changed."
                       : "Windows ile ba\xC5\x9Flatma ayar\xC4\xB1 de\xC4\x9Fi\xC5\x9Ftirilemedi.";
    case TrayText::LoadMenu:
        return english ? "Load menu images\xE2\x80\xA6" : "Men\xC3\xBC g\xC3\xB6r\xC3\xBCnt\xC3\xBClerini y\xC3\xBCkle\xE2\x80\xA6";
    case TrayText::MenuImages:
        return english ? "Images" : "G\xC3\xB6r\xC3\xBCnt\xC3\xBCler";
    case TrayText::MenuImporting:
        return english ? "Claude is reading the menu images\xE2\x80\xA6"
                       : "Claude men\xC3\xBC g\xC3\xB6r\xC3\xBCnt\xC3\xBClerini okuyor\xE2\x80\xA6";
    case TrayText::MenuImported:
        return english ? "Menu loaded" : "Men\xC3\xBC y\xC3\xBCklendi";
    case TrayText::MenuImportFailed:
        return english ? "Menu not loaded" : "Men\xC3\xBC y\xC3\xBCklenemedi";
    case TrayText::CopyHookSettings:
        return english ? "Copy the Claude Code hook setting"
                       : "Claude Code hook ayar\xC4\xB1n\xC4\xB1 kopyala";
    case TrayText::HookSettingsCopied:
        return english ? "Copied. Merge it into %USERPROFILE%\\.claude\\settings.json of this PC."
                       : "Kopyaland\xC4\xB1. Bu bilgisayar\xC4\xB1n %USERPROFILE%\\.claude\\settings.json "
                         "dosyas\xC4\xB1na ekleyin.";
    case TrayText::ChoosePicture:
        return english ? "Choose picture\xE2\x80\xA6" : "Resim se\xC3\xA7\xE2\x80\xA6";
    case TrayText::PictureFiles:
        return english ? "Pictures" : "Resimler";
    case TrayText::EditFoodLists:
        return english ? "Edit food lists\xE2\x80\xA6" : "Yemek listelerini d\xC3\xBCzenle\xE2\x80\xA6";
    case TrayText::Settings:
        return english ? "Settings\xE2\x80\xA6" : "Ayarlar\xE2\x80\xA6";
    case TrayText::SettingsTitle:
        return english ? "STM32 Desk Dashboard settings" : "STM32 Masa Paneli ayarlar\xC4\xB1";
    case TrayText::WebViewMissing:
        return english ? "The settings window needs the Microsoft Edge WebView2 Runtime. Install it from "
                         "https://go.microsoft.com/fwlink/p/?LinkId=2124703 and try again."
                       : "Ayarlar penceresi i\xC3\xA7in Microsoft Edge WebView2 Runtime gerekiyor. "
                         "https://go.microsoft.com/fwlink/p/?LinkId=2124703 adresinden kurup tekrar deneyin.";
    case TrayText::SettingsSaved:
        return english ? "Saved." : "Kaydedildi.";
    case TrayText::SettingsNotSaved:
        return english ? "Could not save; see the log." : "Kaydedilemedi; g\xC3\xBCnl\xC3\xBC\xC4\x9F\xC3\xBC a\xC3\xA7\xC4\xB1n.";
    }
    return "";
}

}  // namespace stm32_audio::host
