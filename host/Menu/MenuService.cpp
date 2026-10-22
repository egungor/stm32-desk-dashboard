#include "MenuService.h"

#include <cstdio>

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
    const char* never;       // a dish of the "never" list, in the detail
    const char* dislike;
    const char* warningTitle;
    const char* neverRow;    // warning row labels
    const char* dislikeRow;
    const char* ok;
};

constexpr Texts kTurkish = {
    "Yemek", "Resmi tatil", "Bu gün için menü yok", "Menü yüklenmedi", "Tepside: Menü görüntülerini yükle...",
    "Salata bar: ", " çeşit", "Salata bar", {"Ana yemek", "Yan yemek", "Çorba", "Tatlı", "Yanında"},
    "istemiyorum", "sevmiyorum", "Bugünkü menüde", "İstemiyorum", "Sevmiyorum", "Tamam",
};

constexpr Texts kEnglish = {
    "Menu", "Public holiday", "No menu for this day", "No menu loaded", "Tray: Load menu images...",
    "Salad bar: ", " items", "Salad bar", {"Main dish", "Side dish", "Soup", "Dessert", "With it"},
    "never", "don't like", "On today's menu", "Never", "Don't like", "OK",
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

MenuService::MenuService(DashController& dash, IMenuFiles& files, IPreferenceFile& lists, IPreferenceFile& warned,
                         DashController::ClockFn clock, LogFn log)
    : dash_(dash),
      files_(files),
      listsFile_(lists),
      warnedFile_(warned),
      clock_(std::move(clock)),
      log_(std::move(log))
{
}

void MenuService::readLists()
{
    std::optional<std::string> text = listsFile_.read();
    if (listsRead_ && text == listsText_) {
        return;
    }
    lists_ = text ? parseMenuLists(*text) : MenuLists{};
    if (listsRead_ && log_) {
        log_("Food lists: " + std::to_string(lists_.never.size()) + " never, " +
             std::to_string(lists_.dislike.size()) + " don't like.");
    }
    listsText_ = std::move(text);
    listsRead_ = true;
    dash_.refreshDetail(kSlot);
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
        readLists();
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
    if (!listsRead_) {
        readLists();
    }
    const MenuDay* entry = dayAt(day);
    const std::string head = std::to_string(kIcon);
    if (entry == nullptr) {
        // Nothing for that day: was a menu loaded at all for its month?
        int y = 0;
        int m = 0;
        int d = 0;
        civilFromDays(day, y, m, d);
        return recordLine({head, "", badge, "", ""}) + "\n" +
               (monthOf(y, m) ? t.noMenu : std::string(t.notLoaded) + "\n" + t.howToLoad);
    }
    if (entry->holiday) {
        return recordLine({head, "", badge, "", ""}) + "\n" + t.holiday;
    }
    if (!entry->hasMeal() && entry->dessert.empty()) {
        const std::string salad =
            entry->saladBar.empty() ? std::string(t.noMenu)
                                    : t.saladCount + std::to_string(entry->saladBar.size()) + t.saladItems;
        return recordLine({head, "", badge, "", ""}) + "\n" + salad;
    }
    const DishMark mark = markOf(*entry, lists_);
    const char* bg = mark == DishMark::Never ? kColourNever : mark == DishMark::Dislike ? kColourDislike : kColourOk;
    std::string text = recordLine({head, "", badge, bg, ""});
    for (const std::string* dish : {entry->main.empty() ? &entry->side : &entry->main, &entry->soup, &entry->dessert}) {
        if (!dish->empty()) {
            text += "\n" + recordLine({*dish});
        }
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
    (void)wd;
    if (!listsRead_) {
        readLists();
    }
    std::string text = recordLine({t.title, dateText(day), "n"});
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
            const DishMark mark = markOf(*fields[i], lists_);
            const std::string value = mark == DishMark::Never     ? *fields[i] + " \xC2\xB7 " + t.never
                                      : mark == DishMark::Dislike ? *fields[i] + " \xC2\xB7 " + t.dislike
                                                                  : *fields[i];
            text += "\n" + recordLine({"", t.labels[i], value});
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

std::string MenuService::dateText(long day) const
{
    int y = 0;
    int m = 0;
    int d = 0;
    civilFromDays(day, y, m, d);
    const int wd = weekdayOf(day);
    return language_ == 1U ? std::string(weekdayName(wd, 1)) + " " + std::to_string(d) + " " + monthName(m, 1)
                           : std::to_string(d) + " " + monthName(m, 0) + " " + weekdayName(wd, 0);
}

std::optional<std::string> MenuService::warningFor(long day)
{
    if (!listsRead_) {
        readLists();
    }
    const MenuDay* entry = dayAt(day);
    if (entry == nullptr || entry->holiday || markOf(*entry, lists_) == DishMark::None) {
        return std::nullopt;
    }
    const Texts& t = texts(language_);
    std::string text = recordLine({t.warningTitle, dateText(day), ""});
    for (const std::string& dish : dishesMarked(*entry, lists_, DishMark::Never)) {
        text += "\n" + recordLine({std::to_string(kIcon), t.neverRow, dish});
    }
    for (const std::string& dish : dishesMarked(*entry, lists_, DishMark::Dislike)) {
        text += "\n" + recordLine({std::to_string(kIcon), t.dislikeRow, dish});
    }
    return text + "\n" + recordLine({"!1", t.ok});
}

void MenuService::updateWarning(bool promptFree)
{
    if (!promptFree || !dash_.synced()) {
        warningShown_ = false;  // replaced by a Claude Code prompt, or the board is gone
        return;
    }
    if (warningShown_) {
        return;
    }
    const std::uint32_t now = clock_();
    const long today = static_cast<long>(now / 86400U);
    const int wd = weekdayOf(today);
    if (today == warnedDay_ || wd == 0 || wd == 6 || (now / 3600U) % 24U < kWarningHour) {
        return;
    }
    int y = 0;
    int m = 0;
    int d = 0;
    civilFromDays(today, y, m, d);
    char date[16];
    std::snprintf(date, sizeof(date), "%04d-%02d-%02d", y, m, d);
    const std::optional<std::string> warned = warnedFile_.read();
    if (warned && warned->compare(0, 10, date) == 0) {
        return;
    }
    const std::optional<std::string> text = warningFor(today);
    if (!text) {
        return;
    }
    dash_.showPrompt(kWarningPromptId, *text);
    warningShown_ = true;
    if (log_) {
        log_("Menu warning shown.");
    }
}

bool MenuService::onAction(const ProtoAction& action)
{
    if (action.kind != PROTO_ACTION_PROMPT || action.ref != kWarningPromptId) {
        return false;
    }
    const std::uint32_t now = clock_();
    int y = 0;
    int m = 0;
    int d = 0;
    civilFromDays(static_cast<long>(now / 86400U), y, m, d);
    char date[16];
    std::snprintf(date, sizeof(date), "%04d-%02d-%02d", y, m, d);
    warnedDay_ = static_cast<long>(now / 86400U);
    if (!warnedFile_.write(std::string(date) + "\n") && log_) {
        log_("WARN: cannot save the date of the menu warning.");
    }
    dash_.closePrompt(kWarningPromptId);
    warningShown_ = false;
    return true;
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
