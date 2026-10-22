// Text format of the host's preference file (ADR-004, #59).
//
// UTF-8, one record per line, fields separated by TAB; backslash, TAB, CR
// and LF inside a field are written as \\, \t, \r, \n. Lines starting with
// '#' and unknown records are ignored, so newer files stay readable.
//
//   # stm32-desk-dashboard preferences
//   version  1
//   setting  <name>  <value>          one per settings field
//   device   <endpoint id>  favorite  <0|1>  alias  <text>
//   order    <endpoint id>            in the user's order
//
// Platform independent; file access is the caller's (IPreferenceFile).
#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "Preferences.h"

namespace stm32_audio::host {

std::string serializePreferences(const Preferences& prefs);

// The settings record's fields by the file's names, in protocol.md order
// (also the settings window's names, ADR-011).
std::vector<std::pair<std::string, unsigned>> settingValues(const ProtoSettings& settings);
// Set field @p name; false if the name is unknown or @p value > 255 (the
// range of the field is Proto_SettingsValid's).
bool setSettingValue(ProtoSettings& settings, const std::string& name, unsigned value);

// Parse @p text into @p prefs (replaced completely). Returns false, and
// leaves @p prefs unchanged, if the text is not a preference file of a
// known version; a damaged line inside a valid file is skipped.
bool parsePreferences(const std::string& text, Preferences& prefs);

// Where the preferences are read from and written to.
class IPreferenceFile {
public:
    virtual ~IPreferenceFile() = default;
    // Whole file; nullopt if it does not exist or cannot be read.
    virtual std::optional<std::string> read() = 0;
    // Replace the file atomically. Returns false on failure.
    virtual bool write(const std::string& content) = 0;
    // Keep an unreadable file aside before it is overwritten.
    virtual void keepUnreadable(const std::string& content) = 0;
    // For messages.
    virtual std::string location() const = 0;
};

}  // namespace stm32_audio::host
