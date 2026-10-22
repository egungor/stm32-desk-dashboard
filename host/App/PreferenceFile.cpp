#include "PreferenceFile.h"

#include <cstdint>
#include <map>
#include <sstream>
#include <utility>
#include <vector>

namespace stm32_audio::host {

namespace {

constexpr const char* kHeader = "# stm32-desk-dashboard preferences";
constexpr const char* kVersion = "1";

// Settings record fields by name, in protocol.md order.
struct SettingField {
    const char* name;
    std::uint8_t ProtoSettings::*field;
};

const SettingField kSettingFields[] = {
    {"layout", &ProtoSettings::layout},
    {"theme", &ProtoSettings::theme},
    {"accent", &ProtoSettings::accent},
    {"grid", &ProtoSettings::grid},
    {"startup_tab", &ProtoSettings::startup_tab},
    {"last_tab", &ProtoSettings::last_tab},
    {"screen_timeout", &ProtoSettings::screen_timeout},
    {"flipped", &ProtoSettings::flipped},
    {"language", &ProtoSettings::language},
    {"volume_limit_on", &ProtoSettings::volume_limit_on},
    {"volume_limit", &ProtoSettings::volume_limit},
    {"startup_screen", &ProtoSettings::startup_screen},
};

std::string escape(const std::string& field)
{
    std::string out;
    for (char c : field) {
        switch (c) {
        case '\\':
            out += "\\\\";
            break;
        case '\t':
            out += "\\t";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\n':
            out += "\\n";
            break;
        default:
            out += c;
            break;
        }
    }
    return out;
}

std::string unescape(const std::string& field)
{
    std::string out;
    for (std::size_t i = 0; i < field.size(); ++i) {
        if (field[i] != '\\' || i + 1 == field.size()) {
            out += field[i];
            continue;
        }
        const char next = field[++i];
        out += next == 't' ? '\t' : next == 'r' ? '\r' : next == 'n' ? '\n' : next;
    }
    return out;
}

std::vector<std::string> splitLine(const std::string& line)
{
    std::vector<std::string> fields;
    std::string current;
    for (char c : line) {
        if (c == '\t') {
            fields.push_back(unescape(current));
            current.clear();
        } else {
            current += c;
        }
    }
    fields.push_back(unescape(current));
    return fields;
}

// 0..255 written in decimal; nullopt otherwise.
std::optional<std::uint8_t> parseByte(const std::string& text)
{
    if (text.empty() || text.size() > 3) {
        return std::nullopt;
    }
    unsigned value = 0;
    for (char c : text) {
        if (c < '0' || c > '9') {
            return std::nullopt;
        }
        value = value * 10U + static_cast<unsigned>(c - '0');
    }
    if (value > 255U) {
        return std::nullopt;
    }
    return static_cast<std::uint8_t>(value);
}

}  // namespace

std::vector<std::pair<std::string, unsigned>> settingValues(const ProtoSettings& settings)
{
    std::vector<std::pair<std::string, unsigned>> values;
    for (const SettingField& f : kSettingFields) {
        values.emplace_back(f.name, settings.*f.field);
    }
    return values;
}

bool setSettingValue(ProtoSettings& settings, const std::string& name, unsigned value)
{
    if (value > 255U) {
        return false;
    }
    for (const SettingField& f : kSettingFields) {
        if (name == f.name) {
            settings.*f.field = static_cast<std::uint8_t>(value);
            return true;
        }
    }
    return false;
}

std::string serializePreferences(const Preferences& prefs)
{
    std::ostringstream out;
    out << kHeader << '\n' << "version\t" << kVersion << '\n';

    const ProtoSettings& s = prefs.settings();
    for (const SettingField& f : kSettingFields) {
        out << "setting\t" << f.name << '\t' << static_cast<unsigned>(s.*(f.field)) << '\n';
    }
    for (const auto& [id, d] : prefs.devices()) {
        out << "device\t" << escape(id) << "\tfavorite\t" << (d.favorite ? 1 : 0) << "\talias\t" << escape(d.alias)
            << '\n';
    }
    for (const std::string& id : prefs.order()) {
        out << "order\t" << escape(id) << '\n';
    }
    return out.str();
}

bool parsePreferences(const std::string& text, Preferences& prefs)
{
    std::istringstream in(text);
    std::string line;
    bool versionSeen = false;

    ProtoSettings settings{};
    Proto_DefaultSettings(&settings);
    std::map<std::string, DevicePreferences> devices;
    std::vector<std::string> order;

    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();  // edited on Windows
        }
        if (line.empty() || line[0] == '#') {
            continue;
        }
        const std::vector<std::string> f = splitLine(line);
        const std::string& kind = f[0];

        if (kind == "version") {
            if (f.size() < 2 || f[1] != kVersion) {
                return false;  // unknown version: do not guess
            }
            versionSeen = true;
        } else if (kind == "setting" && f.size() >= 3) {
            const std::optional<std::uint8_t> value = parseByte(f[2]);
            for (const SettingField& sf : kSettingFields) {
                if (value && f[1] == sf.name) {
                    ProtoSettings candidate = settings;
                    candidate.*(sf.field) = *value;
                    if (Proto_SettingsValid(&candidate) != 0) {
                        settings = candidate;  // out-of-range values keep the default
                    }
                }
            }
        } else if (kind == "device" && f.size() >= 2 && !f[1].empty()) {
            DevicePreferences d;
            for (std::size_t i = 2; i + 1 < f.size(); i += 2) {
                if (f[i] == "favorite") {
                    d.favorite = f[i + 1] == "1";
                } else if (f[i] == "alias") {
                    d.alias = f[i + 1];
                }
            }
            devices[f[1]] = d;
        } else if (kind == "order" && f.size() >= 2 && !f[1].empty()) {
            order.push_back(f[1]);
        }
        // anything else: ignored
    }
    if (!versionSeen) {
        return false;
    }

    Preferences loaded;
    (void)loaded.setSettings(settings);
    loaded.setDevices(std::move(devices));
    loaded.setOrder(std::move(order));
    prefs = std::move(loaded);
    return true;
}

}  // namespace stm32_audio::host
