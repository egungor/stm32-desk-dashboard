// The picture card on the board (ADR-010; protocol.md "Picture"): PICTURE
// and the picture's PICTURE_PARTs.
//
// - After the cards went out (GET_CARDS or a new connection): PICTURE with
//   the current id (0 = none), then its parts.
// - A new picture from the source: a new id (1..255), PICTURE, its parts;
//   a picture still being sent is dropped.
// - Parts go out kPartsPerTick per onTick, so other frames are not held up.
//
// Platform independent; single threaded (the service loop). Call onTick
// often while sending() is true.
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "IFrameSink.h"
#include "PictureSource.h"
#include "protocol.h"

namespace stm32_audio::host {

class PictureService {
public:
    struct Stats {
        unsigned framesSent = 0;
        unsigned sendFailures = 0;
        unsigned picturesSent = 0;  // pictures whose last part went out
    };

    static constexpr unsigned kPartsPerTick = 8U;

    PictureService(IFrameSink& sink, IPictureSource& source);

    // The cards went out to the board: send PICTURE and the parts.
    void onRequest();
    // The HID device went away: send nothing until the next request.
    void onDisconnected();
    // Take a new snapshot, then send the next parts.
    void onTick();

    bool sending() const { return connected_ && next_.has_value(); }
    std::uint8_t picId() const { return picId_; }
    const Stats& stats() const { return stats_; }

private:
    void take();
    void sendPicture();
    void send(const ProtoMessage& msg);

    IFrameSink& sink_;
    IPictureSource& source_;
    Stats stats_;
    bool connected_ = false;
    std::uint32_t version_ = 0;
    std::shared_ptr<const std::vector<std::uint8_t>> pixels_;
    std::uint8_t picId_ = PROTO_NO_PICTURE;
    std::uint8_t counter_ = 0;
    std::optional<std::uint16_t> next_;  // next part to send
};

}  // namespace stm32_audio::host
