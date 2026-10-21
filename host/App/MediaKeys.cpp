#include "MediaKeys.h"

namespace stm32_audio::host {

std::optional<MediaKey> mediaKeyFor(std::uint8_t action)
{
    switch (action) {
    case PROTO_MEDIA_PREVIOUS:
        return MediaKey::Previous;
    case PROTO_MEDIA_PLAY_PAUSE:
        return MediaKey::PlayPause;
    case PROTO_MEDIA_NEXT:
        return MediaKey::Next;
    default:
        return std::nullopt;
    }
}

bool handleMediaAction(const ProtoAction& action, IMediaKeys& keys)
{
    if (action.kind != PROTO_ACTION_MEDIA) {
        return false;
    }
    const std::optional<MediaKey> key = mediaKeyFor(action.action);
    return key.has_value() && keys.press(*key);
}

}  // namespace stm32_audio::host
