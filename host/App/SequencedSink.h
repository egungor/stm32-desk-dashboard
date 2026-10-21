// One sequence counter for every frame the host sends (ADR-006,
// "Routing and sequence numbers"): HostController and DashController both
// send through this sink, which sets the sequence byte and the CRC again,
// so the counter stays "per sender" as protocol.md defines it.
#pragma once

#include <cstdint>

#include "IFrameSink.h"
#include "protocol.h"

namespace stm32_audio::host {

class SequencedSink : public IFrameSink {
public:
    explicit SequencedSink(IFrameSink& next) : next_(next) {}

    bool sendFrame(const Frame& frame) override
    {
        Frame out = frame;
        out[3] = sequence_++;  // protocol.md, "Frame": byte 3 is the sequence
        const std::uint16_t crc = Proto_Crc16(out.data(), PROTO_CRC_OFFSET);
        out[PROTO_CRC_OFFSET] = static_cast<std::uint8_t>(crc & 0xFFU);
        out[PROTO_CRC_OFFSET + 1U] = static_cast<std::uint8_t>(crc >> 8);
        return next_.sendFrame(out);
    }

    // Start again at 0, e.g. for a new connection.
    void reset() { sequence_ = 0; }

private:
    IFrameSink& next_;
    std::uint8_t sequence_ = 0;
};

}  // namespace stm32_audio::host
