#include "WeatherLocation.h"

#include <cstdio>
#include <cstdlib>

#include "nlohmann/json.hpp"

namespace stm32_audio::host {

namespace {

std::string trim(const std::string& text)
{
    const std::size_t first = text.find_first_not_of(" \t\r");
    if (first == std::string::npos) {
        return {};
    }
    const std::size_t last = text.find_last_not_of(" \t\r");
    return text.substr(first, last - first + 1U);
}

std::optional<double> parseNumber(const std::string& text)
{
    if (text.empty()) {
        return std::nullopt;
    }
    char* end = nullptr;
    const double value = std::strtod(text.c_str(), &end);
    if (end == nullptr || *end != '\0') {
        return std::nullopt;
    }
    return value;
}

}  // namespace

std::optional<WeatherLocation> parseLocationFile(const std::string& text)
{
    WeatherLocation location;
    std::optional<double> latitude;
    std::optional<double> longitude;
    // Notepad may save UTF-8 with a byte order mark.
    std::size_t start = (text.compare(0, 3, "\xEF\xBB\xBF") == 0) ? 3U : 0U;

    while (start <= text.size()) {
        std::size_t end = text.find('\n', start);
        if (end == std::string::npos) {
            end = text.size();
        }
        const std::string line = trim(text.substr(start, end - start));
        start = end + 1U;
        if (line.empty() || line[0] == '#') {
            continue;
        }
        const std::size_t eq = line.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        const std::string key = trim(line.substr(0, eq));
        const std::string value = trim(line.substr(eq + 1U));
        if (key == "name") {
            location.name = value;
        } else if (key == "latitude") {
            latitude = parseNumber(value);
        } else if (key == "longitude") {
            longitude = parseNumber(value);
        }
    }
    if (!latitude || !longitude || *latitude < -90.0 || *latitude > 90.0 || *longitude < -180.0 ||
        *longitude > 180.0) {
        return std::nullopt;
    }
    location.latitude = *latitude;
    location.longitude = *longitude;
    return location;
}

std::string formatLocationFile(const WeatherLocation& location)
{
    char numbers[96];
    std::snprintf(numbers, sizeof(numbers), "latitude=%.4f\nlongitude=%.4f\n", location.latitude,
                  location.longitude);
    std::string name = location.name;
    for (char& c : name) {
        if (c == '\n' || c == '\r') {
            c = ' ';
        }
    }
    return "# stm32-desk-dashboard weather location\nname=" + name + "\n" + numbers;
}

std::string urlEncode(const std::string& text)
{
    static const char kHex[] = "0123456789ABCDEF";
    std::string out;
    for (const char c : text) {
        const auto b = static_cast<unsigned char>(c);
        if ((b >= 'A' && b <= 'Z') || (b >= 'a' && b <= 'z') || (b >= '0' && b <= '9') || b == '-' || b == '.' ||
            b == '_' || b == '~') {
            out += c;
        } else {
            out += '%';
            out += kHex[b >> 4U];
            out += kHex[b & 0x0FU];
        }
    }
    return out;
}

std::string geocodingUrl(const std::string& name, std::uint8_t language)
{
    return "https://geocoding-api.open-meteo.com/v1/search?name=" + urlEncode(name) +
           "&count=5&format=json&language=" + (language == 1U ? "en" : "tr");
}

std::vector<Place> parseGeocoding(const std::string& text)
{
    using nlohmann::json;
    std::vector<Place> places;
    const json root = json::parse(text, nullptr, false);
    if (!root.is_object()) {
        return places;
    }
    const auto results = root.find("results");
    if (results == root.end() || !results->is_array()) {
        return places;
    }
    for (const json& r : *results) {
        if (!r.is_object()) {
            continue;
        }
        const auto name = r.find("name");
        const auto lat = r.find("latitude");
        const auto lon = r.find("longitude");
        if (name == r.end() || !name->is_string() || lat == r.end() || !lat->is_number() || lon == r.end() ||
            !lon->is_number()) {
            continue;
        }
        Place place;
        place.location.name = name->get<std::string>();
        place.location.latitude = lat->get<double>();
        place.location.longitude = lon->get<double>();
        place.description = place.location.name;
        for (const char* key : {"admin1", "country"}) {
            const auto it = r.find(key);
            if (it != r.end() && it->is_string() && !it->get_ref<const std::string&>().empty()) {
                place.description += ", " + it->get<std::string>();
            }
        }
        places.push_back(std::move(place));
    }
    return places;
}

}  // namespace stm32_audio::host
