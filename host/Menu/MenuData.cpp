#include "MenuData.h"

#include <cstdio>

#include "nlohmann/json.hpp"

namespace stm32_audio::host {

namespace {

using nlohmann::ordered_json;

const char* const kMonthsTr[] = {"Ocak", "Şubat", "Mart",  "Nisan",  "Mayıs", "Haziran",
                                 "Temmuz", "Ağustos", "Eylül", "Ekim", "Kasım", "Aralık"};
const char* const kMonthsEn[] = {"January", "February", "March",     "April",   "May",      "June",
                                 "July",    "August",   "September", "October", "November", "December"};
const char* const kDaysTr[] = {"Pazar", "Pazartesi", "Salı", "Çarşamba", "Perşembe", "Cuma", "Cumartesi"};
const char* const kDaysEn[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
const char* const kShortTr[] = {"Paz", "Pzt", "Sal", "Çar", "Per", "Cum", "Cmt"};
const char* const kShortEn[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

// "2026-10-05" -> numbers; false if not that shape.
bool parseIsoDate(const std::string& text, int& y, int& m, int& d)
{
    if (text.size() != 10U || text[4] != '-' || text[7] != '-') {
        return false;
    }
    auto digits = [&text](std::size_t from, std::size_t count) {
        int n = 0;
        for (std::size_t i = from; i < from + count; ++i) {
            if (text[i] < '0' || text[i] > '9') {
                return -1;
            }
            n = n * 10 + (text[i] - '0');
        }
        return n;
    };
    y = digits(0, 4);
    m = digits(5, 2);
    d = digits(8, 2);
    return y >= 0 && m >= 0 && d >= 0;
}

std::string isoDate(int y, int m, int d)
{
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d", y, m, d);
    return buffer;
}

std::string stringField(const ordered_json& object, const char* key)
{
    const auto it = object.find(key);
    return (it != object.end() && it->is_string()) ? it->get<std::string>() : std::string();
}

}  // namespace

long daysFromCivil(int y, int m, int d)
{
    y -= (m <= 2) ? 1 : 0;
    const long era = (y >= 0 ? y : y - 399) / 400;
    const long yoe = y - era * 400;
    const long doy = (153L * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

void civilFromDays(long z, int& y, int& m, int& d)
{
    z += 719468;
    const long era = (z >= 0 ? z : z - 146096) / 146097;
    const long doe = z - era * 146097;
    const long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const long mp = (5 * doy + 2) / 153;
    d = static_cast<int>(doy - (153 * mp + 2) / 5 + 1);
    m = static_cast<int>(mp < 10 ? mp + 3 : mp - 9);
    y = static_cast<int>(yoe + era * 400 + (m <= 2 ? 1 : 0));
}

int weekdayOf(long days)
{
    return static_cast<int>(((days % 7) + 11) % 7);  // 1970-01-01 was a Thursday
}

bool validDate(int y, int m, int d)
{
    if (y < 1970 || y > 2100 || m < 1 || m > 12 || d < 1 || d > 31) {
        return false;
    }
    int yy = 0;
    int mm = 0;
    int dd = 0;
    civilFromDays(daysFromCivil(y, m, d), yy, mm, dd);
    return yy == y && mm == m && dd == d;
}

const char* monthName(int month, std::uint8_t language)
{
    if (month < 1 || month > 12) {
        return "";
    }
    return (language == 1U ? kMonthsEn : kMonthsTr)[month - 1];
}

const char* weekdayName(int weekday, std::uint8_t language)
{
    return (weekday < 0 || weekday > 6) ? "" : (language == 1U ? kDaysEn : kDaysTr)[weekday];
}

const char* weekdayShort(int weekday, std::uint8_t language)
{
    return (weekday < 0 || weekday > 6) ? "" : (language == 1U ? kShortEn : kShortTr)[weekday];
}

std::string menuFileName(int year, int month)
{
    char buffer[24];
    std::snprintf(buffer, sizeof(buffer), "menu-%04d-%02d", year, month);
    return buffer;
}

std::optional<MenuMonth> parseMonthFile(const std::string& text)
{
    const ordered_json root = ordered_json::parse(text, nullptr, false);
    if (!root.is_object()) {
        return std::nullopt;
    }
    MenuMonth month;
    int day = 0;
    if (!parseIsoDate(stringField(root, "ay") + "-01", month.year, month.month, day) ||
        !validDate(month.year, month.month, 1)) {
        return std::nullopt;
    }
    const auto days = root.find("gunler");
    if (days == root.end() || !days->is_object()) {
        return month;
    }
    for (const auto& [key, value] : days->items()) {
        int y = 0;
        int m = 0;
        int d = 0;
        if (!parseIsoDate(key, y, m, d) || y != month.year || m != month.month || !validDate(y, m, d) ||
            !value.is_object()) {
            continue;
        }
        const std::string state = stringField(value, "durum");
        MenuDay entry;
        if (state == "tatil") {
            entry.holiday = true;
        } else if (state == "var") {
            entry.main = stringField(value, "ana_yemek");
            entry.side = stringField(value, "yan_yemek");
            entry.soup = stringField(value, "corba");
            entry.dessert = stringField(value, "tatli");
            entry.extra = stringField(value, "yaninda");
            const auto salad = value.find("salata_bar");
            if (salad != value.end() && salad->is_array()) {
                for (const auto& item : *salad) {
                    if (item.is_string() && !item.get_ref<const std::string&>().empty()) {
                        entry.saladBar.push_back(item.get<std::string>());
                    }
                }
            }
        } else {
            continue;
        }
        month.days[d] = std::move(entry);
    }
    return month;
}

std::string formatMonthFile(const MenuMonth& month)
{
    ordered_json days = ordered_json::object();
    for (const auto& [d, entry] : month.days) {
        ordered_json value = ordered_json::object();
        if (entry.holiday) {
            value["durum"] = "tatil";
        } else {
            value["durum"] = "var";
            value["ana_yemek"] = entry.main;
            value["yan_yemek"] = entry.side;
            value["corba"] = entry.soup;
            value["tatli"] = entry.dessert;
            value["yaninda"] = entry.extra;
            value["salata_bar"] = entry.saladBar;
        }
        days[isoDate(month.year, month.month, d)] = std::move(value);
    }
    char ay[16];
    std::snprintf(ay, sizeof(ay), "%04d-%02d", month.year, month.month);
    ordered_json root = ordered_json::object();
    root["ay"] = ay;
    root["gunler"] = std::move(days);
    // Invalid UTF-8 (cannot come from the parser) is replaced, never thrown.
    return root.dump(2, ' ', false, ordered_json::error_handler_t::replace) + "\n";
}

void mergeDay(MenuMonth& month, int day, const MenuDay& incoming)
{
    MenuDay& entry = month.days[day];
    if (incoming.holiday) {
        entry = MenuDay{};
        entry.holiday = true;
        return;
    }
    entry.holiday = false;
    auto take = [](std::string& to, const std::string& from) {
        if (!from.empty()) {
            to = from;
        }
    };
    take(entry.main, incoming.main);
    take(entry.side, incoming.side);
    take(entry.soup, incoming.soup);
    take(entry.dessert, incoming.dessert);
    take(entry.extra, incoming.extra);
    if (!incoming.saladBar.empty()) {
        entry.saladBar = incoming.saladBar;
    }
}

std::string monthSummary(const MenuMonth& month, std::uint8_t language)
{
    int meals = 0;
    int salads = 0;
    int holidays = 0;
    std::vector<int> noSalad;
    std::vector<int> noMeal;
    for (const auto& [d, entry] : month.days) {
        if (entry.holiday) {
            ++holidays;
            continue;
        }
        meals += entry.hasMeal() ? 1 : 0;
        salads += entry.saladBar.empty() ? 0 : 1;
        if (entry.hasMeal() && entry.saladBar.empty()) {
            noSalad.push_back(d);
        }
        if (!entry.hasMeal() && !entry.saladBar.empty()) {
            noMeal.push_back(d);
        }
    }
    const bool en = language == 1U;
    const std::string name = monthName(month.month, language);
    std::string text = name + " " + std::to_string(month.year) + ": " + std::to_string(meals) +
                       (en ? " days with a meal, " : " gün yemek, ") + std::to_string(salads) +
                       (en ? " with the salad bar, " : " gün salata bar, ") + std::to_string(holidays) +
                       (en ? (holidays == 1 ? " holiday" : " holidays") : " tatil");
    auto list = [&](const char* tr, const char* enText, const std::vector<int>& days) {
        if (days.empty()) {
            return;
        }
        text += std::string("; ") + (en ? enText : tr) + ": ";
        for (std::size_t i = 0; i < days.size(); ++i) {
            text += (i > 0 ? ", " : "") + std::to_string(days[i]);
        }
        text += " " + name;
    };
    list("salata bar eksik", "no salad bar", noSalad);
    list("yemek eksik", "no meal", noMeal);
    return text;
}

}  // namespace stm32_audio::host
