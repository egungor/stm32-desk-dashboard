// Puts the weather on the dashboard: reads the location file (again every
// minute, so "dashboard-host weather <city>" takes effect while the tray
// runs), fetches the forecast on a worker thread (BackgroundFetch), and
// keeps the card of its slot and the detail up to date. Single threaded
// apart from the fetch; platform independent (the file and the HTTP client
// are interfaces).
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

#include "BackgroundFetch.h"
#include "DashController.h"
#include "PreferenceFile.h"
#include "WeatherCard.h"

namespace stm32_audio::host {

class WeatherService {
public:
    using LogFn = std::function<void(const std::string& line)>;

    static constexpr std::uint8_t kSlot = 0;  // ADR-006: 0 weather

    WeatherService(DashController& dash, IPreferenceFile& file, IHttpClient& http, DashController::ClockFn clock,
                   LogFn log);

    // 0 Turkish, 1 English (settings record).
    void setLanguage(std::uint8_t language);

    // Call regularly (e.g. every 500 ms) with a monotonic clock. The
    // location file is read at the first tick and on reload() only.
    void onTick(std::uint64_t nowMs);

    // weather.txt changed (settings window or console, #61): read it now.
    void reload();

    // The detail for OPEN_DETAIL on kSlot; nullopt: none (yet), or arg != 0.
    std::optional<std::string> detail(std::int8_t arg) const;

    const WeatherCard& card() const { return weather_; }

private:
    void readLocation();
    void publish();

    DashController& dash_;
    IPreferenceFile& file_;
    BackgroundFetch fetch_;
    DashController::ClockFn clock_;
    LogFn log_;
    WeatherCard weather_;
    bool fileRead_ = false;
    std::optional<std::string> fileText_;
    std::optional<std::string> shownDetail_;
    std::uint8_t language_ = 0;
};

}  // namespace stm32_audio::host
