#include "WeatherCard.h"

#include <cstdio>

#include "DashController.h"

namespace stm32_audio::host {

namespace {

struct Texts {
    const char* title;
    const char* noLocation;
    const char* howToSet;
    const char* waiting;
    const char* unavailable;
    const char* rain;  // printf: percent
    const char* speed;  // printf: km/h
    const char* updated;
    const char* now;
    const char* feelsLike;
    const char* humidity;
    const char* windLabel;
    const char* days;  // printf: count
    const char* today;
    const char* tomorrow;
    const char* weekdays[7];  // Sunday first
};

constexpr Texts kTurkish = {
    "Hava durumu", "Konum ayarlanmadı", "dashboard-host weather <şehir>", "Bekleniyor...",
    "Hava durumu alınamadı", "Yağış %%%d", "%d km/s", "Güncellendi", "Şimdi", "Hissedilen", "Nem",
    "Rüzgâr", "%d gün", "Bugün", "Yarın", {"Paz", "Pzt", "Sal", "Çar", "Per", "Cum", "Cmt"},
};

constexpr Texts kEnglish = {
    "Weather", "No location set", "dashboard-host weather <city>", "Waiting...", "Weather unavailable",
    "Rain %d%%", "%d km/h", "Updated", "Now", "Feels like", "Humidity", "Wind", "%d days", "Today",
    "Tomorrow", {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"},
};

const Texts& texts(std::uint8_t language)
{
    return language == 1U ? kEnglish : kTurkish;
}

std::string format(const char* pattern, int value)
{
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), pattern, value);
    return buffer;
}

std::string hhmm(std::uint32_t localTime)
{
    char buffer[8];
    std::snprintf(buffer, sizeof(buffer), "%02u:%02u", static_cast<unsigned>((localTime / 3600U) % 24U),
                  static_cast<unsigned>((localTime / 60U) % 60U));
    return buffer;
}

std::string percent(std::uint8_t language, int value)
{
    return language == 1U ? std::to_string(value) + "%" : "%" + std::to_string(value);
}

// Days since 1970-01-01 of a civil date (Howard Hinnant's algorithm).
long daysFromCivil(int y, int m, int d)
{
    y -= (m <= 2) ? 1 : 0;
    const long era = (y >= 0 ? y : y - 399) / 400;
    const long yoe = y - era * 400;
    const long doy = (153L * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

long dayNumber(const WeatherDay& day)
{
    return daysFromCivil(day.year, day.month, day.day);
}

std::string range(const WeatherDay& day)
{
    return formatDegrees(day.minC) + " / " + formatDegrees(day.maxC);
}

std::string iconField(int icon)
{
    return std::to_string(icon);
}

}  // namespace

void WeatherCard::setLocation(std::optional<WeatherLocation> place)
{
    const bool same = place.has_value() == location_.has_value() &&
                      (!place || (place->latitude == location_->latitude && place->longitude == location_->longitude));
    if (place && location_) {
        location_->name = place->name;  // a new name alone needs no fetch
    }
    if (same) {
        return;
    }
    location_ = std::move(place);
    report_.reset();
    stale_ = false;
    fetching_ = false;  // an answer still on its way is for the old place
    nextFetchMs_.reset();
}

std::optional<std::string> WeatherCard::fetchDue(std::uint64_t nowMs)
{
    if (!location_ || fetching_ || (nextFetchMs_ && nowMs < *nextFetchMs_)) {
        return std::nullopt;
    }
    fetching_ = true;
    return forecastUrl(location_->latitude, location_->longitude);
}

bool WeatherCard::onFetched(std::uint64_t nowMs, std::uint32_t localTime, const std::string& url,
                            const std::optional<std::string>& body)
{
    if (!location_ || url != forecastUrl(location_->latitude, location_->longitude)) {
        return false;
    }
    fetching_ = false;
    std::optional<WeatherReport> parsed;
    if (body) {
        parsed = parseForecast(*body);
    }
    if (!parsed) {
        nextFetchMs_ = nowMs + kRetryMs;
        const bool changed = !stale_;
        stale_ = true;
        return changed;
    }
    nextFetchMs_ = nowMs + kRefreshMs;
    report_ = std::move(parsed);
    updatedAt_ = localTime;
    stale_ = false;
    return true;
}

const char* weatherColour(int icon)
{
    switch (icon) {
    case 1:
        return "E59B2E";  // clear, day
    case 2:
        return "2B3A67";  // clear, night
    case 3:
        return "3D86C6";  // partly cloudy, day
    case 4:
        return "3A4A70";  // partly cloudy, night
    case 5:
        return "66717F";  // overcast
    case 6:
        return "7D838C";  // fog
    case 7:
        return "3A6EA5";  // drizzle, rain
    case 8:
        return "2D4B7A";  // heavy rain
    case 9:
        return "5F8FBF";  // snow
    case 10:
        return "4A3B6B";  // thunderstorm
    default:
        return "";
    }
}

std::string WeatherCard::card(std::uint32_t localTime) const
{
    const Texts& t = texts(language_);

    if (!location_) {
        return recordLine({"0", t.title, ""}) + "\n" + t.noLocation + "\n" + t.howToSet;
    }
    const std::string place = location_->name.empty() ? std::string(t.title) : location_->name;
    if (!report_) {
        return recordLine({"0", place, ""}) + "\n" + (stale_ ? t.unavailable : t.waiting);
    }

    const WeatherNow& now = report_->now;
    const int icon = weatherIcon(now.code, now.isDay);
    std::string text = recordLine({iconField(icon), place, "", weatherColour(icon), "L"});
    text += "\n" + formatDegrees(now.temperatureC);

    // Today's line: the forecast day of the local date, else the first one.
    const long today = static_cast<long>(localTime / 86400U);
    const WeatherDay* day = report_->days.empty() ? nullptr : &report_->days.front();
    for (const WeatherDay& d : report_->days) {
        if (dayNumber(d) == today) {
            day = &d;
        }
    }
    if (day != nullptr) {
        text += "\n" + range(*day);
    }
    if (stale_) {
        text += "\n" + std::string(t.updated) + " " + hhmm(updatedAt_);
    }
    return text;
}

std::optional<std::string> WeatherCard::detail(std::uint32_t localTime) const
{
    if (!report_ || !location_) {
        return std::nullopt;
    }
    const Texts& t = texts(language_);
    const WeatherNow& now = report_->now;
    std::string subtitle = location_->name.empty() ? std::string() : location_->name + " \xC2\xB7 ";
    subtitle += stale_ ? std::string(t.updated) + " " + hhmm(updatedAt_) : hhmm(updatedAt_);

    std::string text = recordLine({t.title, subtitle, ""});
    text += "\n#" + std::string(t.now);
    text += "\n" + recordLine({iconField(weatherIcon(now.code, now.isDay)), formatDegrees(now.temperatureC),
                               weatherText(now.code, language_)});
    text += "\n" + recordLine({"0", t.feelsLike, formatDegrees(now.feelsLikeC)});
    text += "\n" + recordLine({"0", t.humidity, percent(language_, now.humidityPercent)});
    text += "\n" + recordLine({"0", t.windLabel, format(t.speed, static_cast<int>(now.windKmh + 0.5))});

    const long today = static_cast<long>(localTime / 86400U);
    std::string rows;
    int shown = 0;
    for (const WeatherDay& d : report_->days) {
        const long n = dayNumber(d);
        if (n < today) {
            continue;  // an old report after midnight
        }
        std::string label;
        if (n == today) {
            label = t.today;
        } else if (n == today + 1) {
            label = t.tomorrow;
        } else {
            label = std::string(t.weekdays[static_cast<std::size_t>(((n % 7) + 11) % 7)]) + " " +
                    std::to_string(d.day);
        }
        std::string value = range(d);
        if (d.precipitationPercent) {
            value += " \xC2\xB7 " + format(t.rain, *d.precipitationPercent);
        }
        rows += "\n" + recordLine({iconField(weatherIcon(d.code, true)), label, value});
        ++shown;
    }
    if (shown > 0) {
        text += "\n#" + format(t.days, shown) + rows;
    }
    return text;
}

}  // namespace stm32_audio::host
