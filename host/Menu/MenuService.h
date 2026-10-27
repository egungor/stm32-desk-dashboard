// The menu card (slot 1, ADR-006, ADR-010) and its day detail, from the
// month files (MenuData.h), coloured by the owner's food lists
// (MenuLists.h); and the morning warning. Reads the files again every
// minute and after reload() (an import). Single threaded; platform
// independent.
//
//   card:   11 ␟ ␟ badge ␟ bg ␟ [␟ frame ␟ icon colour] \n main dish \n side
//           dish \n soup \n dessert (the board puts the badge below the icon)
//           badge: "" today, "Pzt 26" for the next working day at the weekend
//           and from the next-day hour on a working day (lunch is over;
//           menu-settings.txt, settings window; default kNextDayHour)
//           bg by the main dish only (ADR-010 amendment): orange ("never"),
//               yellow ("don't like"), else green; a liked main dish adds the
//               gold-leaf frame and a gold icon; empty without a menu
//   detail: Yemek ␟ 21 Ekim Çarşamba ␟ n \n rows (Ana yemek, Yan yemek, Çorba,
//           Tatlı, Yanında; a listed dish with its list's icon, colour and
//           name) \n #Salata bar \n the items; ‹ › move by working day
//   warning (a prompt, id kWarningPromptId): when the main dish is on a list
//           (the panel in the card's colour, the gold leaf for a liked one),
//           or another dish is on "never" / "don't like" (a dark panel, the
//           title strip in the card's colour); a liked side dish alone gives
//           none. Rows: the list's icon, name and the dish; !1 ␟ Tamam.
//           Shown once a day from kWarningHour (the date in the warned file)
//           for today, and when the food lists change for the card's day.
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>

#include "DashController.h"
#include "MenuImport.h"
#include "MenuLists.h"
#include "PreferenceFile.h"

namespace stm32_audio::host {

class MenuService {
public:
    using LogFn = std::function<void(const std::string& line)>;

    static constexpr std::uint8_t kSlot = 1;  // ADR-006: 1 menu
    static constexpr int kIcon = 11;          // protocol.md "Icon": utensils
    static constexpr std::uint8_t kWarningPromptId = 255;  // Claude Code prompts use 0..254
    static constexpr unsigned kWarningHour = 8;
    // From this hour of a working day the card shows the next working day
    // (the default of menu-settings.txt; 0 there: never).
    static constexpr unsigned kNextDayHour = 14;
    // Card backgrounds (ADR-010).
    static constexpr const char* kColourNever = "E0702F";
    static constexpr const char* kColourDislike = "D9A520";
    static constexpr const char* kColourOk = "3E9B5F";
    static constexpr const char* kColourGold = "D4AF37";      // the frame; liked rows
    static constexpr const char* kColourGoldLight = "F2C94C"; // the card's icon; rows on green

    // @p lists: menu-lists.txt; @p warned: the date of the last warning;
    // @p settings: menu-settings.txt (parseMenuNextDayHour()).
    MenuService(DashController& dash, IMenuFiles& files, IPreferenceFile& lists, IPreferenceFile& warned,
                IPreferenceFile& settings, DashController::ClockFn clock, LogFn log);

    void setLanguage(std::uint8_t language);

    // Call regularly (e.g. every 500 ms) with a monotonic clock.
    void onTick(std::uint64_t nowMs);

    // The month files, menu-lists.txt or menu-settings.txt changed (an import, the settings
    // window, #61): read them at the next tick. Not read otherwise.
    void reload() { read_ = false; }

    // OPEN_DETAIL on kSlot: arg 0 the card's day, -1 / +1 the previous /
    // next working day from the one shown.
    std::optional<std::string> detail(std::int8_t arg);

    // The card at @p localTime (seconds since 1970-01-01, local time) and
    // the detail of a day (days since 1970-01-01), for tests and the console.
    std::string card(std::uint32_t localTime);
    std::string detailFor(long day);

    // The day the card shows at @p localTime: today before @p nextDayHour
    // on a working day (0: all day), else the next working day (Monday at
    // the weekend).
    static long cardDay(std::uint32_t localTime, unsigned nextDayHour = kNextDayHour);
    // The hour of menu-settings.txt now.
    unsigned nextDayHour();

    // The morning warning: shows it when it is due, the board is synced and
    // @p promptFree (no Claude Code request waits; one that comes replaces
    // it on the board, and it is shown again afterwards).
    void updateWarning(bool promptFree);
    // ACTION kind 1 for kWarningPromptId: the warning was answered. False
    // for any other action.
    bool onAction(const ProtoAction& action);
    // The warning's prompt record for @p day, or nullopt if nothing on that
    // day is on the lists (for tests and the console).
    std::optional<std::string> warningFor(long day);

private:
    const MenuDay* dayAt(long day);
    const std::optional<MenuMonth>& monthOf(int year, int month);
    void readLists();
    void readSettings();
    std::string dateText(long day) const;

    DashController& dash_;
    IMenuFiles& files_;
    IPreferenceFile& listsFile_;
    IPreferenceFile& warnedFile_;
    IPreferenceFile& settingsFile_;
    unsigned nextDayHour_ = kNextDayHour;
    bool settingsRead_ = false;
    MenuLists lists_;
    std::optional<std::string> listsText_;
    bool listsRead_ = false;
    bool warningShown_ = false;
    long shownDay_ = -1;                 // the day of the shown warning
    std::optional<long> listsChangedDay_;  // a warning for the changed lists is due
    long warnedDay_ = -1;  // answered today, also if the file could not be written
    DashController::ClockFn clock_;
    LogFn log_;
    std::uint8_t language_ = 0;
    bool read_ = false;
    std::map<std::string, std::optional<std::string>> texts_;   // file name -> text
    std::map<std::string, std::optional<MenuMonth>> months_;    // file name -> parsed
    long detailDay_ = 0;
};

// menu-settings.txt:
//
//   next_day_hour=14     the card shows the next working day from this hour
//                        (1..23); 0: never on a working day
//
// No file, a missing key or a bad value: kNextDayHour.
unsigned parseMenuNextDayHour(const std::optional<std::string>& text);
std::string formatMenuSettings(unsigned nextDayHour);

}  // namespace stm32_audio::host
