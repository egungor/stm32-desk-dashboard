#include "WeatherService.h"

namespace stm32_audio::host {

WeatherService::WeatherService(DashController& dash, IPreferenceFile& file, IHttpClient& http,
                               DashController::ClockFn clock, LogFn log)
    : dash_(dash), file_(file), fetch_(http), clock_(std::move(clock)), log_(std::move(log))
{
}

void WeatherService::setLanguage(std::uint8_t language)
{
    if (language != language_) {
        language_ = language;
        weather_.setLanguage(language);
        publish();
    }
}

void WeatherService::readLocation()
{
    std::optional<std::string> text = file_.read();
    if (text == fileText_) {
        return;
    }
    fileText_ = std::move(text);
    std::optional<WeatherLocation> location;
    if (fileText_) {
        location = parseLocationFile(*fileText_);
        if (!location && log_) {
            log_("WARN: " + file_.location() + " has no valid latitude and longitude.");
        }
    }
    if (location && log_) {
        log_("Weather location: " + (location->name.empty() ? std::string("(no name)") : location->name));
    }
    weather_.setLocation(std::move(location));
}

void WeatherService::reload()
{
    fileRead_ = true;
    readLocation();
    publish();
}

void WeatherService::onTick(std::uint64_t nowMs)
{
    if (!fileRead_) {
        fileRead_ = true;
        readLocation();
    }
    if (std::optional<BackgroundFetch::Result> result = fetch_.poll()) {
        const bool ok = result->body && parseForecast(*result->body).has_value();
        (void)weather_.onFetched(nowMs, clock_(), result->url, result->body);
        if (!ok && log_) {
            log_(std::string("WARN: weather: ") + (result->body ? "unexpected answer" : "no answer") + " from " +
                 result->url);
        }
    }
    if (!fetch_.busy()) {
        if (const std::optional<std::string> url = weather_.fetchDue(nowMs)) {
            (void)fetch_.start(*url);
        }
    }
    publish();
}

void WeatherService::publish()
{
    const std::uint32_t now = clock_();
    dash_.setCard(kSlot, weather_.card(now));
    std::optional<std::string> text = weather_.detail(now);
    if (text != shownDetail_) {
        shownDetail_ = std::move(text);
        dash_.refreshDetail(kSlot);  // sent only if the board has it open
    }
}

std::optional<std::string> WeatherService::detail(std::int8_t arg) const
{
    if (arg != 0) {
        return std::nullopt;  // no previous / next for the weather
    }
    return weather_.detail(clock_());
}

}  // namespace stm32_audio::host
