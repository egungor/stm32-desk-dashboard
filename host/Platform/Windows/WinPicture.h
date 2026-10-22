// IPictureSource on Windows (ADR-010): the picture chosen in the tray menu
// ("Resim seç…"), whose path is kept in picture.txt in the settings folder.
// The file is read with WIC, the largest centred 200:92 rectangle is cut
// out (centerCrop: no distortion, the edges are cut) and scaled to
// 200 x 92, then turned into RGB565.
//
// Runs on the service loop's thread: reload() after the tray's event, and
// poll() looks at picture.txt every kPollMs (so "dashboard-host picture"
// works while the tray runs).
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "PictureSource.h"

namespace stm32_audio::host::win {

// The path stored in picture.txt (UTF-8), or nullopt.
std::optional<std::string> storedPicturePath();
// Store @p path (UTF-8) in picture.txt; "" removes the picture.
bool storePicturePath(const std::string& path);

// The picture file as PROTO_PICTURE_BYTES of RGB565, or an error text.
struct PictureLoad {
    std::shared_ptr<const std::vector<std::uint8_t>> pixels;
    std::string error;
};
PictureLoad loadPicture(const std::wstring& path);

class WinPicture : public IPictureSource {
public:
    using LogFn = std::function<void(const std::string& line)>;
    static constexpr std::uint64_t kPollMs = 5000;

    explicit WinPicture(LogFn log);

    // Read picture.txt and the file again.
    void reload();
    // Read picture.txt every kPollMs; reload when the path changed.
    void poll(std::uint64_t nowMs);

    PictureSnapshot snapshot() const override { return snapshot_; }

private:
    LogFn log_;
    PictureSnapshot snapshot_;
    std::optional<std::string> path_;
    std::optional<std::uint64_t> polledMs_;
};

}  // namespace stm32_audio::host::win
