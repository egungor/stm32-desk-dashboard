// The weather feature (ADR-006, v1 step 8): when to fetch the Open-Meteo
// forecast, and the card and detail records made from the last answer.
// Single threaded and platform independent: the caller fetches the URL
// fetchDue() hands out (on another thread) and passes the result back to
// onFetched().
//
//   card:   icon ␟ place ␟ ␟ bg ␟ L \n 18° \n 11° / 21°   (ADR-010)
//           ("Güncellendi 14:00" as a third line when stale; bg follows the
//           weather, weatherColour())
//   detail: Hava durumu ␟ place · 14:00 ␟ \n #Şimdi \n rows \n #10 gün \n
//           one row per day: icon ␟ Bugün / Yarın / Cum 23 ␟ 11° / 21° · %20
#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "WeatherLocation.h"
#include "WeatherReport.h"

namespace stm32_audio::host {

// The card's background (ADR-010) for a weather icon (protocol.md "Icon"
// 1-10), as RRGGBB; "" for anything else (the theme's colour).
const char* weatherColour(int icon);

class WeatherCard {
public:
    static constexpr std::uint64_t kRefreshMs = 30U * 60U * 1000U;
    static constexpr std::uint64_t kRetryMs = 5U * 60U * 1000U;

    // nullopt: no location set (the card says how to set one). A different
    // location drops the report and fetches at once.
    void setLocation(std::optional<WeatherLocation> place);
    const std::optional<WeatherLocation>& location() const { return location_; }

    // 0 Turkish, 1 English (settings record).
    void setLanguage(std::uint8_t language) { language_ = language; }

    // The URL to fetch now, or nullopt (nothing due, a fetch running, or no
    // location). A URL handed out counts as a running fetch until
    // onFetched().
    std::optional<std::string> fetchDue(std::uint64_t nowMs);

    // Result of fetching @p url: the answer body, or nullopt if it failed.
    // An answer for another location (set while it was on its way) is
    // dropped. @p localTime: local time now (localTimeValue()), for
    // "updated hh:mm". Returns true if the records changed.
    bool onFetched(std::uint64_t nowMs, std::uint32_t localTime, const std::string& url,
                   const std::optional<std::string>& body);

    bool stale() const { return stale_; }
    const std::optional<WeatherReport>& report() const { return report_; }

    // Records for @p localTime (picks today's day of the forecast).
    std::string card(std::uint32_t localTime) const;
    // nullopt until a report has arrived.
    std::optional<std::string> detail(std::uint32_t localTime) const;

private:
    std::optional<WeatherLocation> location_;
    std::uint8_t language_ = 0;
    std::optional<WeatherReport> report_;
    std::uint32_t updatedAt_ = 0;  // local time of the report
    bool stale_ = false;            // the last fetch failed
    bool fetching_ = false;
    std::optional<std::uint64_t> nextFetchMs_;  // nullopt: fetch at once
};

}  // namespace stm32_audio::host
