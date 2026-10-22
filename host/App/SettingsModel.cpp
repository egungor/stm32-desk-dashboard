#include "SettingsModel.h"

#include <cmath>
#include <set>

#include "PreferenceFile.h"
#include "nlohmann/json.hpp"

namespace stm32_audio::host {

using nlohmann::json;

namespace {

bool sameSettings(const ProtoSettings& a, const ProtoSettings& b)
{
    return settingValues(a) == settingValues(b);
}

json deviceJson(const SettingsDevice& d)
{
    return json{{"id", d.id}, {"name", d.name}, {"alias", d.alias}, {"favorite", d.favorite}};
}

// A string member, or nullopt if missing or not a string.
std::optional<std::string> stringOf(const json& j, const char* key)
{
    const auto it = j.find(key);
    if (it == j.end() || !it->is_string()) {
        return std::nullopt;
    }
    return it->get<std::string>();
}

// An unsigned member (a whole number 0..255), or nullopt.
std::optional<unsigned> smallNumberOf(const json& j, const char* key)
{
    const auto it = j.find(key);
    if (it == j.end() || !it->is_number_integer()) {
        return std::nullopt;
    }
    const long long n = it->get<long long>();
    if (n < 0 || n > 255) {
        return std::nullopt;
    }
    return static_cast<unsigned>(n);
}

std::optional<double> numberOf(const json& j, const char* key)
{
    const auto it = j.find(key);
    if (it == j.end() || !it->is_number()) {
        return std::nullopt;
    }
    const double d = it->get<double>();
    return std::isfinite(d) ? std::optional<double>(d) : std::nullopt;
}

}  // namespace

bool ServiceSnapshot::operator==(const ServiceSnapshot& o) const
{
    if (connected != o.connected || board.firmware != o.board.firmware || board.protocol != o.board.protocol ||
        !sameSettings(settings, o.settings) || devices.size() != o.devices.size()) {
        return false;
    }
    for (std::size_t i = 0; i < devices.size(); ++i) {
        const SettingsDevice& a = devices[i];
        const SettingsDevice& b = o.devices[i];
        if (a.id != b.id || a.name != b.name || a.alias != b.alias || a.favorite != b.favorite) {
            return false;
        }
    }
    return true;
}

std::string settingsStateJson(const SettingsState& s)
{
    json settings = json::object();
    for (const auto& [name, value] : settingValues(s.service.settings)) {
        settings[name] = value;
    }
    json devices = json::array();
    for (const SettingsDevice& d : s.service.devices) {
        devices.push_back(deviceJson(d));
    }
    json weather = nullptr;
    if (s.weather) {
        weather = json{{"name", s.weather->name}, {"latitude", s.weather->latitude}, {"longitude", s.weather->longitude}};
    }
    const json out{
        {"type", "state"},
        {"hostVersion", s.hostVersion},
        {"hostProtocol", PROTO_VERSION},
        {"connected", s.service.connected},
        {"firmware", s.service.board.firmware},
        {"boardProtocol", s.service.board.protocol},
        {"settings", settings},
        {"devices", devices},
        {"weather", weather},
        {"picture", s.picture},
        {"foodLists", s.foodLists},
        {"claudeKeySet", s.claudeKeySet},
        {"claudeModel", s.claudeModel},
        {"startup", s.startup},
        {"importing", s.importing},
        {"folder", s.folder},
    };
    // Bad UTF-8 (a path from an old file) is replaced, not thrown.
    return out.dump(-1, ' ', false, json::error_handler_t::replace);
}

std::string placesJson(const std::vector<Place>& places)
{
    json list = json::array();
    for (const Place& p : places) {
        list.push_back(json{{"name", p.location.name},
                            {"latitude", p.location.latitude},
                            {"longitude", p.location.longitude},
                            {"description", p.description}});
    }
    return json{{"type", "places"}, {"places", list}}.dump(-1, ' ', false, json::error_handler_t::replace);
}

std::string noticeJson(bool ok, const std::string& text)
{
    return json{{"type", "notice"}, {"ok", ok}, {"text", text}}.dump(-1, ' ', false, json::error_handler_t::replace);
}

std::optional<SettingsRequest> parseSettingsRequest(const std::string& text)
{
    const json j = json::parse(text, nullptr, false);
    if (j.is_discarded() || !j.is_object()) {
        return std::nullopt;
    }
    const std::optional<std::string> cmd = stringOf(j, "cmd");
    if (!cmd) {
        return std::nullopt;
    }
    SettingsRequest r;
    const std::string& c = *cmd;
    if (c == "get") {
        r.kind = SettingsRequestKind::Get;
    } else if (c == "setting") {
        const std::optional<std::string> name = stringOf(j, "name");
        const std::optional<unsigned> value = smallNumberOf(j, "value");
        ProtoSettings probe{};
        if (!name || !value || !setSettingValue(probe, *name, *value) || Proto_SettingsValid(&probe) == 0) {
            return std::nullopt;  // unknown field, or out of its range
        }
        r.kind = SettingsRequestKind::Setting;
        r.name = *name;
        r.value = *value;
    } else if (c == "alias") {
        const std::optional<std::string> id = stringOf(j, "id");
        const std::optional<std::string> alias = stringOf(j, "alias");
        if (!id || id->empty() || !alias) {
            return std::nullopt;
        }
        r.kind = SettingsRequestKind::Alias;
        r.name = *id;
        r.text = *alias;
    } else if (c == "favorite") {
        const std::optional<std::string> id = stringOf(j, "id");
        const std::optional<unsigned> value = smallNumberOf(j, "value");
        if (!id || id->empty() || !value || *value > 1U) {
            return std::nullopt;
        }
        r.kind = SettingsRequestKind::Favorite;
        r.name = *id;
        r.value = *value;
    } else if (c == "order") {
        const auto it = j.find("ids");
        if (it == j.end() || !it->is_array() || it->empty()) {
            return std::nullopt;
        }
        std::set<std::string> seen;
        for (const json& id : *it) {
            if (!id.is_string() || id.get<std::string>().empty() || !seen.insert(id.get<std::string>()).second) {
                return std::nullopt;
            }
            r.ids.push_back(id.get<std::string>());
        }
        r.kind = SettingsRequestKind::Order;
    } else if (c == "weatherSearch") {
        const std::optional<std::string> query = stringOf(j, "query");
        if (!query || query->empty() || query->size() > 100U) {
            return std::nullopt;
        }
        r.kind = SettingsRequestKind::WeatherSearch;
        r.text = *query;
    } else if (c == "weatherSet") {
        const std::optional<std::string> name = stringOf(j, "name");
        const std::optional<double> lat = numberOf(j, "latitude");
        const std::optional<double> lon = numberOf(j, "longitude");
        if (!name || !lat || !lon) {
            return std::nullopt;
        }
        r.location.name = *name;
        r.location.latitude = *lat;
        r.location.longitude = *lon;
        // The file's own rule for valid coordinates.
        if (!parseLocationFile(formatLocationFile(r.location))) {
            return std::nullopt;
        }
        r.kind = SettingsRequestKind::WeatherSet;
    } else if (c == "foodLists") {
        const std::optional<std::string> lists = stringOf(j, "text");
        if (!lists || lists->size() > 64U * 1024U) {
            return std::nullopt;
        }
        r.kind = SettingsRequestKind::FoodLists;
        r.text = *lists;
    } else if (c == "claudeKey") {
        const std::optional<std::string> key = stringOf(j, "key");
        if (!key || key->size() > 256U || key->find_first_of("\r\n") != std::string::npos) {
            return std::nullopt;
        }
        r.kind = SettingsRequestKind::ClaudeKey;
        r.text = *key;
    } else if (c == "startup") {
        const std::optional<unsigned> value = smallNumberOf(j, "value");
        if (!value || *value > 1U) {
            return std::nullopt;
        }
        r.kind = SettingsRequestKind::Startup;
        r.value = *value;
    } else if (c == "pictureChoose") {
        r.kind = SettingsRequestKind::PictureChoose;
    } else if (c == "pictureClear") {
        r.kind = SettingsRequestKind::PictureClear;
    } else if (c == "menuImport") {
        r.kind = SettingsRequestKind::MenuImport;
    } else if (c == "copyHook") {
        r.kind = SettingsRequestKind::CopyHook;
    } else if (c == "openFolder") {
        r.kind = SettingsRequestKind::OpenFolder;
    } else if (c == "openLog") {
        r.kind = SettingsRequestKind::OpenLog;
    } else {
        return std::nullopt;
    }
    return r;
}

bool applySettingsRequest(const SettingsRequest& r, Preferences& prefs)
{
    switch (r.kind) {
    case SettingsRequestKind::Setting: {
        ProtoSettings s = prefs.settings();
        if (!setSettingValue(s, r.name, r.value)) {
            return false;
        }
        return prefs.setSettings(s);
    }
    case SettingsRequestKind::Alias:
        return prefs.setAlias(r.name, r.text);
    case SettingsRequestKind::Favorite:
        return prefs.setFavorite(r.name, r.value != 0U);
    case SettingsRequestKind::Order: {
        // The shown devices in the new order; the stored ones not shown
        // (unplugged) keep their place after them.
        std::vector<std::string> order = r.ids;
        const std::set<std::string> moved(r.ids.begin(), r.ids.end());
        for (const std::string& id : prefs.order()) {
            if (moved.count(id) == 0U) {
                order.push_back(id);
            }
        }
        if (order == prefs.order()) {
            return false;
        }
        prefs.setOrder(std::move(order));
        return true;
    }
    default:
        return false;
    }
}

std::string claudeFileWithKey(const std::optional<std::string>& existing, const std::string& apiKey)
{
    std::string out;
    bool keyWritten = false;
    if (existing) {
        std::size_t start = 0;
        while (start < existing->size()) {
            std::size_t end = existing->find('\n', start);
            if (end == std::string::npos) {
                end = existing->size();
            }
            std::string line = existing->substr(start, end - start);
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            start = end + 1U;
            if (line.rfind("api_key=", 0) == 0) {
                if (!apiKey.empty() && !keyWritten) {
                    out += "api_key=" + apiKey + "\n";
                    keyWritten = true;
                }
                continue;
            }
            out += line + "\n";
        }
    }
    if (!apiKey.empty() && !keyWritten) {
        out = "api_key=" + apiKey + "\n" + out;
    }
    return out;
}

}  // namespace stm32_audio::host
