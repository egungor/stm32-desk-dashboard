#include "MenuHtmlImport.h"

#include <array>
#include <cstdlib>
#include <set>
#include <utility>

#include "DashController.h"
#include "MenuLists.h"

namespace stm32_audio::host {

namespace {

constexpr std::size_t kMaxFieldBytes = 120;  // as the image import
constexpr std::size_t kMaxSaladItems = 16;
constexpr const char* kColumnClass = "portal-food-menu-table-column";

// Two-byte UTF-8 lower case letters of Turkish and their upper case.
constexpr std::array<std::pair<const char*, const char*>, 8> kUpper = {{
    {"\xC4\xB1", "I"},          // ı
    {"\xC3\xA7", "\xC3\x87"},   // ç Ç
    {"\xC4\x9F", "\xC4\x9E"},   // ğ Ğ
    {"\xC3\xB6", "\xC3\x96"},   // ö Ö
    {"\xC5\x9F", "\xC5\x9E"},   // ş Ş
    {"\xC3\xBC", "\xC3\x9C"},   // ü Ü
    {"\xC3\xA2", "\xC3\x82"},   // â Â
    {"\xC3\xAE", "\xC3\x8E"},   // î Î
}};

std::string message(std::uint8_t language, const char* tr, const char* en)
{
    return language == 1U ? en : tr;
}

void appendUtf8(std::string& out, unsigned long cp)
{
    if (cp == 0U || cp > 0x10FFFFUL || (cp >= 0xD800UL && cp <= 0xDFFFUL)) {
        return;
    }
    if (cp < 0x80UL) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800UL) {
        out += static_cast<char>(0xC0UL | (cp >> 6U));
        out += static_cast<char>(0x80UL | (cp & 0x3FUL));
    } else if (cp < 0x10000UL) {
        out += static_cast<char>(0xE0UL | (cp >> 12U));
        out += static_cast<char>(0x80UL | ((cp >> 6U) & 0x3FUL));
        out += static_cast<char>(0x80UL | (cp & 0x3FUL));
    } else {
        out += static_cast<char>(0xF0UL | (cp >> 18U));
        out += static_cast<char>(0x80UL | ((cp >> 12U) & 0x3FUL));
        out += static_cast<char>(0x80UL | ((cp >> 6U) & 0x3FUL));
        out += static_cast<char>(0x80UL | (cp & 0x3FUL));
    }
}

// The text of an HTML fragment: tags dropped (<br> is a space), the
// common character references decoded.
std::string textOf(std::string_view html)
{
    std::string out;
    std::size_t i = 0;
    while (i < html.size()) {
        const char c = html[i];
        if (c == '<') {
            const std::size_t end = html.find('>', i);
            if (end == std::string_view::npos) {
                break;
            }
            out += ' ';
            i = end + 1U;
            continue;
        }
        if (c == '&') {
            const std::size_t semi = html.find(';', i);
            if (semi != std::string_view::npos && semi - i <= 10U) {
                const std::string_view name = html.substr(i + 1U, semi - i - 1U);
                bool known = true;
                if (name == "amp") {
                    out += '&';
                } else if (name == "lt") {
                    out += '<';
                } else if (name == "gt") {
                    out += '>';
                } else if (name == "quot") {
                    out += '"';
                } else if (name == "apos") {
                    out += '\'';
                } else if (name == "nbsp") {
                    out += ' ';
                } else if (name.size() >= 2U && name[0] == '#') {
                    const bool hex = name[1] == 'x' || name[1] == 'X';
                    const std::string digits(name.substr(hex ? 2U : 1U));
                    char* rest = nullptr;
                    const unsigned long cp = std::strtoul(digits.c_str(), &rest, hex ? 16 : 10);
                    known = !digits.empty() && rest != nullptr && *rest == '\0';
                    if (known) {
                        appendUtf8(out, cp);
                    }
                } else {
                    known = false;
                }
                if (known) {
                    i = semi + 1U;
                    continue;
                }
            }
        }
        out += c;
        ++i;
    }
    return out;
}

// The class attribute of the tag text "<div class=... >", or "".
std::string classOf(std::string_view tag)
{
    const std::size_t at = tag.find("class=");
    if (at == std::string_view::npos || at + 6U >= tag.size()) {
        return {};
    }
    const char quote = tag[at + 6U];
    if (quote != '"' && quote != '\'') {
        return {};
    }
    const std::size_t end = tag.find(quote, at + 7U);
    return end == std::string_view::npos ? std::string() : std::string(tag.substr(at + 7U, end - at - 7U));
}

bool hasClass(const std::string& classes, std::string_view name)
{
    std::size_t pos = 0;
    while (pos < classes.size()) {
        std::size_t end = classes.find(' ', pos);
        if (end == std::string::npos) {
            end = classes.size();
        }
        if (std::string_view(classes).substr(pos, end - pos) == name) {
            return true;
        }
        pos = end + 1U;
    }
    return false;
}

// True if @p html has the tag @p name ("div", "/div") at @p pos.
bool tagAt(std::string_view html, std::size_t pos, std::string_view name)
{
    if (pos >= html.size() || html[pos] != '<' || html.substr(pos + 1U, name.size()) != name) {
        return false;
    }
    const std::size_t after = pos + 1U + name.size();
    return after < html.size() && (html[after] == '>' || html[after] == ' ' || html[after] == '\t' ||
                                   html[after] == '\n' || html[after] == '\r' || html[after] == '/');
}

// The labels of the active tabs: <button class="active">Öğle</button>.
std::vector<std::string> activeTabs(std::string_view html)
{
    std::vector<std::string> tabs;
    std::size_t pos = 0;
    while ((pos = html.find("<button", pos)) != std::string_view::npos) {
        const std::size_t end = html.find('>', pos);
        if (end == std::string_view::npos) {
            break;
        }
        const std::size_t close = html.find("</button>", end);
        if (close == std::string_view::npos) {
            break;
        }
        if (hasClass(classOf(html.substr(pos, end - pos)), "active")) {
            tabs.push_back(turkishLower(textOf(html.substr(end + 1U, close - end - 1U))));
        }
        pos = close;
    }
    return tabs;
}

struct Column {
    std::string header;  // "05 Ekim Pazartesi"
    std::vector<std::string> items;
    bool empty = false;  // "Yemek listesi bulunamadı"
};

// The day columns: the <h1> and the text of each child <div>.
std::vector<Column> columnsOf(std::string_view html)
{
    std::vector<Column> columns;
    std::size_t pos = 0;
    while ((pos = html.find("<div", pos)) != std::string_view::npos) {
        const std::size_t tagEnd = html.find('>', pos);
        if (tagEnd == std::string_view::npos) {
            break;
        }
        if (!tagAt(html, pos, "div") || !hasClass(classOf(html.substr(pos, tagEnd - pos)), kColumnClass)) {
            pos = tagEnd;
            continue;
        }
        Column column;
        int depth = 1;
        std::size_t itemStart = 0;
        std::size_t i = tagEnd + 1U;
        while (depth > 0 && (i = html.find('<', i)) != std::string_view::npos) {
            const std::size_t end = html.find('>', i);
            if (end == std::string_view::npos) {
                i = html.size();
                break;
            }
            if (tagAt(html, i, "div")) {
                ++depth;
                if (depth == 2) {
                    itemStart = end + 1U;
                    column.empty = column.empty || hasClass(classOf(html.substr(i, end - i)), "empty");
                }
            } else if (tagAt(html, i, "/div")) {
                if (depth == 2) {
                    column.items.push_back(textOf(html.substr(itemStart, i - itemStart)));
                }
                --depth;
            } else if (depth == 1 && tagAt(html, i, "h1")) {
                const std::size_t close = html.find("</h1>", end);
                if (close != std::string_view::npos) {
                    column.header = textOf(html.substr(end + 1U, close - end - 1U));
                    i = close;
                    continue;
                }
            }
            i = end + 1U;
        }
        columns.push_back(std::move(column));
        if (i == std::string_view::npos) {
            break;
        }
        pos = i;
    }
    return columns;
}

// "05 Ekim Pazartesi" -> 10, 5; false for another header.
bool parseHeader(const std::string& header, int& month, int& day)
{
    const std::string words = turkishLower(header);
    const std::size_t space = words.find(' ');
    if (space == std::string::npos || space == 0U || space > 2U) {
        return false;
    }
    day = 0;
    for (std::size_t i = 0; i < space; ++i) {
        if (words[i] < '0' || words[i] > '9') {
            return false;
        }
        day = day * 10 + (words[i] - '0');
    }
    std::size_t end = words.find(' ', space + 1U);
    if (end == std::string::npos) {
        end = words.size();
    }
    const std::string name = words.substr(space + 1U, end - space - 1U);
    for (int m = 1; m <= 12; ++m) {
        if (name == turkishLower(monthName(m, 0))) {
            month = m;
            return true;
        }
    }
    return false;
}

}  // namespace

bool isMenuPageName(const std::string& fileName)
{
    const std::size_t dot = fileName.rfind('.');
    if (dot == std::string::npos) {
        return false;
    }
    std::string ext = fileName.substr(dot + 1U);
    for (char& c : ext) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return ext == "html" || ext == "htm";
}

std::string menuSentenceCase(std::string_view text)
{
    std::string spaced;
    for (const char c : text) {
        if (c == '/') {
            spaced += " / ";
        } else {
            spaced += c;
        }
    }
    std::string out = turkishLower(spaced);
    if (out.empty()) {
        return out;
    }
    if (out[0] == 'i') {
        out.replace(0, 1, "\xC4\xB0");  // İ
    } else if (out[0] >= 'a' && out[0] <= 'z') {
        out[0] = static_cast<char>(out[0] - 'a' + 'A');
    } else if (out.size() >= 2U) {
        for (const auto& [lower, upper] : kUpper) {
            if (out[0] == lower[0] && out[1] == lower[1]) {
                out.replace(0, 2, upper);
                break;
            }
        }
    }
    return cutUtf8(out, kMaxFieldBytes);
}

MenuPage parseMenuPage(const std::string& html, int importYear, int importMonth, std::uint8_t language)
{
    MenuPage page;
    const std::vector<Column> columns = columnsOf(html);
    if (columns.empty()) {
        page.error = message(language, "Yemek menüsü sayfası değil.", "Not a menu page.");
        return page;
    }
    bool meal = false;
    bool salad = false;
    std::string other;
    for (const std::string& tab : activeTabs(html)) {
        if (tab == "öğle") {
            meal = true;
        } else if (tab == "salatabar" || tab == "salata bar") {
            salad = true;
        } else if (tab == "akşam" || tab == "kreş" || tab == "vejeteryan" || tab == "vejetaryen" || tab == "diyet") {
            other = tab;
        }
    }
    if (meal == salad) {  // neither, or both
        page.error = other.empty()
                         ? message(language, "Sayfada Öğle ya da Salatabar sekmesi seçili değil.",
                                   "Neither the Öğle nor the Salatabar tab is selected on the page.")
                         : message(language, "Sayfada \"", "The page shows the \"") + menuSentenceCase(other) +
                               message(language, "\" menüsü var; Öğle ya da Salatabar sekmesinde kaydedin.",
                                       "\" menu; save it on the Öğle or Salatabar tab.");
        return page;
    }
    page.kind = salad ? MenuPageKind::SaladBar : MenuPageKind::Meal;

    std::set<std::pair<int, int>> seen;
    for (const Column& column : columns) {
        int month = 0;
        int day = 0;
        if (column.empty || !parseHeader(column.header, month, day)) {
            continue;
        }
        const int year = menuYearFor(month, importYear, importMonth);
        if (!validDate(year, month, day) || !seen.insert({month, day}).second) {
            continue;
        }
        std::vector<std::string> items;
        bool holiday = false;
        for (const std::string& item : column.items) {
            const std::string clean = menuSentenceCase(item);
            if (!clean.empty()) {
                holiday = holiday || turkishLower(clean).find("tatil") != std::string::npos;
                items.push_back(clean);
            }
        }
        if (items.empty()) {
            continue;
        }
        DatedMenuDay entry;
        entry.year = year;
        entry.month = month;
        entry.day = day;
        if (holiday) {
            entry.content.holiday = true;
        } else if (page.kind == MenuPageKind::SaladBar) {
            for (std::string& item : items) {
                if (entry.content.saladBar.size() < kMaxSaladItems) {
                    entry.content.saladBar.push_back(std::move(item));
                }
            }
        } else {
            std::string* const fields[] = {&entry.content.main, &entry.content.side, &entry.content.soup,
                                           &entry.content.dessert, &entry.content.extra};
            for (std::size_t i = 0; i < items.size() && i < 5U; ++i) {
                *fields[i] = std::move(items[i]);
            }
        }
        page.days.push_back(std::move(entry));
    }
    if (page.days.empty()) {
        page.error = message(language, "Sayfada menüsü olan bir gün yok.", "No day with a menu on the page.");
    }
    return page;
}

MenuImportResult importMenuPages(const std::vector<MenuPageFile>& pages, IMenuFiles& files, int importYear,
                                 int importMonth, std::uint8_t language)
{
    MenuImportResult result;
    if (pages.empty()) {
        result.message = message(language, "Bir menü sayfası seçin.", "Choose a menu page.");
        return result;
    }
    std::vector<DatedMenuDay> days;
    for (const MenuPageFile& file : pages) {
        if (file.html.size() > kMaxMenuPageBytes) {
            result.message = file.name + message(language, ": 4 MB'tan büyük.", ": larger than 4 MB.");
            return result;
        }
        MenuPage page = parseMenuPage(file.html, importYear, importMonth, language);
        if (!page.error.empty()) {
            result.message = file.name + ": " + page.error;
            return result;
        }
        for (DatedMenuDay& d : page.days) {
            days.push_back(std::move(d));
        }
    }
    return saveMenuDays(days, files, language);
}

}  // namespace stm32_audio::host
