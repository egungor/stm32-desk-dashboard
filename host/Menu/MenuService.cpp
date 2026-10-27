#include "MenuService.h"

#include <cstdio>
#include <cstdlib>

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
    const char* liked;
    const char* warningTitle;
    const char* warningTitleOther;  // another day than today
    const char* neverRow;    // warning row labels
    const char* dislikeRow;
    const char* likedRow;
    const char* ok;
};

constexpr Texts kTurkish = {
    "Yemek", "Resmi tatil", "Bu gün için menü yok", "Menü yüklenmedi", "Tepside: Menü görüntülerini yükle...",
    "Salata bar: ", " çeşit", "Salata bar", {"Ana yemek", "Yan yemek", "Çorba", "Tatlı", "Yanında"},
    "istemiyorum", "sevmiyorum", "seviyorum", "Bugünkü menüde", "Menüde", "İstemiyorum", "Sevmiyorum",
    "Seviyorum", "Tamam",
};

constexpr Texts kEnglish = {
    "Menu", "Public holiday", "No menu for this day", "No menu loaded", "Tray: Load menu images...",
    "Salad bar: ", " items", "Salad bar", {"Main dish", "Side dish", "Soup", "Dessert", "With it"},
    "never", "don't like", "like", "On today's menu", "On the menu", "Never", "Don't like", "Like", "OK",
};

const Texts& texts(std::uint8_t language)
{
    return language == 1U ? kEnglish : kTurkish;
}

// The dish that decides the card's colour: the main dish, or the side dish
// on a day without one (as the card's first line).
const std::string& mainDish(const MenuDay& day)
{
    return day.main.empty() ? day.side : day.main;
}

// The icon (protocol.md "Icon") and the colour of a list's dishes.
const char* markIcon(DishMark mark)
{
    return mark == DishMark::Liked ? "14" : mark == DishMark::Never ? "15" : mark == DishMark::Dislike ? "16" : "0";
}

const char* markColour(DishMark mark)
{
    return mark == DishMark::Liked     ? MenuService::kColourGold
           : mark == DishMark::Never   ? MenuService::kColourNever
           : mark == DishMark::Dislike ? MenuService::kColourDislike
                                       : "";
}

long nextWorkingDay(long day, int step)
{
    do {
        day += step;
    } while (weekdayOf(day) == 0 || weekdayOf(day) == 6);
    return day;
}

std::string trimmed(const std::string& text)
{
    const std::size_t first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    return text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1U);
}

}  // namespace

unsigned parseMenuNextDayHour(const std::optional<std::string>& text)
{
    unsigned hour = MenuService::kNextDayHour;
    if (!text) {
        return hour;
    }
    std::size_t start = (text->compare(0, 3, "\xEF\xBB\xBF") == 0) ? 3U : 0U;
    while (start <= text->size()) {
        std::size_t end = text->find('\n', start);
        if (end == std::string::npos) {
            end = text->size();
        }
        const std::string line = trimmed(text->substr(start, end - start));
        start = end + 1U;
        const std::size_t eq = line.find('=');
        if (line.empty() || line[0] == '#' || eq == std::string::npos ||
            trimmed(line.substr(0, eq)) != "next_day_hour") {
            continue;
        }
        const std::string value = trimmed(line.substr(eq + 1U));
        char* stop = nullptr;
        const long parsed = std::strtol(value.c_str(), &stop, 10);
        if (!value.empty() && stop != nullptr && *stop == '\0' && parsed >= 0 && parsed <= 23) {
            hour = static_cast<unsigned>(parsed);
        }
    }
    return hour;
}

std::string formatMenuSettings(unsigned nextDayHour)
{
    return "# stm32-desk-dashboard menu settings\n"
           "# next_day_hour: from this hour of a working day the menu card shows the next\n"
           "# working day (1..23); 0: never\n"
           "next_day_hour=" +
           std::to_string(nextDayHour) + "\n";
}

MenuService::MenuService(DashController& dash, IMenuFiles& files, IPreferenceFile& lists, IPreferenceFile& warned,
                         IPreferenceFile& settings, DashController::ClockFn clock, LogFn log)
    : dash_(dash),
      files_(files),
      listsFile_(lists),
      warnedFile_(warned),
      settingsFile_(settings),
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
    const bool changed = listsRead_;
    if (changed && log_) {
        log_("Food lists: " + std::to_string(lists_.never.size()) + " never, " +
             std::to_string(lists_.dislike.size()) + " don't like, " + std::to_string(lists_.liked.size()) +
             " like.");
    }
    listsText_ = std::move(text);
    listsRead_ = true;
    dash_.refreshDetail(kSlot);
    if (changed) {
        // Warn again for the card's day with the new lists; a shown warning
        // is replaced (or closed if there is nothing to say any more).
        listsChangedDay_ = cardDay(clock_(), nextDayHour());
        if (warningShown_) {
            dash_.closePrompt(kWarningPromptId);
            warningShown_ = false;
        }
    }
}

void MenuService::setLanguage(std::uint8_t language)
{
    if (language != language_) {
        language_ = language;
        dash_.setCard(kSlot, card(clock_()));
    }
}

long MenuService::cardDay(std::uint32_t localTime, unsigned nextDayHour)
{
    const long today = static_cast<long>(localTime / 86400U);
    const int wd = weekdayOf(today);
    const bool lunchOver = nextDayHour != 0U && (localTime / 3600U) % 24U >= nextDayHour;
    return (wd == 0 || wd == 6 || lunchOver) ? nextWorkingDay(today, 1) : today;
}

void MenuService::readSettings()
{
    const unsigned hour = parseMenuNextDayHour(settingsFile_.read());
    if (settingsRead_ && hour != nextDayHour_) {
        if (log_) {
            log_("Menu: next day from " + (hour == 0U ? std::string("never") : std::to_string(hour) + ":00") + ".");
        }
        dash_.refreshDetail(kSlot);
    }
    nextDayHour_ = hour;
    settingsRead_ = true;
}

unsigned MenuService::nextDayHour()
{
    if (!settingsRead_) {
        readSettings();
    }
    return nextDayHour_;
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

void MenuService::onTick(std::uint64_t /*nowMs: the files are read on reload() only*/)
{
    if (!read_) {
        read_ = true;
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
        readSettings();
        if (changed) {
            if (log_) {
                log_("Menu files changed.");
            }
            dash_.refreshDetail(kSlot);
        }
    }
    dash_.setCard(kSlot, card(clock_()));
}

std::string MenuService::card(std::uint32_t localTime)
{
    const Texts& t = texts(language_);
    const long today = static_cast<long>(localTime / 86400U);
    const long day = cardDay(localTime, nextDayHour());
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
    // The main dish decides (ADR-010 amendment); a liked one is framed.
    const DishMark mark = markOf(mainDish(*entry), lists_);
    const char* bg = mark == DishMark::Never ? kColourNever : mark == DishMark::Dislike ? kColourDislike : kColourOk;
    std::string text = mark == DishMark::Liked ? recordLine({head, "", badge, bg, "", kColourGold, kColourGoldLight})
                                               : recordLine({head, "", badge, bg, ""});
    // The main dish (in the large line), then the side dish, soup and
    // dessert in the small lines.
    const std::string dessert = withoutFruit(entry->dessert);  // #49: no "Meyve /"
    const std::string none;
    for (const std::string* dish : {&mainDish(*entry), entry->main.empty() ? &none : &entry->side, &entry->soup,
                                    &dessert}) {
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
    DishMark marks[5];
    bool anyMarked = false;
    for (std::size_t i = 0; i < 5U; ++i) {
        marks[i] = markOf(*fields[i], lists_);
        anyMarked = anyMarked || marks[i] != DishMark::None;
    }
    for (std::size_t i = 0; i < 5U; ++i) {
        if (!fields[i]->empty()) {
            // A listed dish: its list's icon and colour, and the list's name
            // (an icon column for every row then, so that labels line up).
            const DishMark mark = marks[i];
            const std::string value = mark == DishMark::Never     ? *fields[i] + " \xC2\xB7 " + t.never
                                      : mark == DishMark::Dislike ? *fields[i] + " \xC2\xB7 " + t.dislike
                                      : mark == DishMark::Liked   ? *fields[i] + " \xC2\xB7 " + t.liked
                                                                  : *fields[i];
            text += "\n" + (mark != DishMark::None ? recordLine({markIcon(mark), t.labels[i], value, markColour(mark)})
                                                   : recordLine({anyMarked ? "0" : "", t.labels[i], value}));
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
    if (entry == nullptr || entry->holiday) {
        return std::nullopt;
    }
    // The main dish on a list: the panel in the card's colour (gold leaf
    // for a liked one). Else only "never" / "don't like" dishes warn: a
    // dark panel with the title strip in the card's (green) colour.
    const DishMark mainMark = markOf(mainDish(*entry), lists_);
    const bool otherBad = markOf(*entry, lists_) != DishMark::None;
    if (mainMark == DishMark::None && !otherBad) {
        return std::nullopt;
    }
    const Texts& t = texts(language_);
    const bool panel = mainMark != DishMark::None;
    const char* bg = mainMark == DishMark::Never     ? kColourNever
                     : mainMark == DishMark::Dislike ? kColourDislike
                                                     : kColourOk;
    const std::string icon = std::to_string(kIcon);
    const char* title = day == static_cast<long>(clock_() / 86400U) ? t.warningTitle : t.warningTitleOther;
    std::string text = panel ? recordLine({title, dateText(day), "", bg, mainMark == DishMark::Liked ? kColourGold : "",
                                           "", icon})
                             : recordLine({title, dateText(day), "", kColourOk, "", "s", icon});
    const std::pair<DishMark, const char*> groups[3] = {
        {DishMark::Never, t.neverRow}, {DishMark::Dislike, t.dislikeRow}, {DishMark::Liked, t.likedRow}};
    for (const auto& [mark, label] : groups) {
        // On a coloured panel its text colour, liked dishes gold on green;
        // on the dark panel each list's colour.
        const char* colour = !panel                                         ? markColour(mark)
                             : (mark == DishMark::Liked && mainMark == DishMark::Liked) ? kColourGoldLight
                                                                            : "";
        for (const std::string& dish : dishesMarked(*entry, lists_, mark)) {
            text += "\n" + (*colour != '\0' ? recordLine({markIcon(mark), label, dish, colour})
                                             : recordLine({markIcon(mark), label, dish}));
        }
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
    // The food lists changed: the card's day with the new lists.
    if (listsChangedDay_) {
        if (const std::optional<std::string> text = warningFor(*listsChangedDay_)) {
            dash_.showPrompt(kWarningPromptId, *text);
            warningShown_ = true;
            shownDay_ = *listsChangedDay_;
            if (log_) {
                log_("Menu warning shown (food lists changed).");
            }
            return;
        }
        listsChangedDay_.reset();
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
    shownDay_ = today;
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
    const long today = static_cast<long>(now / 86400U);
    listsChangedDay_.reset();
    // A warning about today also answers the morning one; one about
    // another day (the lists changed in the afternoon) does not.
    if (shownDay_ < 0 || shownDay_ == today) {
        int y = 0;
        int m = 0;
        int d = 0;
        civilFromDays(today, y, m, d);
        char date[16];
        std::snprintf(date, sizeof(date), "%04d-%02d-%02d", y, m, d);
        warnedDay_ = today;
        if (!warnedFile_.write(std::string(date) + "\n") && log_) {
            log_("WARN: cannot save the date of the menu warning.");
        }
    }
    dash_.closePrompt(kWarningPromptId);
    warningShown_ = false;
    return true;
}

std::optional<std::string> MenuService::detail(std::int8_t arg)
{
    if (arg == 0) {
        detailDay_ = cardDay(clock_(), nextDayHour());
    } else {
        detailDay_ = nextWorkingDay(detailDay_, arg > 0 ? 1 : -1);
    }
    return detailFor(detailDay_);
}

}  // namespace stm32_audio::host
