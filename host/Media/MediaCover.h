// Pure helpers of the media reader (ADR-008): the cover's square crop and
// RGB565 pixels, and the name of the playing app. Platform independent so
// they are unit tested; WinMediaSession calls them.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace stm32_audio::host {

struct SquareCrop {
    std::uint32_t x = 0;
    std::uint32_t y = 0;
    std::uint32_t size = 0;  // 0 for an empty image
};

// The largest centred square of a @p width x @p height image.
SquareCrop centerSquare(std::uint32_t width, std::uint32_t height);

struct CropRect {
    std::uint32_t x = 0;
    std::uint32_t y = 0;
    std::uint32_t width = 0;   // 0 for an empty image
    std::uint32_t height = 0;
};

// The largest centred rectangle of a @p width x @p height image with the
// aspect ratio @p targetWidth : @p targetHeight (ADR-010: the picture is
// cropped at the edges, never distorted).
CropRect centerCrop(std::uint32_t width, std::uint32_t height, std::uint32_t targetWidth,
                    std::uint32_t targetHeight);

// @p pixels 32-bit BGRA pixels (B, G, R, A bytes, as WIC's
// GUID_WICPixelFormat32bppBGRA) as RGB565, two bytes per pixel, little
// endian (protocol.md, "Cover"). Alpha is ignored. Empty if the size is
// not a multiple of 4.
std::vector<std::uint8_t> rgb565FromBgra(const std::uint8_t* pixels, std::size_t bytes);

// A short name for the app of a media session from its
// SourceAppUserModelId: "Spotify.exe" -> "Spotify", "chrome" -> "Chrome",
// "Microsoft.ZuneMusic_8wekyb3d8bbwe!Microsoft.ZuneMusic" -> "Media Player".
std::string mediaAppName(std::string_view appUserModelId);

// One media session of GetSessions(), as the choice below sees it.
struct MediaCandidate {
    std::string appId;     // SourceAppUserModelId
    bool playing = false;  // PlaybackStatus Playing
    bool paused = false;   // PlaybackStatus Paused
    bool current = false;  // the session GetCurrentSession() returned
};

// The session to show and control (#75): Windows drops a paused session
// (Deezer in the browser and in its app) as the current one although it is
// still in GetSessions(). In order: the current session while it plays, a
// playing one (the one of @p lastAppId first), the one of @p lastAppId
// (shown before), the current one, a paused one. The index into
// @p sessions, or -1 for none.
int chooseMediaSession(const std::vector<MediaCandidate>& sessions, std::string_view lastAppId);

}  // namespace stm32_audio::host
