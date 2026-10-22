// What plays on the PC (ADR-008), as the rest of the host sees it. The
// Windows implementation (WinMediaSession) reads the system media
// transport controls on its own thread; MediaService only takes
// snapshots and sends commands, so it is tested with a fake.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "MediaKeys.h"
#include "protocol.h"

namespace stm32_audio::host {

struct MediaSnapshot {
    std::uint8_t status = PROTO_MEDIA_STATUS_NONE;  // PROTO_MEDIA_STATUS_*
    std::uint8_t controls = 0;                      // PROTO_MEDIA_CAN_*
    std::string title;
    std::string artist;
    std::string album;
    std::string app;               // e.g. "Spotify"
    std::uint32_t positionS = 0;   // at sampledAtMs
    std::uint32_t durationS = 0;   // 0: unknown
    std::uint64_t sampledAtMs = 0; // monotonic clock of the reader
    // Changes with every new cover (0: none yet). The cover itself:
    // PROTO_ART_BYTES of RGB565, or null when the track has none.
    std::uint32_t coverVersion = 0;
    std::shared_ptr<const std::vector<std::uint8_t>> cover;
};

class IMediaSession {
public:
    virtual ~IMediaSession() = default;

    // The current state; cheap, any thread.
    virtual MediaSnapshot snapshot() const = 0;

    // Previous / play-pause / next for the current session. False if there
    // is no session (the caller may fall back to the media keys).
    virtual bool command(MediaKey key) = 0;

    // Jump to @p seconds from the start of the current track. False if
    // there is no session.
    virtual bool seek(std::uint32_t seconds) = 0;
};

}  // namespace stm32_audio::host
