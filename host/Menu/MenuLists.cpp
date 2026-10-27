#include "MenuLists.h"

#include <array>
#include <utility>

namespace stm32_audio::host {

namespace {

// Two-byte UTF-8 upper case letters of Turkish and their lower case.
constexpr std::array<std::pair<const char*, const char*>, 7> kPairs = {{
    {"\xC4\xB0", "i"},          // İ
    {"\xC3\x87", "\xC3\xA7"},   // Ç ç
    {"\xC4\x9E", "\xC4\x9F"},   // Ğ ğ
    {"\xC3\x96", "\xC3\xB6"},   // Ö ö
    {"\xC5\x9E", "\xC5\x9F"},   // Ş ş
    {"\xC3\x9C", "\xC3\xBC"},   // Ü ü
    {"\xC3\x82", "\xC3\xA2"},   // Â â
}};

std::string trim(std::string_view s)
{
    std::size_t b = 0;
    std::size_t e = s.size();
    while (b < e && (s[b] == ' ' || s[b] == '\t' || s[b] == '\r')) {
        ++b;
    }
    while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r')) {
        --e;
    }
    return std::string(s.substr(b, e - b));
}

bool contains(const std::string& haystack, const std::string& needle)
{
    return !needle.empty() && haystack.find(needle) != std::string::npos;
}

}  // namespace

// The comment block of the file: in a new file, and instead of the block
// of before liked dishes (until v0.9.0-beta.12, kOldComments).
#define MENU_LISTS_COMMENTS                                                                  \
    "# Yemek listeleri (STM32 Desk Dashboard)\n"                                           \
    "# Her satıra bir yemek adı yazın. Adın geçtiği her yemek eşleşir:\n"                 \
    "# \"fasulye\" hem \"Kuru fasulye\" hem \"Taze fasulye\" ile eşleşir.\n"             \
    "# Büyük / küçük harf fark etmez. # ile başlayan satırlar açıklamadır.\n"             \
    "#\n"                                                                                 \
    "# Yemek kartının rengini ana yemek belirler:\n"                                      \
    "# [istemiyorum]: hiç istemediğim yemekler -> kart turuncu\n"                         \
    "# [sevmiyorum]:  yediğim ama sevmediğim yemekler -> kart sarı\n"                     \
    "# [seviyorum]:   sevdiğim yemekler -> kart yeşil, altın çerçeveli, çatal bıçak altın\n" \
    "# Diğer yemekler (yan yemek, çorba, tatlı) karta dokununca açılan ayrıntıda\n"       \
    "# listenin renginde görünür.\n"                                                      \
    "#\n"                                                                                 \
    "# Uyarı sabah 8'de ve listeler kaydedilince gelir: ana yemek bir listedeyse\n"       \
    "# ya da başka bir yemek [istemiyorum] / [sevmiyorum] listesindeyse. Yalnızca\n"      \
    "# sevdiğim bir yan yemek, çorba ya da tatlı için uyarı gelmez.\n"

namespace {

const char* const kOldComments =
    "# Yemek listeleri (STM32 Desk Dashboard)\n"
    "# Her satıra bir yemek adı yazın. Adın geçtiği her yemek eşleşir:\n"
    "# \"fasulye\" hem \"Kuru fasulye\" hem \"Taze fasulye\" ile eşleşir.\n"
    "# Büyük / küçük harf fark etmez. # ile başlayan satırlar açıklamadır.\n"
    "#\n"
    "# [istemiyorum]: hiç istemediğim yemekler -> yemek kartı turuncu, sabah 8'de uyarı\n"
    "# [sevmiyorum]:  yediğim ama sevmediğim yemekler -> yemek kartı sarı, sabah 8'de uyarı\n";

}  // namespace

const char* const kMenuListsTemplate =
    MENU_LISTS_COMMENTS
    "\n"
    "[istemiyorum]\n"
    "\n"
    "[sevmiyorum]\n"
    "\n"
    "[seviyorum]\n";

std::string turkishLower(std::string_view text)
{
    std::string lowered;
    lowered.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c >= 'A' && c <= 'Z') {
            lowered += (c == 'I') ? std::string("\xC4\xB1") : std::string(1, static_cast<char>(c - 'A' + 'a'));
            continue;
        }
        bool replaced = false;
        if (i + 1U < text.size()) {
            for (const auto& [upper, lower] : kPairs) {
                if (text[i] == upper[0] && text[i + 1U] == upper[1]) {
                    lowered += lower;
                    ++i;
                    replaced = true;
                    break;
                }
            }
        }
        if (!replaced) {
            lowered += c;
        }
    }

    std::string folded;
    bool space = false;
    for (char c : lowered) {
        const bool ws = c == ' ' || c == '\t' || c == '\n' || c == '\r';
        if (ws) {
            space = !folded.empty();
            continue;
        }
        if (space) {
            folded += ' ';
            space = false;
        }
        folded += c;
    }
    return folded;
}

MenuLists parseMenuLists(std::string_view text)
{
    MenuLists lists;
    std::vector<std::string>* section = nullptr;
    std::size_t pos = 0;
    while (pos <= text.size()) {
        std::size_t end = text.find('\n', pos);
        if (end == std::string_view::npos) {
            end = text.size();
        }
        std::string line = trim(text.substr(pos, end - pos));
        pos = end + 1U;
        if (line.size() >= 3U && line.compare(0, 3, "\xEF\xBB\xBF") == 0) {
            line = trim(line.substr(3));  // UTF-8 byte order mark (Notepad)
        }
        if (line.empty() || line[0] == '#') {
            continue;
        }
        if (line.front() == '[' && line.back() == ']') {
            const std::string name = turkishLower(line.substr(1, line.size() - 2U));
            if (name == "istemiyorum" || name == "never") {
                section = &lists.never;
            } else if (name == "sevmiyorum" || name == "dislike") {
                section = &lists.dislike;
            } else if (name == "seviyorum" || name == "like") {
                section = &lists.liked;
            } else {
                section = nullptr;
            }
            continue;
        }
        if (section != nullptr) {
            std::string name = turkishLower(line);
            if (!name.empty()) {
                section->push_back(std::move(name));
            }
        }
    }
    return lists;
}

std::string withoutFruit(const std::string& dessert)
{
    std::string out;
    std::size_t pos = 0;
    while (pos <= dessert.size()) {
        std::size_t end = dessert.find('/', pos);
        if (end == std::string::npos) {
            end = dessert.size();
        }
        const std::string part = trim(dessert.substr(pos, end - pos));
        pos = end + 1U;
        if (part.empty() || turkishLower(part).find("meyve") != std::string::npos) {
            continue;
        }
        out += (out.empty() ? "" : " / ") + part;
    }
    return out;
}

DishMark markOf(const std::string& dish, const MenuLists& lists)
{
    if (dish.empty()) {
        return DishMark::None;
    }
    const std::string lower = turkishLower(dish);
    for (const std::string& name : lists.never) {
        if (contains(lower, name)) {
            return DishMark::Never;
        }
    }
    for (const std::string& name : lists.dislike) {
        if (contains(lower, name)) {
            return DishMark::Dislike;
        }
    }
    for (const std::string& name : lists.liked) {
        if (contains(lower, name)) {
            return DishMark::Liked;
        }
    }
    return DishMark::None;
}

DishMark markOf(const MenuDay& day, const MenuLists& lists)
{
    DishMark worst = DishMark::None;
    for (const std::string* dish : {&day.main, &day.side, &day.soup, &day.dessert, &day.extra}) {
        const DishMark m = markOf(*dish, lists);
        if (m == DishMark::Never) {
            return m;
        }
        if (m == DishMark::Dislike) {
            worst = m;
        }
    }
    return worst;
}

std::vector<std::string> dishesMarked(const MenuDay& day, const MenuLists& lists, DishMark mark)
{
    std::vector<std::string> out;
    for (const std::string* dish : {&day.main, &day.side, &day.soup, &day.dessert, &day.extra}) {
        if (markOf(*dish, lists) == mark) {
            out.push_back(*dish);
        }
    }
    return out;
}

std::optional<std::string> upgradeMenuListsComments(const std::string& text)
{
    // Compare without carriage returns (Notepad) and a byte order mark.
    std::string plain;
    plain.reserve(text.size());
    const std::size_t start = text.compare(0, 3, "\xEF\xBB\xBF") == 0 ? 3U : 0U;
    for (std::size_t i = start; i < text.size(); ++i) {
        if (text[i] != '\r') {
            plain += text[i];
        }
    }
    const std::string old = kOldComments;
    if (plain.compare(0, old.size(), old) != 0) {
        return std::nullopt;  // edited by hand, or already new
    }
    const std::string rest = plain.substr(old.size());
    bool hasLiked = false;  // a [seviyorum] / [like] line after the comments
    std::size_t pos = 0;
    while (pos <= rest.size()) {
        std::size_t end = rest.find('\n', pos);
        if (end == std::string::npos) {
            end = rest.size();
        }
        const std::string line = turkishLower(rest.substr(pos, end - pos));
        hasLiked = hasLiked || line == "[seviyorum]" || line == "[like]";
        pos = end + 1U;
    }
    std::string out = MENU_LISTS_COMMENTS + rest;
    if (!hasLiked) {
        out += (out.empty() || out.back() == '\n' ? "" : "\n");
        out += "\n[seviyorum]\n";
    }
    if (text.find("\r\n") != std::string::npos) {  // keep Windows line ends
        std::string crlf;
        for (const char c : out) {
            crlf += (c == '\n') ? std::string("\r\n") : std::string(1, c);
        }
        out = crlf;
    }
    return out;
}

}  // namespace stm32_audio::host
