// Menu import (v1 step 9): menu images -> one Claude API call (Messages
// API with a structured output, https://platform.claude.com/docs/en/
// build-with-claude/structured-outputs) -> days merged into the month
// files (MenuData.h). Platform independent: HTTP and the files are
// interfaces; the caller reads the images.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "HttpClient.h"
#include "MenuData.h"

namespace stm32_audio::host {

inline constexpr const char* kClaudeApiUrl = "https://api.anthropic.com/v1/messages";
inline constexpr const char* kDefaultMenuModel = "claude-sonnet-5-5";
inline constexpr std::size_t kMaxMenuImages = 10;
inline constexpr std::size_t kMaxMenuImageBytes = 5U * 1024U * 1024U;  // API limit per image

struct MenuImage {
    std::string name;       // for messages
    std::string mediaType;  // "image/png", ...
    std::string bytes;
};

// The API key and model: %APPDATA%\stm32-desk-dashboard\claude.txt
//   api_key=sk-ant-...
//   model=claude-sonnet-5-5      (optional)
// or the ANTHROPIC_API_KEY environment variable when the file has no key.
struct ClaudeConfig {
    std::string apiKey;
    std::string model = kDefaultMenuModel;
};
ClaudeConfig parseClaudeConfig(const std::optional<std::string>& fileText, const std::string& environmentKey);

// "image/png" etc. from the file name, nullopt for other files.
std::optional<std::string> imageMediaType(const std::string& fileName);

std::string base64Encode(const std::string& bytes);

// The Messages API request body.
std::string menuRequestBody(const std::vector<MenuImage>& images, const std::string& model);

struct DatedMenuDay {
    int year = 0;
    int month = 0;
    int day = 0;
    MenuDay content;
};

struct MenuAnswer {
    std::vector<DatedMenuDay> days;  // "none" days are left out
    std::string error;               // empty on success
};

// Read the API answer. Days get the year closest to the import date
// (@p importYear / @p importMonth): the images carry no year.
MenuAnswer parseMenuAnswer(const HttpResponse& response, int importYear, int importMonth, std::uint8_t language);

// The month files, by name (menuFileName()).
class IMenuFiles {
public:
    virtual ~IMenuFiles() = default;
    virtual std::optional<std::string> read(const std::string& name) = 0;
    virtual bool write(const std::string& name, const std::string& text) = 0;
};

struct MenuImportResult {
    bool ok = false;
    std::string message;  // summary per month, or the error
};

// The whole import; blocks for the API call (up to minutes).
MenuImportResult importMenu(const std::vector<MenuImage>& images, const ClaudeConfig& config, IHttpClient& http,
                            IMenuFiles& files, int importYear, int importMonth, std::uint8_t language);

}  // namespace stm32_audio::host
