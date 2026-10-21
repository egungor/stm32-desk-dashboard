// The menu card (slot 1, ADR-006) and its day detail, from the month files
// (MenuData.h). Reads the files again every minute and after reload() (an
// import). Single threaded; platform independent.
//
//   card:   11 ␟ Yemek ␟ badge \n main dish \n soup \n Salata bar: 6 çeşit
//           badge: "" today, "Pzt 26" for the next working day at the weekend
//   detail: Yemek ␟ 21 Ekim Çarşamba ␟ n \n rows (Ana yemek, Yan yemek, Çorba,
//           Tatlı, Yanında) \n #Salata bar \n the items; ‹ › move by working day
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>

#include "DashController.h"
#include "MenuImport.h"

namespace stm32_audio::host {

class MenuService {
public:
    using LogFn = std::function<void(const std::string& line)>;

    static constexpr std::uint8_t kSlot = 1;  // ADR-006: 1 menu
    static constexpr int kIcon = 11;          // protocol.md "Icon": utensils
    static constexpr std::uint64_t kFileIntervalMs = 60U * 1000U;

    MenuService(DashController& dash, IMenuFiles& files, DashController::ClockFn clock, LogFn log);

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

private:
    const MenuDay* dayAt(long day);
    const std::optional<MenuMonth>& monthOf(int year, int month);
    long currentDay() const { return static_cast<long>(clock_() / 86400U); }

    DashController& dash_;
    IMenuFiles& files_;
    DashController::ClockFn clock_;
    LogFn log_;
    std::uint8_t language_ = 0;
    std::optional<std::uint64_t> readMs_;
    std::map<std::string, std::optional<std::string>> texts_;   // file name -> text
    std::map<std::string, std::optional<MenuMonth>> months_;    // file name -> parsed
    long detailDay_ = 0;
};

}  // namespace stm32_audio::host
