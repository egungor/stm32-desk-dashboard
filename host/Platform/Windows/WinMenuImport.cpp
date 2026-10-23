#include "WinMenuImport.h"

#include <windows.h>

#include "WinHttpClient.h"

namespace stm32_audio::host::win {

namespace {

std::optional<std::string> readFile(const std::wstring& path, std::size_t maxBytes)
{
    const HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                    FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return std::nullopt;
    }
    std::optional<std::string> result;
    LARGE_INTEGER size{};
    if (GetFileSizeEx(file, &size) && size.QuadPart >= 0 &&
        static_cast<unsigned long long>(size.QuadPart) <= maxBytes) {
        std::string data(static_cast<std::size_t>(size.QuadPart), '\0');
        DWORD got = 0;
        if (data.empty() ||
            (ReadFile(file, data.data(), static_cast<DWORD>(data.size()), &got, nullptr) && got == data.size())) {
            result = std::move(data);
        }
    }
    CloseHandle(file);
    return result;
}

std::string environmentKey()
{
    wchar_t buffer[512];
    const DWORD n = GetEnvironmentVariableW(L"ANTHROPIC_API_KEY", buffer, 512);
    return (n == 0 || n >= 512) ? std::string() : toUtf8(buffer);
}

}  // namespace

std::optional<std::string> WinMenuFiles::read(const std::string& name)
{
    WinPreferenceFile file(toWide(name).c_str(), L"json");
    return file.read();
}

bool WinMenuFiles::write(const std::string& name, const std::string& text)
{
    WinPreferenceFile file(toWide(name).c_str(), L"json");
    return file.write(text);
}

bool areMenuPages(const std::vector<std::wstring>& paths)
{
    if (paths.empty()) {
        return false;
    }
    for (const std::wstring& path : paths) {
        if (!isMenuPageName(toUtf8(path.c_str()))) {
            return false;
        }
    }
    return true;
}

MenuImportResult importMenuFiles(const std::vector<std::wstring>& paths, std::uint8_t language)
{
    SYSTEMTIME t{};
    GetLocalTime(&t);
    if (areMenuPages(paths)) {
        std::vector<MenuPageFile> pages;
        for (const std::wstring& path : paths) {
            MenuPageFile page;
            page.name = toUtf8(path.c_str());
            std::optional<std::string> html = readFile(path, kMaxMenuPageBytes);
            if (!html) {
                MenuImportResult r;
                r.message = page.name + (language == 1U ? ": cannot be read or larger than 4 MB."
                                                        : ": okunamadı ya da 4 MB'tan büyük.");
                return r;
            }
            page.html = std::move(*html);
            pages.push_back(std::move(page));
        }
        WinMenuFiles files;
        return importMenuPages(pages, files, t.wYear, t.wMonth, language);
    }

    std::vector<MenuImage> images;
    for (const std::wstring& path : paths) {
        MenuImage image;
        image.name = toUtf8(path.c_str());
        const std::optional<std::string> type = imageMediaType(image.name);
        if (!type) {
            MenuImportResult r;
            r.message = image.name + (isMenuPageName(image.name)
                                          ? (language == 1U ? ": load the pages without images."
                                                            : ": sayfaları görüntüsüz yükleyin.")
                                          : (language == 1U ? ": not a PNG, JPEG, WebP or GIF image."
                                                            : ": PNG, JPEG, WebP ya da GIF görüntüsü değil."));
            return r;
        }
        image.mediaType = *type;
        std::optional<std::string> bytes = readFile(path, kMaxMenuImageBytes + 1U);
        if (!bytes) {
            MenuImportResult r;
            r.message = image.name + (language == 1U ? ": cannot be read or larger than 5 MB."
                                                     : ": okunamadı ya da 5 MB'tan büyük.");
            return r;
        }
        image.bytes = std::move(*bytes);
        images.push_back(std::move(image));
    }

    WinPreferenceFile claudeFile(L"claude");
    const ClaudeConfig config = parseClaudeConfig(claudeFile.read(), environmentKey());
    WinHttpClient http;
    WinMenuFiles files;
    return importMenu(images, config, http, files, t.wYear, t.wMonth, language);
}

}  // namespace stm32_audio::host::win
