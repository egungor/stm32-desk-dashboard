#include "WeatherReport.h"

#include <cmath>
#include <cstdio>

#include "nlohmann/json.hpp"

namespace stm32_audio::host {

namespace {

using nlohmann::json;

std::optional<double> number(const json& object, const char* key)
{
    const auto it = object.find(key);
    if (it == object.end() || !it->is_number()) {
        return std::nullopt;
    }
    return it->get<double>();
}

std::optional<double> numberAt(const json& array, std::size_t i)
{
    if (!array.is_array() || i >= array.size() || !array[i].is_number()) {
        return std::nullopt;
    }
    return array[i].get<double>();
}

int toInt(double value)
{
    return static_cast<int>(std::lround(value));
}

// "2026-10-21" -> year, month, day.
bool parseDate(const json& value, WeatherDay& day)
{
    if (!value.is_string()) {
        return false;
    }
    const std::string& text = value.get_ref<const std::string&>();
    int y = 0;
    int m = 0;
    int d = 0;
    if (text.size() != 10U || std::sscanf(text.c_str(), "%4d-%2d-%2d", &y, &m, &d) != 3 || m < 1 || m > 12 ||
        d < 1 || d > 31) {
        return false;
    }
    day.year = y;
    day.month = m;
    day.day = d;
    return true;
}

struct CodeText {
    int code;
    const char* tr;
    const char* en;
};

constexpr CodeText kCodeTexts[] = {
    {0, "Açık", "Clear"},
    {1, "Az bulutlu", "Mainly clear"},
    {2, "Parçalı bulutlu", "Partly cloudy"},
    {3, "Kapalı", "Overcast"},
    {45, "Sisli", "Fog"},
    {48, "Kırağılı sis", "Rime fog"},
    {51, "Hafif çisenti", "Light drizzle"},
    {53, "Çisenti", "Drizzle"},
    {55, "Yoğun çisenti", "Dense drizzle"},
    {56, "Donan çisenti", "Freezing drizzle"},
    {57, "Donan çisenti", "Freezing drizzle"},
    {61, "Hafif yağmur", "Light rain"},
    {63, "Yağmur", "Rain"},
    {65, "Şiddetli yağmur", "Heavy rain"},
    {66, "Donan yağmur", "Freezing rain"},
    {67, "Donan yağmur", "Freezing rain"},
    {71, "Hafif kar", "Light snow"},
    {73, "Kar", "Snow"},
    {75, "Yoğun kar", "Heavy snow"},
    {77, "Kar taneleri", "Snow grains"},
    {80, "Hafif sağanak", "Light showers"},
    {81, "Sağanak", "Showers"},
    {82, "Şiddetli sağanak", "Violent showers"},
    {85, "Hafif kar sağanağı", "Light snow showers"},
    {86, "Kar sağanağı", "Snow showers"},
    {95, "Gök gürültülü fırtına", "Thunderstorm"},
    {96, "Dolulu fırtına", "Thunderstorm, hail"},
    {99, "Dolulu fırtına", "Thunderstorm, hail"},
};

}  // namespace

std::string forecastUrl(double latitude, double longitude)
{
    char buffer[384];
    std::snprintf(buffer, sizeof(buffer),
                  "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
                  "&current=temperature_2m,apparent_temperature,relative_humidity_2m,weather_code,"
                  "wind_speed_10m,is_day"
                  "&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max"
                  "&timezone=auto&forecast_days=%d",
                  latitude, longitude, kForecastDays);
    return buffer;
}

std::optional<WeatherReport> parseForecast(const std::string& text)
{
    const json root = json::parse(text, nullptr, false);
    if (!root.is_object()) {
        return std::nullopt;
    }
    const auto current = root.find("current");
    if (current == root.end() || !current->is_object()) {
        return std::nullopt;
    }
    const std::optional<double> temperature = number(*current, "temperature_2m");
    const std::optional<double> code = number(*current, "weather_code");
    if (!temperature || !code) {
        return std::nullopt;
    }

    WeatherReport report;
    report.now.temperatureC = *temperature;
    report.now.code = toInt(*code);
    report.now.feelsLikeC = number(*current, "apparent_temperature").value_or(*temperature);
    report.now.humidityPercent = toInt(number(*current, "relative_humidity_2m").value_or(0.0));
    report.now.windKmh = number(*current, "wind_speed_10m").value_or(0.0);
    report.now.isDay = number(*current, "is_day").value_or(1.0) != 0.0;

    const auto daily = root.find("daily");
    if (daily == root.end() || !daily->is_object()) {
        return report;
    }
    const auto times = daily->find("time");
    if (times == daily->end() || !times->is_array()) {
        return report;
    }
    const json empty = json::array();
    auto column = [&](const char* key) -> const json& {
        const auto it = daily->find(key);
        return (it != daily->end() && it->is_array()) ? *it : empty;
    };
    const json& codes = column("weather_code");
    const json& maxima = column("temperature_2m_max");
    const json& minima = column("temperature_2m_min");
    const json& precipitation = column("precipitation_probability_max");

    for (std::size_t i = 0; i < times->size(); ++i) {
        WeatherDay day;
        const std::optional<double> dayCode = numberAt(codes, i);
        const std::optional<double> maxC = numberAt(maxima, i);
        const std::optional<double> minC = numberAt(minima, i);
        if (!parseDate((*times)[i], day) || !dayCode || !maxC || !minC) {
            continue;
        }
        day.code = toInt(*dayCode);
        day.maxC = *maxC;
        day.minC = *minC;
        if (const std::optional<double> p = numberAt(precipitation, i)) {
            day.precipitationPercent = toInt(*p);
        }
        report.days.push_back(day);
    }
    return report;
}

int weatherIcon(int code, bool isDay)
{
    switch (code) {
    case 0:
        return isDay ? 1 : 2;
    case 1:
    case 2:
        return isDay ? 3 : 4;
    case 3:
        return 5;
    case 45:
    case 48:
        return 6;
    case 51:
    case 53:
    case 55:
    case 56:
    case 57:
    case 61:
    case 63:
    case 80:
    case 81:
        return 7;
    case 65:
    case 66:
    case 67:
    case 82:
        return 8;
    case 71:
    case 73:
    case 75:
    case 77:
    case 85:
    case 86:
        return 9;
    case 95:
    case 96:
    case 99:
        return 10;
    default:
        return 0;
    }
}

std::string weatherText(int code, std::uint8_t language)
{
    for (const CodeText& entry : kCodeTexts) {
        if (entry.code == code) {
            return language == 1U ? entry.en : entry.tr;
        }
    }
    return {};
}

std::string formatDegrees(double celsius)
{
    // lround(-0.4) is 0, so there is no "-0".
    return std::to_string(std::lround(celsius)) + "\xC2\xB0";
}

}  // namespace stm32_audio::host
