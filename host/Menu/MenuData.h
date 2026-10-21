// The menu of the day (v1 step 9): one JSON file per month in
// %APPDATA%\stm32-desk-dashboard, written by the import and editable in
// Notepad (format of the design draft):
//
//   {
//     "ay": "2026-10",
//     "gunler": {
//       "2026-10-05": { "durum": "var", "ana_yemek": "Kağıt kebabı", "yan_yemek": "...",
//                       "corba": "...", "tatli": "...", "yaninda": "...",
//                       "salata_bar": ["Akdeniz salata", "..."] },
//       "2026-10-28": { "durum": "tatil" }
//     }
//   }
//
// Platform independent.
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace stm32_audio::host {

struct MenuDay {
    bool holiday = false;
    std::string main;     // ana yemek
    std::string side;     // yan yemek
    std::string soup;     // çorba
    std::string dessert;  // tatlı
    std::string extra;    // yanında
    std::vector<std::string> saladBar;

    bool hasMeal() const { return !main.empty() || !side.empty() || !soup.empty(); }
};

struct MenuMonth {
    int year = 0;
    int month = 0;
    std::map<int, MenuDay> days;  // day of the month
};

// "menu-2026-10" (the file name without ".json").
std::string menuFileName(int year, int month);

// nullopt if @p json is not a month file (or another month than the name
// says, when @p year / @p month are given). Days of other months, bad
// dates and unknown states are left out.
std::optional<MenuMonth> parseMonthFile(const std::string& json);
std::string formatMonthFile(const MenuMonth& month);

// Put an imported day into @p month: a holiday replaces the day; a menu
// replaces the fields it has (the meal and the salad bar can come from
// different imports) and clears a holiday.
void mergeDay(MenuMonth& month, int day, const MenuDay& incoming);

// "Ekim 2026: 20 gün yemek, 17 gün salata bar, 2 tatil; salata bar eksik:
// 26, 27, 30 Ekim" (language 0 Turkish, 1 English).
std::string monthSummary(const MenuMonth& month, std::uint8_t language);

// ---------------------------------------------------------------- dates

// Days since 1970-01-01 of a civil date, and back.
long daysFromCivil(int year, int month, int day);
void civilFromDays(long days, int& year, int& month, int& day);
// 0 Sunday … 6 Saturday.
int weekdayOf(long days);
bool validDate(int year, int month, int day);

// Month and weekday names; language 0 Turkish, 1 English.
const char* monthName(int month, std::uint8_t language);
const char* weekdayName(int weekday, std::uint8_t language);
const char* weekdayShort(int weekday, std::uint8_t language);

}  // namespace stm32_audio::host
