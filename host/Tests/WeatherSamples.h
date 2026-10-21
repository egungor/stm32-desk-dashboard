// Open-Meteo answers for the weather tests, written by hand in the format
// of the API documentation (https://open-meteo.com/en/docs and
// /en/docs/geocoding-api) for the URLs forecastUrl() and geocodingUrl()
// build. Not recorded from the live API.
#pragma once

namespace weather_samples {

inline const char* const kForecast = R"({
  "latitude": 41.0,
  "longitude": 28.9375,
  "generationtime_ms": 0.0820159912109375,
  "utc_offset_seconds": 10800,
  "timezone": "Europe/Istanbul",
  "timezone_abbreviation": "GMT+3",
  "elevation": 39.0,
  "current_units": {
    "time": "iso8601", "interval": "seconds", "temperature_2m": "°C", "apparent_temperature": "°C",
    "relative_humidity_2m": "%", "weather_code": "wmo code", "wind_speed_10m": "km/h", "is_day": ""
  },
  "current": {
    "time": "2026-10-21T14:00", "interval": 900, "temperature_2m": 18.4, "apparent_temperature": 16.2,
    "relative_humidity_2m": 65, "weather_code": 2, "wind_speed_10m": 12.3, "is_day": 1
  },
  "daily_units": {
    "time": "iso8601", "weather_code": "wmo code", "temperature_2m_max": "°C", "temperature_2m_min": "°C",
    "precipitation_probability_max": "%"
  },
  "daily": {
    "time": ["2026-10-21", "2026-10-22", "2026-10-23", "2026-10-24", "2026-10-25",
             "2026-10-26", "2026-10-27", "2026-10-28", "2026-10-29", "2026-10-30"],
    "weather_code": [2, 61, 3, 0, 80, 95, 45, 71, 1, 3],
    "temperature_2m_max": [21.2, 17.0, 16.4, 19.8, 18.1, 15.5, 14.9, 4.2, 12.6, 13.0],
    "temperature_2m_min": [11.4, 12.6, 10.9, 9.5, 10.2, 9.8, -0.4, -3.6, 5.1, 6.0],
    "precipitation_probability_max": [20, 85, 30, 0, 60, 90, 10, 70, 5, null]
  }
})";

// A day with a missing value is left out; current conditions only with
// the two values the card needs.
inline const char* const kForecastGaps = R"({
  "current": { "time": "2026-10-21T23:00", "temperature_2m": -0.3, "weather_code": 0, "is_day": 0 },
  "daily": {
    "time": ["2026-10-21", "2026-10-22", "bad"],
    "weather_code": [0, null, 3],
    "temperature_2m_max": [5.0, 6.0, 7.0],
    "temperature_2m_min": [-1.0, 0.0, 1.0]
  }
})";

inline const char* const kForecastError = R"({"error": true, "reason": "Latitude must be in range of -90 to 90°."})";

inline const char* const kGeocoding = R"({
  "results": [
    { "id": 745044, "name": "İstanbul", "latitude": 41.01384, "longitude": 28.94966, "elevation": 39.0,
      "feature_code": "PPLA", "country_code": "TR", "admin1_id": 745042, "timezone": "Europe/Istanbul",
      "population": 14804116, "country_id": 298795, "country": "Türkiye", "admin1": "İstanbul" },
    { "id": 1, "name": "Istanbul", "latitude": 40.9, "longitude": -73.1, "country": "United States" },
    { "id": 2, "name": "broken" }
  ],
  "generationtime_ms": 0.5
})";

}  // namespace weather_samples
