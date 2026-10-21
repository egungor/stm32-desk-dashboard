#include "MediaCover.h"

#include <cctype>

namespace stm32_audio::host {

SquareCrop centerSquare(std::uint32_t width, std::uint32_t height)
{
    SquareCrop crop;
    crop.size = width < height ? width : height;
    crop.x = (width - crop.size) / 2U;
    crop.y = (height - crop.size) / 2U;
    return crop;
}

std::vector<std::uint8_t> rgb565FromBgra(const std::uint8_t* pixels, std::size_t bytes)
{
    std::vector<std::uint8_t> out;
    if ((pixels == nullptr) || (bytes % 4U) != 0U) {
        return out;
    }
    out.reserve(bytes / 2U);
    for (std::size_t i = 0; i < bytes; i += 4U) {
        const unsigned b = pixels[i];
        const unsigned g = pixels[i + 1U];
        const unsigned r = pixels[i + 2U];
        const unsigned v = ((r >> 3U) << 11U) | ((g >> 2U) << 5U) | (b >> 3U);
        out.push_back(static_cast<std::uint8_t>(v & 0xFFU));
        out.push_back(static_cast<std::uint8_t>(v >> 8U));
    }
    return out;
}

std::string mediaAppName(std::string_view id)
{
    struct Known {
        const char* match;  // lower case, compared with the start of the id's name
        const char* name;
    };
    static const Known known[] = {
        {"spotify", "Spotify"},
        {"chrome", "Chrome"},
        {"msedge", "Edge"},
        {"microsoftedge", "Edge"},
        {"firefox", "Firefox"},
        {"308046b0af4a39cb", "Firefox"},  // Firefox's AppUserModelID
        {"opera", "Opera"},
        {"brave", "Brave"},
        {"vlc", "VLC"},
        {"microsoft.zunemusic", "Media Player"},
        {"microsoft.media.player", "Media Player"},
        {"applemusic", "Apple Music"},
        {"appleinc.applemusic", "Apple Music"},
        {"itunes", "iTunes"},
        {"deezer", "Deezer"},
        {"tidal", "TIDAL"},
        {"foobar2000", "foobar2000"},
        {"musicbee", "MusicBee"},
        {"winamp", "Winamp"},
    };

    // A path: keep the file name.
    const std::size_t slash = id.find_last_of("\\/");
    if (slash != std::string_view::npos) {
        id.remove_prefix(slash + 1U);
    }
    std::string lower;
    for (char c : id) {
        lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    for (const Known& k : known) {
        if (lower.compare(0, std::char_traits<char>::length(k.match), k.match) == 0) {
            return k.name;
        }
    }
    // A packaged app: "Publisher.App_hash!App" -> "App".
    std::string_view name = id;
    const std::size_t bang = name.find('!');
    if (bang != std::string_view::npos) {
        name = name.substr(bang + 1U);
    }
    if ((name.size() > 4U) && (lower.size() >= 4U) && (lower.compare(lower.size() - 4U, 4U, ".exe") == 0) &&
        (bang == std::string_view::npos)) {
        name.remove_suffix(4U);
    }
    std::string out(name);
    if (!out.empty()) {
        out[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(out[0])));
    }
    return out;
}

}  // namespace stm32_audio::host
