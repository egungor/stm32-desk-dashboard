// The menu card (slot 1, ADR-006, ADR-010) and its day detail, from the
// month files (MenuData.h), coloured by the owner's food lists
// (MenuLists.h); and the morning warning. Reads the files again every
// minute and after reload() (an import). Single threaded; platform
// independent.
//
//   card:   11 ␟ ␟ badge ␟ bg ␟ \n main dish \n soup \n dessert
//           badge: "" today, "Pzt 26" for the next working day at the weekend
//           bg: orange (a "never" dish), yellow (a "don't like" dish), green;
//               empty without a menu for the day
//   detail: Yemek ␟ 21 Ekim Çarşamba ␟ n \n rows (Ana yemek, Yan yemek, Çorba,
//           Tatlı, Yanında; a listed dish marked) \n #Salata bar \n the items;
//           ‹ › move by working day
//   warning (a prompt, id kWarningPromptId): on a working day with a listed
//           dish, once from kWarningHour: Yemek uyarısı ␟ date ␟ \n rows
//           \n !1 ␟ Tamam; answered once a day (the date in the warned file).
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
    static constexpr std::uint64_t kFileIntervalMs = 60U * 1000U;
    static constexpr std::uint8_t kWarningPromptId = 255;  // Claude Code prompts use 0..254
    static constexpr unsigned kWarningHour = 8;
    // Card backgrounds (ADR-010).
    static constexpr const char* kColourNever = "E0702F";
    static constexpr const char* kColourDislike = "D9A520";
    static constexpr const char* kColourOk = "3E9B5F";

    // @p lists: menu-lists.txt; @p warned: the date of the last warning.
    MenuService(DashController& dash, IMenuFiles& files, IPreferenceFile& lists, IPreferenceFile& warned,
                DashController::ClockFn clock, LogFn log);

    void setLanguage(std::uint8_t language);

    // Call regularly (e.g. every 500 ms) with a monotonic clock.
    void onTick(std::uint64_t nowMs);

    // The files changed (an import): read them at the next tick.
    void reload() { readMs_.reset(); }

    // OPEN_DETAIL on kSlot: arg 0 the card's day, -1 / +1 the previous /
    // next working day from the one shown.
    std::optional<std::string> detail(std::int8_t arg);

    // Records for a day (days since 1970-01-01), for tests and the console.
    std::string card(long today);
    std::string detailFor(long day);

    // The day the card shows: today, or Monday at the weekend.
    static long cardDay(long today);

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
    std::string dateText(long day) const;
    long currentDay() const { return static_cast<long>(clock_() / 86400U); }

    DashController& dash_;
    IMenuFiles& files_;
    IPreferenceFile& listsFile_;
    IPreferenceFile& warnedFile_;
    MenuLists lists_;
    std::optional<std::string> listsText_;
    bool listsRead_ = false;
    bool warningShown_ = false;
    long warnedDay_ = -1;  // answered today, also if the file could not be written
    DashController::ClockFn clock_;
    LogFn log_;
    std::uint8_t language_ = 0;
    std::optional<std::uint64_t> readMs_;
    std::map<std::string, std::optional<std::string>> texts_;   // file name -> text
    std::map<std::string, std::optional<MenuMonth>> months_;    // file name -> parsed
    long detailDay_ = 0;
};

}  // namespace stm32_audio::host
