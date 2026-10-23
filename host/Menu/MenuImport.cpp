#include "MenuImport.h"

#include <cstdlib>
#include <set>
#include <utility>

#include "DashController.h"
#include "nlohmann/json.hpp"

namespace stm32_audio::host {

namespace {

using nlohmann::json;

constexpr std::size_t kMaxFieldBytes = 120;
constexpr std::size_t kMaxSaladItems = 16;

const char* const kPrompt =
    "These images are pages of a workplace cafeteria's monthly menu, in Turkish. They can show a meal "
    "calendar, a salad bar calendar, or both. Only weekdays are shown; each day cell has a header such as "
    "\"05 Ekim / Pazartesi\" (day and month name, no year).\n"
    "Meal calendar: five lines per day in this fixed order: main dish (main), side dish (side), soup (soup), "
    "dessert (dessert, e.g. \"MEYVE / ...\"), and what comes with it (extra, e.g. \"YOĞURT / ...\"). A long "
    "name can wrap onto a second line: join the parts.\n"
    "Salad bar calendar: about six items per day (salad_bar).\n"
    "Return every day cell you can read, from all images, each date once (combine a date's meal and salad "
    "bar if both are shown). month: 1-12, day: the day of the month.\n"
    "status: \"menu\" if food is listed; \"holiday\" if it says RESMİ TATİL or another holiday; \"none\" if it "
    "says YEMEK LİSTESİ BULUNAMADI, is empty, or is cut off so that it cannot be read.\n"
    "Text: normal Turkish sentence case (first letter capital, the rest lower case, correct Turkish letters "
    "İ i I ı Ğ ğ Ş ş Ç ç Ö ö Ü ü), the original words and abbreviations kept (e.g. \"Zyt. börülce\"), "
    "nothing translated or added. Use \"\" for a line that is not given and [] when no salad bar is shown "
    "for that day.";

std::string trim(const std::string& text)
{
    const std::size_t first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    const std::size_t last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1U);
}

// One line, single spaces, at most kMaxFieldBytes.
std::string cleanText(const std::string& text)
{
    std::string out;
    bool space = false;
    for (const char c : trim(text)) {
        const bool blank = c == ' ' || c == '\t' || c == '\r' || c == '\n';
        if (blank) {
            space = true;
            continue;
        }
        if (space && !out.empty()) {
            out += ' ';
        }
        space = false;
        out += c;
    }
    return cutUtf8(out, kMaxFieldBytes);
}

std::string stringOf(const json& object, const char* key)
{
    const auto it = object.find(key);
    return (it != object.end() && it->is_string()) ? cleanText(it->get<std::string>()) : std::string();
}

std::string message(std::uint8_t language, const char* tr, const char* en)
{
    return language == 1U ? en : tr;
}

json schema()
{
    const json text = {{"type", "string"}};
    json day = {
        {"type", "object"},
        {"properties",
         {{"month", {{"type", "integer"}}},
          {"day", {{"type", "integer"}}},
          {"status", {{"type", "string"}, {"enum", {"menu", "holiday", "none"}}}},
          {"main", text},
          {"side", text},
          {"soup", text},
          {"dessert", text},
          {"extra", text},
          {"salad_bar", {{"type", "array"}, {"items", text}}}}},
        {"required", {"month", "day", "status", "main", "side", "soup", "dessert", "extra", "salad_bar"}},
        {"additionalProperties", false},
    };
    return {
        {"type", "object"},
        {"properties", {{"days", {{"type", "array"}, {"items", day}}}}},
        {"required", {"days"}},
        {"additionalProperties", false},
    };
}

}  // namespace

int menuYearFor(int month, int importYear, int importMonth)
{
    int best = importYear;
    int bestDistance = 1000;
    for (int y = importYear - 1; y <= importYear + 1; ++y) {
        const int distance = std::abs((y * 12 + month) - (importYear * 12 + importMonth));
        if (distance < bestDistance) {
            best = y;
            bestDistance = distance;
        }
    }
    return best;
}

ClaudeConfig parseClaudeConfig(const std::optional<std::string>& fileText, const std::string& environmentKey)
{
    ClaudeConfig config;
    if (fileText) {
        std::size_t start = (fileText->compare(0, 3, "\xEF\xBB\xBF") == 0) ? 3U : 0U;
        while (start <= fileText->size()) {
            std::size_t end = fileText->find('\n', start);
            if (end == std::string::npos) {
                end = fileText->size();
            }
            const std::string line = trim(fileText->substr(start, end - start));
            start = end + 1U;
            const std::size_t eq = line.find('=');
            if (line.empty() || line[0] == '#' || eq == std::string::npos) {
                continue;
            }
            const std::string key = trim(line.substr(0, eq));
            const std::string value = trim(line.substr(eq + 1U));
            if (key == "api_key") {
                config.apiKey = value;
            } else if (key == "model" && !value.empty()) {
                config.model = value;
            }
        }
    }
    if (config.apiKey.empty()) {
        config.apiKey = trim(environmentKey);
    }
    return config;
}

std::optional<std::string> imageMediaType(const std::string& fileName)
{
    const std::size_t dot = fileName.rfind('.');
    if (dot == std::string::npos) {
        return std::nullopt;
    }
    std::string ext = fileName.substr(dot + 1U);
    for (char& c : ext) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    if (ext == "png") {
        return "image/png";
    }
    if (ext == "jpg" || ext == "jpeg") {
        return "image/jpeg";
    }
    if (ext == "webp") {
        return "image/webp";
    }
    if (ext == "gif") {
        return "image/gif";
    }
    return std::nullopt;
}

std::string base64Encode(const std::string& bytes)
{
    static const char kTable[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((bytes.size() + 2U) / 3U * 4U);
    std::size_t i = 0;
    while (i + 2U < bytes.size()) {
        const unsigned n = (static_cast<unsigned>(static_cast<unsigned char>(bytes[i])) << 16U) |
                           (static_cast<unsigned>(static_cast<unsigned char>(bytes[i + 1U])) << 8U) |
                           static_cast<unsigned>(static_cast<unsigned char>(bytes[i + 2U]));
        out += kTable[(n >> 18U) & 63U];
        out += kTable[(n >> 12U) & 63U];
        out += kTable[(n >> 6U) & 63U];
        out += kTable[n & 63U];
        i += 3U;
    }
    const std::size_t rest = bytes.size() - i;
    if (rest > 0U) {
        unsigned n = static_cast<unsigned>(static_cast<unsigned char>(bytes[i])) << 16U;
        if (rest == 2U) {
            n |= static_cast<unsigned>(static_cast<unsigned char>(bytes[i + 1U])) << 8U;
        }
        out += kTable[(n >> 18U) & 63U];
        out += kTable[(n >> 12U) & 63U];
        out += (rest == 2U) ? kTable[(n >> 6U) & 63U] : '=';
        out += '=';
    }
    return out;
}

std::string menuRequestBody(const std::vector<MenuImage>& images, const std::string& model)
{
    json content = json::array();
    for (const MenuImage& image : images) {
        content.push_back({{"type", "image"},
                           {"source",
                            {{"type", "base64"}, {"media_type", image.mediaType}, {"data", base64Encode(image.bytes)}}}});
    }
    content.push_back({{"type", "text"}, {"text", kPrompt}});
    const json body = {
        {"model", model},
        {"max_tokens", 16000},
        {"messages", json::array({{{"role", "user"}, {"content", content}}})},
        {"output_config", {{"format", {{"type", "json_schema"}, {"schema", schema()}}}}},
    };
    return body.dump(-1, ' ', false, json::error_handler_t::replace);
}

MenuAnswer parseMenuAnswer(const HttpResponse& response, int importYear, int importMonth, std::uint8_t language)
{
    MenuAnswer answer;
    const json root = json::parse(response.body, nullptr, false);
    if (response.status != 200) {
        std::string detail;
        if (root.is_object()) {
            const auto error = root.find("error");
            if (error != root.end() && error->is_object()) {
                detail = stringOf(*error, "message");
            }
        }
        if (response.status == 401) {
            answer.error = message(language, "Claude API anahtarı geçersiz (401).", "Invalid Claude API key (401).");
        } else {
            answer.error = message(language, "Claude API hatası ", "Claude API error ") +
                           std::to_string(response.status) + (detail.empty() ? "" : ": " + detail);
        }
        return answer;
    }
    if (!root.is_object()) {
        answer.error = message(language, "Claude API yanıtı okunamadı.", "Unreadable Claude API answer.");
        return answer;
    }
    const std::string stop = root.value("stop_reason", std::string());
    if (stop != "end_turn") {
        answer.error = message(language, "Claude API yanıtı tamamlanmadı (", "Claude API answer incomplete (") +
                       (stop.empty() ? "?" : stop) + ").";
        return answer;
    }
    std::string text;
    const auto content = root.find("content");
    if (content != root.end() && content->is_array()) {
        for (const json& block : *content) {
            if (block.is_object() && block.value("type", std::string()) == "text") {
                text = block.value("text", std::string());
                break;
            }
        }
    }
    const json result = json::parse(text, nullptr, false);
    const auto days = result.is_object() ? result.find("days") : result.end();
    if (!result.is_object() || days == result.end() || !days->is_array()) {
        answer.error = message(language, "Claude API yanıtında gün listesi yok.", "No days in the Claude API answer.");
        return answer;
    }

    std::set<std::pair<int, int>> seen;
    for (const json& d : *days) {
        if (!d.is_object() || !d.contains("month") || !d.contains("day") || !d["month"].is_number_integer() ||
            !d["day"].is_number_integer()) {
            continue;
        }
        const int month = d["month"].get<int>();
        const int day = d["day"].get<int>();
        const std::string status = stringOf(d, "status");
        if (month < 1 || month > 12 || status == "none") {
            continue;
        }
        const int year = menuYearFor(month, importYear, importMonth);
        if (!validDate(year, month, day) || !seen.insert({month, day}).second) {
            continue;
        }
        DatedMenuDay entry;
        entry.year = year;
        entry.month = month;
        entry.day = day;
        if (status == "holiday") {
            entry.content.holiday = true;
        } else if (status == "menu") {
            entry.content.main = stringOf(d, "main");
            entry.content.side = stringOf(d, "side");
            entry.content.soup = stringOf(d, "soup");
            entry.content.dessert = stringOf(d, "dessert");
            entry.content.extra = stringOf(d, "extra");
            const auto salad = d.find("salad_bar");
            if (salad != d.end() && salad->is_array()) {
                for (const json& item : *salad) {
                    if (item.is_string() && entry.content.saladBar.size() < kMaxSaladItems) {
                        const std::string clean = cleanText(item.get<std::string>());
                        if (!clean.empty()) {
                            entry.content.saladBar.push_back(clean);
                        }
                    }
                }
            }
            if (!entry.content.hasMeal() && entry.content.saladBar.empty() && entry.content.dessert.empty() &&
                entry.content.extra.empty()) {
                continue;
            }
        } else {
            continue;
        }
        answer.days.push_back(std::move(entry));
    }
    if (answer.days.empty()) {
        answer.error = message(language, "Görüntülerde okunabilir bir gün bulunamadı.",
                               "No readable day in the images.");
    }
    return answer;
}

MenuImportResult importMenu(const std::vector<MenuImage>& images, const ClaudeConfig& config, IHttpClient& http,
                            IMenuFiles& files, int importYear, int importMonth, std::uint8_t language)
{
    MenuImportResult result;
    if (config.apiKey.empty()) {
        result.message = message(language,
                                 "Claude API anahtarı yok: claude.txt dosyasına api_key=... yazın "
                                 "veya ANTHROPIC_API_KEY ortam değişkenini ayarlayın.",
                                 "No Claude API key: put api_key=... in claude.txt or set ANTHROPIC_API_KEY.");
        return result;
    }
    if (images.empty() || images.size() > kMaxMenuImages) {
        result.message = message(language, "1 ile 10 arasında görüntü seçin.", "Choose 1 to 10 images.");
        return result;
    }
    for (const MenuImage& image : images) {
        if (image.bytes.empty() || image.bytes.size() > kMaxMenuImageBytes) {
            result.message = image.name + message(language, ": görüntü boş ya da 5 MB'tan büyük.",
                                                  ": the image is empty or larger than 5 MB.");
            return result;
        }
    }

    const std::optional<HttpResponse> response =
        http.post(kClaudeApiUrl,
                  {{"x-api-key", config.apiKey}, {"anthropic-version", "2023-06-01"}, {"content-type", "application/json"}},
                  menuRequestBody(images, config.model));
    if (!response) {
        result.message = message(language, "Claude API'ye ulaşılamadı.", "Could not reach the Claude API.");
        return result;
    }
    const MenuAnswer answer = parseMenuAnswer(*response, importYear, importMonth, language);
    if (!answer.error.empty()) {
        result.message = answer.error;
        return result;
    }
    return saveMenuDays(answer.days, files, language);
}

MenuImportResult saveMenuDays(const std::vector<DatedMenuDay>& days, IMenuFiles& files, std::uint8_t language)
{
    MenuImportResult result;
    // Merge month by month.
    std::map<std::pair<int, int>, MenuMonth> months;
    for (const DatedMenuDay& d : days) {
        const std::pair<int, int> key{d.year, d.month};
        auto it = months.find(key);
        if (it == months.end()) {
            MenuMonth month;
            const std::optional<std::string> text = files.read(menuFileName(d.year, d.month));
            const std::optional<MenuMonth> stored = text ? parseMonthFile(*text) : std::nullopt;
            if (stored && stored->year == d.year && stored->month == d.month) {
                month = *stored;
            }
            month.year = d.year;
            month.month = d.month;
            it = months.emplace(key, std::move(month)).first;
        }
        mergeDay(it->second, d.day, d.content);
    }
    std::string summary;
    for (const auto& [key, month] : months) {
        if (!files.write(menuFileName(key.first, key.second), formatMonthFile(month))) {
            result.message = message(language, "Menü dosyası yazılamadı: ", "Could not write the menu file: ") +
                             menuFileName(key.first, key.second) + ".json";
            return result;
        }
        summary += (summary.empty() ? "" : "\n") + monthSummary(month, language);
    }
    result.ok = true;
    result.message = summary;
    return result;
}

}  // namespace stm32_audio::host
