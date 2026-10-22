// The picture of the picture card (ADR-010), as the rest of the host sees
// it. The Windows implementation (WinPicture) reads the file chosen in the
// tray menu with WIC; PictureService only takes snapshots, so it is tested
// with a fake.
#pragma once

#include <cstdint>
#include <memory>
#include <vector>

namespace stm32_audio::host {

struct PictureSnapshot {
    // Changes with every new picture (also to "none"); 0: nothing read yet.
    std::uint32_t version = 0;
    // PROTO_PICTURE_BYTES of RGB565 (480 x 272), or null: no picture.
    std::shared_ptr<const std::vector<std::uint8_t>> pixels;
};

class IPictureSource {
public:
    virtual ~IPictureSource() = default;
    virtual PictureSnapshot snapshot() const = 0;
};

}  // namespace stm32_audio::host
