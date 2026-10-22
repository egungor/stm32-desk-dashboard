// The picture card's pictures on Windows (ADR-010, ADR-012).
//
// The library (PictureLibrary.h): the photos the user added in the settings
// window, copied unchanged into %APPDATA%\stm32-desk-dashboard\pictures,
// with a JPEG preview each (pictures\previews\<id>.jpg, at most
// kPreviewLongSide pixels, upright) for the window; the list, each photo's
// part shown on the board and the shuffle in pictures.json, written by the
// settings window only.
//
// WinPicture (IPictureSource) runs on the service loop's thread: it alone
// keeps which picture is shown (picture-current.txt: an id, or "none"),
// reads pictures.json at the start and on reloadIfChanged() (the settings
// window or "dashboard-host picture add" changed it, #61), shows another
// picture on show() (the window's request) and when the shuffle's time
// has come (onTick). The photo is read with WIC, turned upright by its EXIF
// orientation, its part cut out and scaled to 480 x 272 RGB565.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <vector>

#include "PictureLibrary.h"
#include "PictureSource.h"

namespace stm32_audio::host::win {

inline constexpr unsigned kPreviewLongSide = 1600;
inline constexpr const char* kNoPicture = "none";

// %APPDATA%\stm32-desk-dashboard\pictures, or "" if %APPDATA% is unknown.
std::wstring picturesDirectory();

// pictures.json; nullopt if it does not exist (or is not a library).
std::optional<PictureLibrary> readPictureLibrary();
bool writePictureLibrary(const PictureLibrary& library);

// picture-current.txt: the shown picture's id, kNoPicture, or "" (none
// chosen yet: the first picture). Written by the service only.
std::string readCurrentPicture();
bool writeCurrentPicture(const std::string& id);

// The photo at @p path as PROTO_PICTURE_BYTES of RGB565: @p crop of the
// upright photo, else (none, or not valid for it) the centred 480:272 part.
struct PictureLoad {
    std::shared_ptr<const std::vector<std::uint8_t>> pixels;
    std::string error;
};
PictureLoad loadPicture(const std::wstring& path, const std::optional<NormRect>& crop = std::nullopt);

// Copy @p source into the pictures folder, write its preview and append it
// to @p library (the centred part, in the shuffle). nullopt and @p error if
// it cannot be read or copied. The caller writes the library.
std::optional<LibraryPicture> addLibraryPicture(PictureLibrary& library, const std::wstring& source,
                                                std::string& error);
// Delete a picture's copy and preview (after taking it out of the library).
void removeLibraryFiles(const LibraryPicture& picture);

// Once, when there is no pictures.json yet: the picture chosen before #72
// (picture.txt) becomes the library's first. True if pictures.json was
// written; @p message says what happened.
bool migrateOldPicture(std::string& message);

class WinPicture : public IPictureSource {
public:
    using LogFn = std::function<void(const std::string& line)>;

    explicit WinPicture(LogFn log);

    // Read the library and the current picture, load it.
    void reload();
    // Read the library again: load the shown picture again if its file or
    // part changed or it was removed; restart the shuffle's time if its
    // settings changed.
    void reloadIfChanged();
    // Show picture @p id ("" = none) now; false if there is no such picture.
    bool show(const std::string& id, std::uint64_t nowMs);
    // The shuffle: the next picture when its time has come.
    void onTick(std::uint64_t nowMs);

    // The shown picture's id, "" if none.
    const std::string& currentId() const { return current_; }

    PictureSnapshot snapshot() const override { return snapshot_; }

private:
    void loadCurrent();

    LogFn log_;
    PictureSnapshot snapshot_;
    PictureLibrary library_;
    std::string current_;
    std::optional<LibraryPicture> loaded_;  // what snapshot_ holds
    std::optional<std::uint64_t> shownAtMs_;
    std::mt19937 rng_;
};

}  // namespace stm32_audio::host::win
