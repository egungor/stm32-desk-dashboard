// Splits the frames from the STM32 between the audio and the dashboard
// side of protocol v4 (ADR-006, "Routing and sequence numbers"): each
// frame is decoded once; commands 0x20..0x3F go to DashController, the
// others to HostController; an ERROR goes by its ref_command. Invalid
// frames are answered here with ERROR (never an ERROR with an ERROR).
//
// Platform independent. Not thread-safe: call from the thread that runs
// both controllers.
#pragma once

#include <cstdint>

#include "DashController.h"
#include "HostController.h"
#include "IFrameSink.h"

namespace stm32_audio::host {

class FrameRouter {
public:
    struct Stats {
        unsigned framesRejected = 0;  // failed Proto_Decode
        unsigned toAudio = 0;
        unsigned toDashboard = 0;
    };

    FrameRouter(HostController& audio, DashController& dash, IFrameSink& sink)
        : audio_(audio), dash_(dash), sink_(sink)
    {
    }

    static bool isDashboardCommand(std::uint8_t command) { return (command >= 0x20U) && (command <= 0x3FU); }

    void onFrame(const Frame& frame);

    const Stats& stats() const { return stats_; }

private:
    HostController& audio_;
    DashController& dash_;
    IFrameSink& sink_;
    Stats stats_;
};

}  // namespace stm32_audio::host
