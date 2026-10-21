// IMediaKeys on Windows: one key down and one key up through SendInput
// (VK_MEDIA_PREV_TRACK, VK_MEDIA_PLAY_PAUSE, VK_MEDIA_NEXT_TRACK). Windows
// hands media keys to the app that registered for them (Spotify, a
// browser playing YouTube, the Media Player) whatever window has focus.
#pragma once

#include "MediaKeys.h"

namespace stm32_audio::host::win {

class WinMediaKeys : public IMediaKeys {
public:
    bool press(MediaKey key) override;
};

}  // namespace stm32_audio::host::win
