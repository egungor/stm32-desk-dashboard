#include "WinMediaKeys.h"

#include <windows.h>

namespace stm32_audio::host::win {

bool WinMediaKeys::press(MediaKey key)
{
    WORD vk = VK_MEDIA_PLAY_PAUSE;
    switch (key) {
    case MediaKey::Previous:
        vk = VK_MEDIA_PREV_TRACK;
        break;
    case MediaKey::Next:
        vk = VK_MEDIA_NEXT_TRACK;
        break;
    case MediaKey::PlayPause:
        break;
    }

    INPUT inputs[2] = {};
    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wVk = vk;
    inputs[1] = inputs[0];
    inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;
    return SendInput(2U, inputs, static_cast<int>(sizeof(INPUT))) == 2U;
}

}  // namespace stm32_audio::host::win
