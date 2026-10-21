// Media strip of the Dashboard tab (ADR-006, "ACTION"): previous track,
// play/pause and next track become Windows media keys. The key press
// itself is behind IMediaKeys (Windows: WinMediaKeys, SendInput), so the
// mapping is tested without Windows.
#pragma once

#include <cstdint>
#include <optional>

#include "protocol.h"

namespace stm32_audio::host {

enum class MediaKey { Previous, PlayPause, Next };

class IMediaKeys {
public:
    virtual ~IMediaKeys() = default;

    // Press and release @p key once. False if Windows refused the input.
    virtual bool press(MediaKey key) = 0;
};

// The key of a media strip action (PROTO_MEDIA_*), or nullopt.
std::optional<MediaKey> mediaKeyFor(std::uint8_t action);

// Handle an ACTION if it is the media strip's: true if the key was
// pressed. Any other action: false (for the caller to try elsewhere).
bool handleMediaAction(const ProtoAction& action, IMediaKeys& keys);

}  // namespace stm32_audio::host
