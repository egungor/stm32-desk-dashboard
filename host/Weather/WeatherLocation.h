// The one fixed weather location (decision of 2026-10-21): kept in
// %APPDATA%\stm32-desk-dashboard\weather.txt, never in the repository.
//
//   # stm32-desk-dashboard weather location
//   name=İstanbul
//   latitude=41.0138
//   longitude=28.9497
//
// "dashboard-host weather <city>" finds a place with the Open-Meteo
// geocoding API and writes the file. Platform independent.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace stm32_audio::host {

struct WeatherLocation {
    std::string name;  // shown on the card; may be empty
    double latitude = 0.0;
    double longitude = 0.0;
};

// nullopt if latitude or longitude is missing or out of range. Unknown
// keys, blank lines and "#" comments are ignored.
std::optional<WeatherLocation> parseLocationFile(const std::string& text);
std::string formatLocationFile(const WeatherLocation& location);

// A place found by the geocoding API, with where it is for the user to
// choose: "İstanbul, Istanbul, Türkiye".
struct Place {
    WeatherLocation location;
    std::string description;
};

// Geocoding search URL (https://open-meteo.com/en/docs/geocoding-api);
// language as the settings record (0 Turkish, 1 English).
std::string geocodingUrl(const std::string& name, std::uint8_t language);

// The places of a geocoding answer; empty if none or not an answer.
std::vector<Place> parseGeocoding(const std::string& json);

// Percent-encode a UTF-8 string for a URL query value.
std::string urlEncode(const std::string& text);

}  // namespace stm32_audio::host
