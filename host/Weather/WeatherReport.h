// Open-Meteo forecast (https://open-meteo.com/en/docs): the request URL,
// the answer parsed into a small report, and the WMO weather code as an
// icon number of protocol.md and a text. Platform independent.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace stm32_audio::host {

struct WeatherNow {
    double temperatureC = 0.0;
    double feelsLikeC = 0.0;
    int humidityPercent = 0;
    double windKmh = 0.0;
    int code = 0;  // WMO weather code
    bool isDay = true;
};

struct WeatherDay {
    int year = 0;
    int month = 0;
    int day = 0;
    int code = 0;
    double minC = 0.0;
    double maxC = 0.0;
    std::optional<int> precipitationPercent;  // nullopt: not given
};

struct WeatherReport {
    WeatherNow now;
    std::vector<WeatherDay> days;  // in date order, today first
};

inline constexpr int kForecastDays = 10;

// Forecast URL for a location: current conditions and kForecastDays days
// in the location's time zone, °C and km/h.
std::string forecastUrl(double latitude, double longitude);

// nullopt if @p json is not a forecast answer with current conditions.
// Days with a missing value (code, min, max) are left out.
std::optional<WeatherReport> parseForecast(const std::string& json);

// Icon number of protocol.md ("Icon"): 1 sun, 2 moon, 3 cloud and sun,
// 4 cloud and moon, 5 cloud, 6 fog, 7 rain, 8 heavy rain, 9 snow,
// 10 thunderstorm; 0 for an unknown code.
int weatherIcon(int code, bool isDay);

// Short description of a WMO code; language as the settings record
// (0 Turkish, 1 English). "" for an unknown code.
std::string weatherText(int code, std::uint8_t language);

// "18°", "-3°" (rounded; never "-0°").
std::string formatDegrees(double celsius);

}  // namespace stm32_audio::host
