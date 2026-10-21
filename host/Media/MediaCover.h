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

// @p pixels 32-bit BGRA pixels (B, G, R, A bytes, as WIC's
// GUID_WICPixelFormat32bppBGRA) as RGB565, two bytes per pixel, little
// endian (protocol.md, "Cover"). Alpha is ignored. Empty if the size is
// not a multiple of 4.
std::vector<std::uint8_t> rgb565FromBgra(const std::uint8_t* pixels, std::size_t bytes);

// A short name for the app of a media session from its
// SourceAppUserModelId: "Spotify.exe" -> "Spotify", "chrome" -> "Chrome",
// "Microsoft.ZuneMusic_8wekyb3d8bbwe!Microsoft.ZuneMusic" -> "Media Player".
std::string mediaAppName(std::string_view appUserModelId);

}  // namespace stm32_audio::host
