#include "MenuService.h"

namespace stm32_audio::host {

namespace {

struct Texts {
    const char* title;
    const char* holiday;
    const char* noMenu;
    const char* notLoaded;
    const char* howToLoad;
    const char* saladCount;  // + number + saladItems
    const char* saladItems;
    const char* saladBar;
    const char* labels[5];   // main, side, soup, dessert, extra
};

constexpr Texts kTurkish = {
    "Yemek", "Resmi tatil", "Bu gün için menü yok", "Menü yüklenmedi", "Tepside: Menü görüntülerini yükle…",
    "Salata bar: ", " çeşit", "Salata bar", {"Ana yemek", "Yan yemek", "Çorba", "Tatlı", "Yanında"},
};

constexpr Texts kEnglish = {
    "Menu", "Public holiday", "No menu for this day", "No menu loaded", "Tray: Load menu images…",
    "Salad bar: ", " items", "Salad bar", {"Main dish", "Side dish", "Soup", "Dessert", "With it"},
};

const Texts& texts(std::uint8_t language)
{
    return language == 1U ? kEnglish : kTurkish;
}

long nextWorkingDay(long day, int step)
{
    do {
        day += step;
    } while (weekdayOf(day) == 0 || weekdayOf(day) == 6);
    return day;
}

}  // namespace

MenuService::MenuService(DashController& dash, IMenuFiles& files, DashController::ClockFn clock, LogFn log)
    : dash_(dash), files_(files), clock_(std::move(clock)), log_(std::move(log))
{
}

void MenuService::setLanguage(std::uint8_t language)
{
    if (language != language_) {
        language_ = language;
        dash_.setCard(kSlot, card(currentDay()));
    }
}

long MenuService::cardDay(long today)
{
    const int wd = weekdayOf(today);
    return (wd == 0 || wd == 6) ? nextWorkingDay(today, 1) : today;
}

const std::optional<MenuMonth>& MenuService::monthOf(int year, int month)
{
    const std::string name = menuFileName(year, month);
    auto it = months_.find(name);
    if (it != months_.end()) {
        return it->second;
    }
    std::optional<std::string> text = files_.read(name);
    std::optional<MenuMonth> parsed;
    if (text) {
        parsed = parseMonthFile(*text);
        if (parsed && (parsed->year != year || parsed->month != month)) {
            parsed.reset();
        }
        if (!parsed && log_) {
            log_("WARN: " + name + ".json is not a menu file of that month; ignored.");
        }
    }
    texts_[name] = std::move(text);
    return months_.emplace(name, std::move(parsed)).first->second;
}

const MenuDay* MenuService::dayAt(long day)
{
    int y = 0;
    int m = 0;
    int d = 0;
    civilFromDays(day, y, m, d);
    const std::optional<MenuMonth>& month = monthOf(y, m);
    if (!month) {
        return nullptr;
    }
    const auto it = month->days.find(d);
    return it == month->days.end() ? nullptr : &it->second;
}

void MenuService::onTick(std::uint64_t nowMs)
{
    if (!readMs_ || nowMs - *readMs_ >= kFileIntervalMs) {
        readMs_ = nowMs;
        // Read every file again; refresh an open detail if one changed.
        bool changed = false;
        for (const auto& [name, text] : texts_) {
            if (files_.read(name) != text) {
                changed = true;
            }
        }
        texts_.clear();
        months_.clear();
        if (changed) {
            if (log_) {
                log_("Menu files changed.");
            }
            dash_.refreshDetail(kSlot);
        }
    }
    dash_.setCard(kSlot, card(currentDay()));
}

std::string MenuService::card(long today)
{
    const Texts& t = texts(language_);
    const long day = cardDay(today);
    std::string badge;
    if (day != today) {
        int y = 0;
        int m = 0;
        int d = 0;
        civilFromDays(day, y, m, d);
        badge = std::string(weekdayShort(weekdayOf(day), language_)) + " " + std::to_string(d);
    }
    std::string text = recordLine({std::to_string(kIcon), t.title, badge});
    const MenuDay* entry = dayAt(day);
    if (entry == nullptr) {
        // Nothing for that day: was a menu loaded at all for its month?
        int y = 0;
        int m = 0;
        int d = 0;
        civilFromDays(day, y, m, d);
        return text + "\n" + (monthOf(y, m) ? t.noMenu : std::string(t.notLoaded) + "\n" + t.howToLoad);
    }
    if (entry->holiday) {
        return text + "\n" + t.holiday;
    }
    const std::string salad =
        entry->saladBar.empty() ? std::string()
                                : t.saladCount + std::to_string(entry->saladBar.size()) + t.saladItems;
    if (!entry->hasMeal()) {
        return text + "\n" + (salad.empty() ? std::string(t.noMenu) : salad);
    }
    text += "\n" + (entry->main.empty() ? entry->side : entry->main);
    if (!entry->soup.empty()) {
        text += "\n" + entry->soup;
    }
    if (!salad.empty()) {
        text += "\n" + salad;
    } else if (!entry->dessert.empty()) {
        text += "\n" + entry->dessert;
    }
    return text;
}

std::string MenuService::detailFor(long day)
{
    const Texts& t = texts(language_);
    int y = 0;
    int m = 0;
    int d = 0;
    civilFromDays(day, y, m, d);
    const int wd = weekdayOf(day);
    const std::string date = language_ == 1U ? std::string(weekdayName(wd, 1)) + " " + std::to_string(d) + " " +
                                                   monthName(m, 1)
                                             : std::to_string(d) + " " + monthName(m, 0) + " " + weekdayName(wd, 0);
    std::string text = recordLine({t.title, date, "n"});
    const MenuDay* entry = dayAt(day);
    if (entry == nullptr) {
        return text + "\n" + recordLine({"", "", monthOf(y, m) ? t.noMenu : t.notLoaded});
    }
    if (entry->holiday) {
        return text + "\n" + recordLine({"", "", t.holiday});
    }
    const std::string* fields[5] = {&entry->main, &entry->side, &entry->soup, &entry->dessert, &entry->extra};
    for (std::size_t i = 0; i < 5U; ++i) {
        if (!fields[i]->empty()) {
            text += "\n" + recordLine({"", t.labels[i], *fields[i]});
        }
    }
    if (!entry->saladBar.empty()) {
        std::string items;
        for (const std::string& item : entry->saladBar) {
            items += (items.empty() ? "" : " \xC2\xB7 ") + item;
        }
        text += "\n#" + std::string(t.saladBar) + "\n" + recordLine({"", "", items});
    }
    return text;
}

std::optional<std::string> MenuService::detail(std::int8_t arg)
{
    if (arg == 0) {
        detailDay_ = cardDay(currentDay());
    } else {
        detailDay_ = nextWorkingDay(detailDay_, arg > 0 ? 1 : -1);
    }
    return detailFor(detailDay_);
}

}  // namespace stm32_audio::host
