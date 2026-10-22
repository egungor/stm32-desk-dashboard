// The picture card's pictures (ADR-012, #72): the photos the user added in
// the settings window, kept unchanged in the settings folder, each with the
// part shown on the board (a crop of the upright photo, 480:272) and
// whether it takes part in the shuffle; the shuffle's settings.
//
// %APPDATA%\stm32-desk-dashboard\pictures.json, written by the settings
// window only (WinPictureLibrary), read by the service (WinPicture), which
// alone keeps which picture is shown (picture-current.txt). Platform
// independent, so the rules are unit tested.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace stm32_audio::host {

// A rectangle in fractions (0..1) of the upright photo's width and height.
struct NormRect {
    double x = 0.0;
    double y = 0.0;
    double w = 1.0;
    double h = 1.0;
};

struct LibraryPicture {
    std::string id;     // "p1", "p2", ...: the file names (<id>.<ext>, previews/<id>.jpg)
    std::string file;   // the copy in the pictures folder, e.g. "p3.jpg"
    std::string name;   // the original file name, for the user
    std::uint32_t width = 0;   // the upright photo's size in pixels
    std::uint32_t height = 0;
    NormRect crop;      // the part shown on the board
    bool shuffle = true;
};

struct ShuffleSettings {
    bool enabled = false;
    unsigned minutes = 10;  // kMinShuffleMinutes .. kMaxShuffleMinutes
    bool random = true;     // false: in the library's order
};

struct PictureLibrary {
    std::vector<LibraryPicture> pictures;
    ShuffleSettings shuffle;
    unsigned nextId = 1;  // ids are never used twice (previews are cached by URL)
};

inline constexpr unsigned kMinShuffleMinutes = 1;
inline constexpr unsigned kMaxShuffleMinutes = 24U * 60U;

// nullopt if @p json is not a library; entries that are not valid are left
// out, out-of-range shuffle settings fall back to the defaults.
std::optional<PictureLibrary> parsePictureLibrary(const std::string& json);
std::string serializePictureLibrary(const PictureLibrary& library);

// The largest centred 480:272 rectangle of a @p width x @p height photo.
NormRect defaultCrop(std::uint32_t width, std::uint32_t height);

// Inside the photo, not empty, and 480:272 in pixels within 2 %.
bool validCrop(const NormRect& crop, std::uint32_t width, std::uint32_t height);

const LibraryPicture* findPicture(const PictureLibrary& library, const std::string& id);

// An id never used before ("p<n>"); counts nextId on.
std::string newPictureId(PictureLibrary& library);

// The next picture of the shuffle after @p current: the pictures that take
// part, in order, or a random one other than @p current (@p random01 in
// [0, 1) picks it). nullopt if none takes part.
std::optional<std::string> nextShufflePicture(const PictureLibrary& library, const std::string& current,
                                              double random01);

}  // namespace stm32_audio::host
