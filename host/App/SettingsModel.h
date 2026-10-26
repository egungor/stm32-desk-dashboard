// The settings window (ADR-011): the state its page shows, as JSON, and the
// requests the page sends, checked here. Platform independent, so the rules
// are unit tested; the window (WinSettingsWindow) and the tray program only
// carry the strings.
//
// Page -> host, one JSON object per message, "cmd" names the request:
//   get                                   the state, please
//   setting   name, value                 a field of the settings record
//                                         (the names of the preference file)
//   alias     id, alias                   "" removes it
//   favorite  id, value (0/1)
//   order     ids [...]                   the audio devices in the new order
//   weatherSearch  query                  places for a city name
//   weatherSet     name, latitude, longitude
//   picturesAdd                           choose photos and add them (ADR-012)
//   pictureRemove  id | pictureShow  id ("" none) | pictureInclude  id, value
//   pictureCrop    id, x, y, w, h        the part shown (fractions of the photo)
//   pictureShuffle enabled, minutes, random
//   foodLists text                        menu-lists.txt
//   menuNextDay value                     menu-settings.txt's next_day_hour
//                                         (0 never, 1..23)
//   claudeKey key                         claude.txt's api_key ("" removes it)
//   menuImport | startup value | copyHook | openFolder | openLog
//
// Host -> page: {"type":"state",...} (settingsStateJson), {"type":"places",
// "places":[...]}, {"type":"notice","ok":bool,"text":...}.
//
// setting, alias, favorite and order change the preferences the service
// owns, pictureShow the picture it shows: the tray passes them to the
// service loop (HostEvent::SettingsRequest), which applies them
// (applySettingsRequest() for the preferences) and tells the board.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "HidMatch.h"
#include "PictureLibrary.h"
#include "Preferences.h"
#include "WeatherLocation.h"
#include "protocol.h"

namespace stm32_audio::host {

struct SettingsDevice {
    std::string id;
    std::string name;   // Windows' name
    std::string alias;  // "" if none
    bool favorite = false;
};

// What the service knows (published to the tray when it changes).
struct ServiceSnapshot {
    bool connected = false;
    BoardVersion board;
    ProtoSettings settings{};
    std::vector<SettingsDevice> devices;  // in the board's order
    std::string picture;                  // the shown picture's id (ADR-012), "" none

    bool operator==(const ServiceSnapshot& o) const;
    bool operator!=(const ServiceSnapshot& o) const { return !(*this == o); }
};

// Everything the page shows.
struct SettingsState {
    std::string hostVersion;
    ServiceSnapshot service;
    std::optional<WeatherLocation> weather;
    PictureLibrary pictures;  // pictures.json (ADR-012)
    std::string foodLists;  // menu-lists.txt
    unsigned menuNextDayHour = 14;  // menu-settings.txt (MenuService)
    bool claudeKeySet = false;
    std::string claudeModel;
    bool startup = false;
    bool importing = false;  // a menu import is running
    std::string folder;      // the settings folder
};

// @p previewBase: where the page finds the previews ("https://<host>/").
std::string settingsStateJson(const SettingsState& state, const std::string& previewBase = std::string());
std::string placesJson(const std::vector<Place>& places);
std::string noticeJson(bool ok, const std::string& text);

enum class SettingsRequestKind {
    Get,
    Setting,
    Alias,
    Favorite,
    Order,
    WeatherSearch,
    WeatherSet,
    PicturesAdd,
    PictureRemove,
    PictureShow,
    PictureCrop,
    PictureInclude,
    PictureShuffle,
    FoodLists,
    MenuNextDay,
    ClaudeKey,
    MenuImport,
    Startup,
    CopyHook,
    OpenFolder,
    OpenLog,
};

struct SettingsRequest {
    SettingsRequestKind kind = SettingsRequestKind::Get;
    std::string name;  // setting: field; alias, favorite: device id; picture*: picture id
    std::string text;  // alias, weatherSearch query, foodLists, claudeKey
    unsigned value = 0;  // setting, favorite, startup, menuNextDay
    std::vector<std::string> ids;  // order
    WeatherLocation location;      // weatherSet
    NormRect crop;                 // pictureCrop
    ShuffleSettings shuffle;       // pictureShuffle
};

// nullopt for anything that is not a known, well-formed request (unknown
// setting names and out-of-range values included).
std::optional<SettingsRequest> parseSettingsRequest(const std::string& json);

// The requests that change the preferences (setting, alias, favorite,
// order); true if something changed. Other kinds: false.
bool applySettingsRequest(const SettingsRequest& request, Preferences& prefs);

// The requests the service loop applies.
inline bool isServiceRequest(SettingsRequestKind kind)
{
    return kind == SettingsRequestKind::Setting || kind == SettingsRequestKind::Alias ||
           kind == SettingsRequestKind::Favorite || kind == SettingsRequestKind::Order ||
           kind == SettingsRequestKind::PictureShow;
}

// claude.txt with @p apiKey and the model of @p existing (kept), for the
// claudeKey request.
std::string claudeFileWithKey(const std::optional<std::string>& existing, const std::string& apiKey);

}  // namespace stm32_audio::host
